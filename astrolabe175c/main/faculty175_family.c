#include "faculty175_family.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_now.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "nvs.h"

#include "astrolabe_time.h"

static const char *TAG = "faculty175_family";

#define FAMILY_IDENTITY_MAGIC 0x4944454eU
#define FAMILY_WELLNESS_MAGIC 0x57454c4cU
#define FAMILY_WELLNESS_VERSION 1u
#define FAMILY_WELLNESS_STALE_MS 120000u
#define FAMILY_INIT_RETRY_MS 10000u

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t version;
    uint8_t channel;
    uint16_t size;
    uint32_t seq;
    uint32_t uptime_ms;
    uint8_t source_mac[6];
    uint8_t subject_id;
    uint8_t flags;
    uint8_t stress;
    uint8_t hrv_ms;
    uint8_t heart_rate_bpm;
    uint8_t spo2_percent;
    uint16_t sleep_total_min;
    uint16_t sleep_light_min;
    uint16_t sleep_deep_min;
    uint16_t sleep_rem_min;
    uint16_t sleep_awake_min;
    uint8_t battery_percent;
    int8_t stress_trend_30m;
} family_wellness_packet_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t version;
    uint8_t channel;
    uint16_t size;
    uint32_t seq;
    uint32_t uptime_ms;
    uint8_t source_mac[6];
    uint8_t subject_id;
    char subject_name[FACULTY175_FAMILY_SUBJECT_NAME_MAX];
} family_identity_packet_t;

typedef struct {
    bool used;
    uint8_t subject_id;
    char name[FACULTY175_FAMILY_SUBJECT_NAME_MAX];
} family_subject_t;

typedef struct {
    uint32_t day_key;
    uint32_t stress_sum;
    uint32_t load_sum;
    uint16_t stress_sample_count;
    uint16_t sample_count;
    uint16_t high_stress_samples;
    uint16_t low_hrv_samples;
    uint8_t stress_peak;
    uint8_t load_peak;
    uint32_t first_ms;
    uint32_t last_ms;
} family_day_t;

typedef struct {
    bool used;
    faculty175_family_wellness_t state;
    family_day_t day;
} family_slot_t;

static portMUX_TYPE s_family_lock = portMUX_INITIALIZER_UNLOCKED;
static family_slot_t s_slots[FACULTY175_FAMILY_SUBJECT_MAX];
static family_subject_t s_subjects[FACULTY175_FAMILY_SUBJECT_MAX] = {
    {.used = true, .subject_id = 1, .name = "Camille"},
    {.used = true, .subject_id = 2, .name = "Daniel"},
};
static bool s_ready;
static bool s_init_attempted;
static uint32_t s_next_init_ms;
static int s_channel;

static uint32_t family_now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static uint32_t family_day_key(uint32_t rx_ms)
{
    if (astrolabe_time_valid()) {
        struct tm local = {};
        astrolabe_time_local(&local);
        return (uint32_t)((local.tm_year + 1900) * 400 + local.tm_yday);
    }
    return rx_ms / 86400000u;
}

static void clean_name(char *name, size_t cap, const char *fallback)
{
    if (name == NULL || cap == 0) {
        return;
    }
    name[cap - 1] = '\0';
    for (size_t i = 0; name[i] != '\0'; ++i) {
        const unsigned char c = (unsigned char)name[i];
        if (c < 0x20 || c > 0x7e) {
            name[i] = '_';
        }
    }
    if (name[0] == '\0' && fallback != NULL) {
        snprintf(name, cap, "%s", fallback);
    }
}

static const char *subject_name_locked(uint8_t subject_id)
{
    for (size_t i = 0; i < FACULTY175_FAMILY_SUBJECT_MAX; ++i) {
        if (s_subjects[i].used && s_subjects[i].subject_id == subject_id) {
            return s_subjects[i].name;
        }
    }
    return "Unknown";
}

