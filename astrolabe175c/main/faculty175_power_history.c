#include "faculty175_power_history.h"

#include <stdio.h>
#include <string.h>

#include "astrolabe_time.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"

#define POWER_HISTORY_NAMESPACE "power_hist"
#define POWER_HISTORY_INTERVAL_S (15u * 60u)

static const char *TAG = "faculty175_power_history";
static SemaphoreHandle_t s_lock;
static bool s_loaded;
static uint8_t s_head;
static uint8_t s_count;
EXT_RAM_BSS_ATTR static faculty175_power_history_sample_t s_samples[FACULTY175_POWER_HISTORY_CAPACITY];

static SemaphoreHandle_t history_lock(void)
{
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
    }
    return s_lock;
}

static void slot_key(char key[4], uint8_t slot)
{
    snprintf(key, 4, "s%02u", (unsigned)slot);
}

static void load_locked(void)
{
    if (s_loaded) {
        return;
    }
    s_loaded = true;
    nvs_handle_t nvs;
    if (nvs_open(POWER_HISTORY_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) {
        return;
    }
    uint8_t head = 0;
    uint8_t count = 0;
    if (nvs_get_u8(nvs, "head", &head) != ESP_OK ||
        nvs_get_u8(nvs, "count", &count) != ESP_OK ||
        head >= FACULTY175_POWER_HISTORY_CAPACITY ||
        count > FACULTY175_POWER_HISTORY_CAPACITY) {
        nvs_close(nvs);
        return;
    }
    size_t valid = 0;
    const uint8_t oldest = (uint8_t)((head + FACULTY175_POWER_HISTORY_CAPACITY - count) %
                                     FACULTY175_POWER_HISTORY_CAPACITY);
    for (uint8_t i = 0; i < count; ++i) {
        const uint8_t slot = (uint8_t)((oldest + i) % FACULTY175_POWER_HISTORY_CAPACITY);
        char key[4];
        slot_key(key, slot);
        size_t size = sizeof(s_samples[slot]);
        if (nvs_get_blob(nvs, key, &s_samples[slot], &size) == ESP_OK &&
            size == sizeof(s_samples[slot])) {
            ++valid;
        } else {
            break;
        }
    }
    nvs_close(nvs);
    if (valid == count) {
        s_head = head;
        s_count = count;
    } else {
        memset(s_samples, 0, sizeof(s_samples));
        s_head = 0;
        s_count = 0;
        ESP_LOGW(TAG, "discarded corrupt history ring");
    }
}

static uint8_t sample_flags(const faculty175_pmu_status_t *pmu)
{
    return (pmu->battery_present ? FACULTY175_POWER_HISTORY_BATTERY_PRESENT : 0) |
           (pmu->vbus_in ? FACULTY175_POWER_HISTORY_VBUS : 0) |
           (pmu->charging ? FACULTY175_POWER_HISTORY_CHARGING : 0) |
           (pmu->discharging ? FACULTY175_POWER_HISTORY_DISCHARGING : 0);
}

void faculty175_power_history_maybe_record(const faculty175_pmu_status_t *pmu)
{
    if (pmu == NULL || !pmu->present || pmu->battery_percent < 0 ||
        pmu->battery_percent > 100 || !astrolabe_time_valid()) {
        return;
    }
    SemaphoreHandle_t lock = history_lock();
    if (lock == NULL || xSemaphoreTake(lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
    load_locked();
    const uint32_t epoch_s = (uint32_t)astrolabe_time_now();
    const uint8_t flags = sample_flags(pmu);
    bool record = s_count == 0;
    if (!record) {
        const uint8_t newest = (uint8_t)((s_head + FACULTY175_POWER_HISTORY_CAPACITY - 1) %
                                         FACULTY175_POWER_HISTORY_CAPACITY);
        const faculty175_power_history_sample_t *last = &s_samples[newest];
        const uint8_t transition_mask = FACULTY175_POWER_HISTORY_VBUS |
                                        FACULTY175_POWER_HISTORY_CHARGING |
                                        FACULTY175_POWER_HISTORY_DISCHARGING;
        record = epoch_s < last->epoch_s || epoch_s - last->epoch_s >= POWER_HISTORY_INTERVAL_S ||
                 last->battery_percent != (uint8_t)pmu->battery_percent ||
                 ((last->flags ^ flags) & transition_mask) != 0;
    }
    if (!record) {
        xSemaphoreGive(lock);
        return;
    }

    const uint8_t slot = s_head;
    const faculty175_power_history_sample_t sample = {
        .epoch_s = epoch_s,
        .battery_mv = pmu->battery_mv,
        .battery_percent = (uint8_t)pmu->battery_percent,
        .flags = flags,
    };
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(POWER_HISTORY_NAMESPACE, NVS_READWRITE, &nvs);
    if (err == ESP_OK) {
        char key[4];
        slot_key(key, slot);
        const uint8_t next_head = (uint8_t)((slot + 1) % FACULTY175_POWER_HISTORY_CAPACITY);
        const uint8_t next_count = s_count < FACULTY175_POWER_HISTORY_CAPACITY
                                       ? (uint8_t)(s_count + 1)
                                       : s_count;
        err = nvs_set_blob(nvs, key, &sample, sizeof(sample));
        if (err == ESP_OK) err = nvs_set_u8(nvs, "head", next_head);
        if (err == ESP_OK) err = nvs_set_u8(nvs, "count", next_count);
        if (err == ESP_OK) err = nvs_commit(nvs);
        nvs_close(nvs);
        if (err == ESP_OK) {
            s_samples[slot] = sample;
            s_head = next_head;
            s_count = next_count;
        }
    }
    xSemaphoreGive(lock);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "history write failed: %s", esp_err_to_name(err));
    }
}

size_t faculty175_power_history_load(faculty175_power_history_sample_t *out, size_t capacity)
{
    if (out == NULL || capacity == 0) {
        return 0;
    }
    SemaphoreHandle_t lock = history_lock();
    if (lock == NULL || xSemaphoreTake(lock, pdMS_TO_TICKS(250)) != pdTRUE) {
        return 0;
    }
    load_locked();
    const size_t count = s_count < capacity ? s_count : capacity;
    const uint8_t oldest = (uint8_t)((s_head + FACULTY175_POWER_HISTORY_CAPACITY - s_count) %
                                     FACULTY175_POWER_HISTORY_CAPACITY);
    const uint8_t skip = (uint8_t)(s_count - count);
    for (size_t i = 0; i < count; ++i) {
        const uint8_t slot = (uint8_t)((oldest + skip + i) % FACULTY175_POWER_HISTORY_CAPACITY);
        out[i] = s_samples[slot];
    }
    xSemaphoreGive(lock);
    return count;
}

bool faculty175_power_history_estimate(const faculty175_pmu_status_t *pmu,
                                       faculty175_power_history_estimate_t *out)
{
    if (out == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    if (pmu == NULL || !pmu->present || !pmu->battery_present || pmu->vbus_in ||
        pmu->charging || pmu->battery_percent < 0 || pmu->battery_percent > 100 ||
        !astrolabe_time_valid()) {
        return false;
    }
    faculty175_power_history_sample_t samples[FACULTY175_POWER_HISTORY_CAPACITY];
    const size_t count = faculty175_power_history_load(samples, FACULTY175_POWER_HISTORY_CAPACITY);
    const uint32_t now_s = (uint32_t)astrolabe_time_now();
    for (size_t i = count; i-- > 0;) {
        const faculty175_power_history_sample_t *sample = &samples[i];
        if ((sample->flags & FACULTY175_POWER_HISTORY_BATTERY_PRESENT) == 0 ||
            (sample->flags & (FACULTY175_POWER_HISTORY_VBUS |
                              FACULTY175_POWER_HISTORY_CHARGING)) != 0) {
            break;
        }
        if (sample->epoch_s > now_s || sample->battery_percent < pmu->battery_percent) {
            continue;
        }
        const uint32_t elapsed_s = now_s - sample->epoch_s;
        const int drop = (int)sample->battery_percent - pmu->battery_percent;
        if (elapsed_s >= POWER_HISTORY_INTERVAL_S && drop >= 1 && elapsed_s > out->elapsed_s) {
            out->valid = true;
            out->elapsed_s = elapsed_s;
            out->drop_percent = drop;
            out->percent_per_hour = (float)drop * 3600.0f / (float)elapsed_s;
            out->remaining_hours = out->percent_per_hour > 0.0f
                                       ? (float)pmu->battery_percent / out->percent_per_hour
                                       : 0.0f;
        }
    }
    return out->valid;
}