static void persist_subject_name(uint8_t subject_id, const char *name)
{
    nvs_handle_t nvs;
    if (nvs_open("family", NVS_READWRITE, &nvs) != ESP_OK) {
        return;
    }
    char key[16];
    snprintf(key, sizeof(key), "subject%u", subject_id);
    (void)nvs_set_str(nvs, key, name);
    (void)nvs_commit(nvs);
    nvs_close(nvs);
}

static void update_subject_name(uint8_t subject_id, const char *name)
{
    if (subject_id == 0 || name == NULL || name[0] == '\0') {
        return;
    }
    char clean[FACULTY175_FAMILY_SUBJECT_NAME_MAX];
    snprintf(clean, sizeof(clean), "%s", name);
    clean_name(clean, sizeof(clean), "Unknown");

    bool changed = false;
    portENTER_CRITICAL(&s_family_lock);
    int free_slot = -1;
    for (size_t i = 0; i < FACULTY175_FAMILY_SUBJECT_MAX; ++i) {
        if (s_subjects[i].used && s_subjects[i].subject_id == subject_id) {
            if (strncmp(s_subjects[i].name, clean, sizeof(s_subjects[i].name)) != 0) {
                snprintf(s_subjects[i].name, sizeof(s_subjects[i].name), "%s", clean);
                changed = true;
            }
            portEXIT_CRITICAL(&s_family_lock);
            if (changed) {
                persist_subject_name(subject_id, clean);
                ESP_LOGI(TAG, "learned subject=%u name=%s", subject_id, clean);
            }
            return;
        }
        if (!s_subjects[i].used && free_slot < 0) {
            free_slot = (int)i;
        }
    }
    if (free_slot >= 0) {
        s_subjects[free_slot].used = true;
        s_subjects[free_slot].subject_id = subject_id;
        snprintf(s_subjects[free_slot].name, sizeof(s_subjects[free_slot].name), "%s", clean);
        changed = true;
    }
    portEXIT_CRITICAL(&s_family_lock);
    if (changed) {
        persist_subject_name(subject_id, clean);
        ESP_LOGI(TAG, "learned subject=%u name=%s", subject_id, clean);
    }
}

static int slot_for_subject_locked(uint8_t subject_id, bool allocate)
{
    int free_slot = -1;
    for (size_t i = 0; i < FACULTY175_FAMILY_SUBJECT_MAX; ++i) {
        if (s_slots[i].used && s_slots[i].state.subject_id == subject_id) {
            return (int)i;
        }
        if (!s_slots[i].used && free_slot < 0) {
            free_slot = (int)i;
        }
    }
    if (!allocate || free_slot < 0) {
        return -1;
    }
    s_slots[free_slot].used = true;
    s_slots[free_slot].state.valid = true;
    s_slots[free_slot].state.subject_id = subject_id;
    snprintf(s_slots[free_slot].state.subject_name,
             sizeof(s_slots[free_slot].state.subject_name),
             "%s",
             subject_name_locked(subject_id));
    return free_slot;
}

static void copy_packet_to_state(faculty175_family_wellness_t *state,
                                 const family_wellness_packet_t *packet,
                                 const uint8_t *src,
                                 uint32_t rx_ms)
{
    state->valid = true;
    state->subject_id = packet->subject_id;
    state->seq = packet->seq;
    state->last_rx_ms = rx_ms;
    state->age_ms = 0;
    state->flags = packet->flags;
    state->stress = packet->stress;
    state->hrv_ms = packet->hrv_ms;
    state->heart_rate_bpm = packet->heart_rate_bpm;
    state->spo2_percent = packet->spo2_percent;
    state->sleep_total_min = packet->sleep_total_min;
    state->sleep_light_min = packet->sleep_light_min;
    state->sleep_deep_min = packet->sleep_deep_min;
    state->sleep_rem_min = packet->sleep_rem_min;
    state->sleep_awake_min = packet->sleep_awake_min;
    state->battery_percent = packet->battery_percent;
    state->stress_trend_30m = packet->stress_trend_30m;
    memcpy(state->source_mac, src, sizeof(state->source_mac));
}

static void copy_day_to_state(faculty175_family_wellness_t *state, const family_day_t *day)
{
    if (state == NULL || day == NULL) {
        return;
    }
    state->day_sample_count = day->sample_count;
    state->day_stress_avg = day->stress_sample_count > 0 ? (uint8_t)(day->stress_sum / day->stress_sample_count) : 0;
    state->day_stress_peak = day->stress_peak;
    state->day_high_stress_samples = day->high_stress_samples;
    state->day_low_hrv_samples = day->low_hrv_samples;
    state->day_load_avg = day->sample_count > 0 ? (uint8_t)(day->load_sum / day->sample_count) : 0;
    state->day_load_peak = day->load_peak;
    state->day_first_ms = day->first_ms;
    state->day_last_ms = day->last_ms;
}

static void update_day_state(family_slot_t *slot, uint32_t rx_ms)
{
    if (slot == NULL) {
        return;
    }
    faculty175_family_wellness_t *state = &slot->state;
    family_day_t *day = &slot->day;
    const uint32_t day_key = family_day_key(rx_ms);
    if (day->sample_count == 0 || day->day_key != day_key) {
        memset(day, 0, sizeof(*day));
        day->day_key = day_key;
        day->first_ms = rx_ms;
    }

    bool sampled = false;
    if ((state->flags & FACULTY175_FAMILY_FLAG_STRESS_VALID) != 0) {
        day->stress_sum += state->stress;
        ++day->stress_sample_count;
        if (state->stress > day->stress_peak) {
            day->stress_peak = state->stress;
        }
        if (state->stress >= 70) {
            ++day->high_stress_samples;
        }
        sampled = true;
    }
    if ((state->flags & FACULTY175_FAMILY_FLAG_HRV_VALID) != 0 && state->hrv_ms > 0 && state->hrv_ms < 35) {
        ++day->low_hrv_samples;
        sampled = true;
    }
    if (sampled) {
        const uint8_t load = faculty175_family_load_score(state);
        day->load_sum += load;
        if (load > day->load_peak) {
            day->load_peak = load;
        }
        ++day->sample_count;
        day->last_ms = rx_ms;
    }
    copy_day_to_state(state, day);
}

const char *faculty175_family_guidance_cue(const faculty175_family_wellness_t *state)
{
    if (state == NULL || !state->valid) {
        return "waiting";
    }
    const bool stress = (state->flags & FACULTY175_FAMILY_FLAG_STRESS_VALID) != 0;
    const bool hrv = (state->flags & FACULTY175_FAMILY_FLAG_HRV_VALID) != 0;
    const bool sleep = (state->flags & FACULTY175_FAMILY_FLAG_SLEEP_VALID) != 0;
    const bool spo2 = (state->flags & FACULTY175_FAMILY_FLAG_SPO2_VALID) != 0;
    if (state->age_ms > FAMILY_WELLNESS_STALE_MS) {
        return "stale";
    }
    if (spo2 && state->spo2_percent > 0 && state->spo2_percent < 92) {
        return "low-spo2";
    }
    if (stress && state->stress >= 70 && state->stress_trend_30m >= 8) {
        return "check-in-soon";
    }
    if (stress && state->stress >= 70) {
        return "offer-grounding";
    }
    if (sleep && state->sleep_total_min > 0 && state->sleep_total_min < 360) {
        return "protect-rest";
    }
    if (hrv && state->hrv_ms > 0 && state->hrv_ms < 35) {
        return "slow-breath";
    }
    return "steady";
}

uint16_t faculty175_family_sleep_debt_min(const faculty175_family_wellness_t *state)
{
    if (state == NULL || (state->flags & FACULTY175_FAMILY_FLAG_SLEEP_VALID) == 0 || state->sleep_total_min >= 420) {
        return 0;
    }
    return (uint16_t)(420u - state->sleep_total_min);
}

uint8_t faculty175_family_load_score(const faculty175_family_wellness_t *state)
{
    if (state == NULL || !state->valid) {
        return 0;
    }
    int score = 0;
    if ((state->flags & FACULTY175_FAMILY_FLAG_STRESS_VALID) != 0) {
        score = state->stress;
        score += state->stress_trend_30m > 0 ? state->stress_trend_30m / 2 : 0;
    }
    if ((state->flags & FACULTY175_FAMILY_FLAG_HRV_VALID) != 0 && state->hrv_ms > 0 && state->hrv_ms < 35) {
        score += 10;
    }
    if (faculty175_family_sleep_debt_min(state) > 60) {
        score += 8;
    }
    if ((state->flags & FACULTY175_FAMILY_FLAG_SPO2_VALID) != 0 && state->spo2_percent > 0 && state->spo2_percent < 94) {
        score += 12;
    }
    if (state->age_ms > FAMILY_WELLNESS_STALE_MS) {
        score /= 2;
    }
    if (score < 0) {
        score = 0;
    } else if (score > 100) {
        score = 100;
    }
    return (uint8_t)score;
}

static void log_wellness(const faculty175_family_wellness_t *state)
{
    ESP_LOGI(TAG,
             "wellness subject=%s id=%u cue=%s score=%u age=%" PRIu32
             " seq=%" PRIu32 " flags=0x%02x stress=%u trend=%d hrv=%u hr=%u spo2=%u sleep=%u debt=%u batt=%u",
             state->subject_name,
             state->subject_id,
             faculty175_family_guidance_cue(state),
             faculty175_family_load_score(state),
             state->age_ms,
             state->seq,
             state->flags,
             state->stress,
             state->stress_trend_30m,
             state->hrv_ms,
             state->heart_rate_bpm,
             state->spo2_percent,
             state->sleep_total_min,
             faculty175_family_sleep_debt_min(state),
             state->battery_percent);
}

static void handle_wellness_packet(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (len != (int)sizeof(family_wellness_packet_t)) {
        return;
    }
    family_wellness_packet_t packet;
    memcpy(&packet, data, sizeof(packet));
    if (packet.magic != FAMILY_WELLNESS_MAGIC || packet.version != FAMILY_WELLNESS_VERSION ||
        packet.size != sizeof(packet) || packet.subject_id == 0) {
        return;
    }
    const uint8_t *src = packet.source_mac;
    static const uint8_t zero_mac[6] = {};
    if (memcmp(src, zero_mac, sizeof(zero_mac)) == 0 && info != NULL) {
        src = info->src_addr;
    }

    const uint32_t rx_ms = family_now_ms();
    faculty175_family_wellness_t snapshot = {};
    bool stored = false;
    portENTER_CRITICAL(&s_family_lock);
    const int idx = slot_for_subject_locked(packet.subject_id, true);
    if (idx >= 0) {
        copy_packet_to_state(&s_slots[idx].state, &packet, src, rx_ms);
        snprintf(s_slots[idx].state.subject_name,
                 sizeof(s_slots[idx].state.subject_name),
                 "%s",
                 subject_name_locked(packet.subject_id));
        update_day_state(&s_slots[idx], rx_ms);
        snapshot = s_slots[idx].state;
        stored = true;
    }
    portEXIT_CRITICAL(&s_family_lock);
    if (stored) {
        log_wellness(&snapshot);
    } else {
        ESP_LOGW(TAG, "wellness table full; dropped subject=%u seq=%" PRIu32, packet.subject_id, packet.seq);
    }
}

static void handle_identity_packet(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    (void)info;
    if (len != (int)sizeof(family_identity_packet_t)) {
        return;
    }
    family_identity_packet_t packet;
    memcpy(&packet, data, sizeof(packet));
    if (packet.magic != FAMILY_IDENTITY_MAGIC || packet.version != FAMILY_WELLNESS_VERSION ||
        packet.size != sizeof(packet) || packet.subject_id == 0) {
        return;
    }
    packet.subject_name[sizeof(packet.subject_name) - 1] = '\0';
    clean_name(packet.subject_name, sizeof(packet.subject_name), "Unknown");
    update_subject_name(packet.subject_id, packet.subject_name);
}

static void family_recv_cb(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (data == NULL || len < (int)sizeof(uint32_t)) {
        return;
    }
    uint32_t magic = 0;
    memcpy(&magic, data, sizeof(magic));
    if (magic == FAMILY_IDENTITY_MAGIC) {
        handle_identity_packet(info, data, len);
    } else if (magic == FAMILY_WELLNESS_MAGIC) {
        handle_wellness_packet(info, data, len);
    }
}

static void load_subject_names(void)
{
    nvs_handle_t nvs;
    if (nvs_open("family", NVS_READONLY, &nvs) != ESP_OK) {
        return;
    }
    for (size_t i = 0; i < FACULTY175_FAMILY_SUBJECT_MAX; ++i) {
        if (!s_subjects[i].used) {
            continue;
        }
        char key[16];
        snprintf(key, sizeof(key), "subject%u", s_subjects[i].subject_id);
        size_t len = sizeof(s_subjects[i].name);
        if (nvs_get_str(nvs, key, s_subjects[i].name, &len) == ESP_OK) {
            clean_name(s_subjects[i].name, sizeof(s_subjects[i].name), "Unknown");
        }
    }
    nvs_close(nvs);
}

esp_err_t faculty175_family_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }
    s_init_attempted = true;
    load_subject_names();
    wifi_mode_t mode = WIFI_MODE_NULL;
    esp_err_t err = esp_wifi_get_mode(&mode);
    if (err != ESP_OK || mode == WIFI_MODE_NULL) {
        s_next_init_ms = family_now_ms() + FAMILY_INIT_RETRY_MS;
        return err == ESP_OK ? ESP_ERR_INVALID_STATE : err;
    }
    uint8_t primary = 0;
    wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
    if (esp_wifi_get_channel(&primary, &second) == ESP_OK) {
        s_channel = primary;
    }
    err = esp_now_init();
    if (err != ESP_OK && err != ESP_ERR_ESPNOW_INTERNAL) {
        s_next_init_ms = family_now_ms() + FAMILY_INIT_RETRY_MS;
        ESP_LOGW(TAG, "esp-now init failed: %s", esp_err_to_name(err));
        return err;
    }
    err = esp_now_register_recv_cb(family_recv_cb);
    if (err != ESP_OK && err != ESP_ERR_ESPNOW_EXIST) {
        s_next_init_ms = family_now_ms() + FAMILY_INIT_RETRY_MS;
        ESP_LOGW(TAG, "esp-now recv register failed: %s", esp_err_to_name(err));
        return err;
    }
    s_ready = true;
    ESP_LOGI(TAG, "ESP-NOW wellness receive ready channel=%d", s_channel);
    return ESP_OK;
}

void faculty175_family_tick(uint32_t now_ms)
{
    if (!s_ready && (!s_init_attempted || (int32_t)(now_ms - s_next_init_ms) >= 0)) {
        (void)faculty175_family_init();
    }
}

bool faculty175_family_ready(void)
{
    return s_ready;
}

int faculty175_family_channel(void)
{
    return s_channel;
}

size_t faculty175_family_snapshot(faculty175_family_wellness_t *out, size_t cap)
{
    if (out == NULL || cap == 0) {
        return 0;
    }
    const uint32_t now_ms = family_now_ms();
    size_t count = 0;
    portENTER_CRITICAL(&s_family_lock);
    for (size_t i = 0; i < FACULTY175_FAMILY_SUBJECT_MAX && count < cap; ++i) {
        if (!s_slots[i].used || !s_slots[i].state.valid) {
            continue;
        }
        out[count] = s_slots[i].state;
        out[count].age_ms = now_ms - out[count].last_rx_ms;
        copy_day_to_state(&out[count], &s_slots[i].day);
        ++count;
    }
    portEXIT_CRITICAL(&s_family_lock);
    return count;
}

bool faculty175_family_primary_partner(faculty175_family_wellness_t *out)
{
    faculty175_family_wellness_t states[FACULTY175_FAMILY_SUBJECT_MAX] = {};
    const size_t count = faculty175_family_snapshot(states, FACULTY175_FAMILY_SUBJECT_MAX);
    if (count == 0) {
        return false;
    }
    size_t best = 0;
    for (size_t i = 1; i < count; ++i) {
        const uint8_t score = faculty175_family_load_score(&states[i]);
        const uint8_t best_score = faculty175_family_load_score(&states[best]);
        if (score > best_score || (score == best_score && states[i].age_ms < states[best].age_ms)) {
            best = i;
        }
    }
    if (out != NULL) {
        *out = states[best];
    }
    return true;
}

void faculty175_family_format_summary(char *out, size_t cap)
{
    if (out == NULL || cap == 0) {
        return;
    }
    out[0] = '\0';
    faculty175_family_wellness_t partner = {};
    if (!faculty175_family_primary_partner(&partner)) {
        snprintf(out, cap, "family wellness: waiting for paired devices");
        return;
    }
    snprintf(out,
             cap,
             "%s cue=%s score=%u stress=%u trend=%+d hrv=%u day_avg=%u day_peak=%u sleep=%uh%02u age=%lus",
             partner.subject_name,
             faculty175_family_guidance_cue(&partner),
             faculty175_family_load_score(&partner),
             partner.stress,
             partner.stress_trend_30m,
             partner.hrv_ms,
             partner.day_stress_avg,
             partner.day_stress_peak,
             partner.sleep_total_min / 60,
             partner.sleep_total_min % 60,
             (unsigned long)(partner.age_ms / 1000u));
}

void faculty175_family_format_day_summary(const faculty175_family_wellness_t *state, char *out, size_t cap)
{
    if (out == NULL || cap == 0) {
        return;
    }
    out[0] = '\0';
    if (state == NULL || !state->valid || state->day_sample_count == 0) {
        snprintf(out, cap, "day so far: waiting for ring samples");
        return;
    }
    snprintf(out,
             cap,
             "day avg %u peak %u high %u/%u low-hrv %u load %u/%u",
             state->day_stress_avg,
             state->day_stress_peak,
             state->day_high_stress_samples,
             state->day_sample_count,
             state->day_low_hrv_samples,
             state->day_load_avg,
             state->day_load_peak);
}

void faculty175_family_format_voice_context(char *out, size_t cap)
{
    if (out == NULL || cap == 0) {
        return;
    }
    faculty175_family_wellness_t partner = {};
    if (!faculty175_family_primary_partner(&partner)) {
        snprintf(out, cap, "Family wellness context: no paired wellness packet has been received yet.");
        return;
    }
    snprintf(out,
             cap,
             "Family wellness context: %s has cue %s, load score %u, stress %u with 30 minute trend %+d, HRV %u ms, heart rate %u bpm, SpO2 %u%%, sleep %u minutes with %u minutes estimated sleep debt. Day so far has %u samples, average stress %u, peak stress %u, high-stress samples %u, low-HRV samples %u, average load %u and peak load %u. Use this gently: suggest check-ins and supportive regulation, not diagnosis.",
             partner.subject_name,
             faculty175_family_guidance_cue(&partner),
             faculty175_family_load_score(&partner),
             partner.stress,
             partner.stress_trend_30m,
             partner.hrv_ms,
             partner.heart_rate_bpm,
             partner.spo2_percent,
             partner.sleep_total_min,
             faculty175_family_sleep_debt_min(&partner),
             partner.day_sample_count,
             partner.day_stress_avg,
             partner.day_stress_peak,
             partner.day_high_stress_samples,
             partner.day_low_hrv_samples,
             partner.day_load_avg,
             partner.day_load_peak);
}
