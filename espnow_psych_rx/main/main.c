#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "psych_rx";

#define ESPNOW_CHANNEL 6
#define PSYCH_MAGIC 0x50535943u
#define IDENTITY_MAGIC 0x4944454eu
#define WELLNESS_MAGIC 0x57454c4cu
#define WELLNESS_SUBJECT_CAMILLE 1u
#define WELLNESS_STATE_STALE_MS 120000u
#define WELLNESS_MONITOR_PERIOD_MS 30000u
#define WELLNESS_MAX_STATES 4u
#define IDENTITY_NAME_MAX 24u

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t version;
    uint8_t channel;
    uint16_t size;
    uint32_t seq;
    uint32_t uptime_ms;
    uint8_t mac[6];
    int8_t arousal;
    int8_t valence;
    uint8_t focus_axis;
    int16_t encoder_steps;
    uint16_t button_presses;
} psychometer_packet_t;

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
    char subject_name[IDENTITY_NAME_MAX];
} family_identity_packet_t;

enum {
    WELLNESS_FLAG_STRESS_VALID = 1u << 0,
    WELLNESS_FLAG_HRV_VALID = 1u << 1,
    WELLNESS_FLAG_HR_VALID = 1u << 2,
    WELLNESS_FLAG_SPO2_VALID = 1u << 3,
    WELLNESS_FLAG_SLEEP_VALID = 1u << 4,
    WELLNESS_FLAG_BATTERY_VALID = 1u << 5,
};

typedef struct {
    bool used;
    uint8_t subject_id;
    uint8_t source_mac[6];
    uint32_t last_rx_ms;
    uint32_t last_seq;
    family_wellness_packet_t latest;
} wellness_state_t;

typedef struct {
    uint8_t subject_id;
    char name[IDENTITY_NAME_MAX];
} subject_identity_t;

static wellness_state_t s_wellness_states[WELLNESS_MAX_STATES];
static portMUX_TYPE s_wellness_lock = portMUX_INITIALIZER_UNLOCKED;
static char s_device_name[IDENTITY_NAME_MAX] = "LunaSay";
static subject_identity_t s_subjects[] = {
    {WELLNESS_SUBJECT_CAMILLE, "LunaSay"},
    {2, "LunaSay"},
};

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static bool sanitize_identity_name(char *name, size_t size, const char *fallback)
{
    bool changed = false;
    if (!name || size == 0) {
        return true;
    }
    name[size - 1] = '\0';
    for (size_t i = 0; name[i] != '\0'; ++i) {
        unsigned char c = (unsigned char)name[i];
        if (c < 0x20 || c > 0x7e) {
            name[i] = '_';
            changed = true;
        }
    }
    if (name[0] == '\0') {
        snprintf(name, size, "%s", fallback);
        changed = true;
    }
    return changed;
}

static void load_identity_from_nvs(void)
{
    nvs_handle_t nvs = 0;
    esp_err_t err = nvs_open("identity", NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "identity NVS open failed: %s", esp_err_to_name(err));
        return;
    }

    bool commit = false;
    size_t len = sizeof(s_device_name);
    err = nvs_get_str(nvs, "device_name", s_device_name, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_set_str(nvs, "device_name", s_device_name));
        commit = true;
    } else if (err != ESP_OK) {
        ESP_LOGW(TAG, "device name read failed: %s", esp_err_to_name(err));
    }
    if (sanitize_identity_name(s_device_name, sizeof(s_device_name), "LunaSay")) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_set_str(nvs, "device_name", s_device_name));
        commit = true;
    }

    for (size_t i = 0; i < sizeof(s_subjects) / sizeof(s_subjects[0]); ++i) {
        char key[12] = {};
        snprintf(key, sizeof(key), "subject%u", s_subjects[i].subject_id);
        len = sizeof(s_subjects[i].name);
        err = nvs_get_str(nvs, key, s_subjects[i].name, &len);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_set_str(nvs, key, s_subjects[i].name));
            commit = true;
        } else if (err != ESP_OK) {
            ESP_LOGW(TAG, "%s read failed: %s", key, esp_err_to_name(err));
        }
        if (sanitize_identity_name(s_subjects[i].name, sizeof(s_subjects[i].name), "Unknown")) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_set_str(nvs, key, s_subjects[i].name));
            commit = true;
        }
    }

    if (commit) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_commit(nvs));
    }
    nvs_close(nvs);
    ESP_LOGI(TAG, "identity device=%s subject1=%s subject2=%s",
             s_device_name, s_subjects[0].name, s_subjects[1].name);
}

static const char *subject_name(uint8_t subject_id)
{
    for (size_t i = 0; i < sizeof(s_subjects) / sizeof(s_subjects[0]); ++i) {
        if (s_subjects[i].subject_id == subject_id) {
            return s_subjects[i].name;
        }
    }
    return "Unknown";
}

static void persist_subject_name(uint8_t subject_id, const char *name)
{
    nvs_handle_t nvs = 0;
    esp_err_t err = nvs_open("identity", NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "identity NVS open for subject update failed: %s", esp_err_to_name(err));
        return;
    }
    char key[12] = {};
    snprintf(key, sizeof(key), "subject%u", subject_id);
    ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_set_str(nvs, key, name));
    ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_commit(nvs));
    nvs_close(nvs);
}

static void update_subject_name(uint8_t subject_id, const char *name)
{
    if (subject_id == 0 || !name || name[0] == '\0') {
        return;
    }
    char clean[IDENTITY_NAME_MAX] = {};
    snprintf(clean, sizeof(clean), "%s", name);
    sanitize_identity_name(clean, sizeof(clean), "Unknown");

    for (size_t i = 0; i < sizeof(s_subjects) / sizeof(s_subjects[0]); ++i) {
        if (s_subjects[i].subject_id == subject_id) {
            if (strncmp(s_subjects[i].name, clean, sizeof(s_subjects[i].name)) != 0) {
                snprintf(s_subjects[i].name, sizeof(s_subjects[i].name), "%s", clean);
                persist_subject_name(subject_id, clean);
                ESP_LOGI(TAG, "identity learned subject=%u name=%s", subject_id, clean);
            }
            return;
        }
    }
}

static int find_wellness_state(uint8_t subject_id)
{
    for (size_t i = 0; i < WELLNESS_MAX_STATES; ++i) {
        if (s_wellness_states[i].used && s_wellness_states[i].subject_id == subject_id) {
            return (int)i;
        }
    }
    return -1;
}

static int allocate_wellness_state(uint8_t subject_id)
{
    int idx = find_wellness_state(subject_id);
    if (idx >= 0) {
        return idx;
    }
    for (size_t i = 0; i < WELLNESS_MAX_STATES; ++i) {
        if (!s_wellness_states[i].used) {
            s_wellness_states[i].used = true;
            s_wellness_states[i].subject_id = subject_id;
            return (int)i;
        }
    }
    return -1;
}

static const char *guidance_cue(const family_wellness_packet_t *packet)
{
    const bool stress_valid = (packet->flags & WELLNESS_FLAG_STRESS_VALID) != 0;
    const bool hrv_valid = (packet->flags & WELLNESS_FLAG_HRV_VALID) != 0;
    const bool sleep_valid = (packet->flags & WELLNESS_FLAG_SLEEP_VALID) != 0;
    if (stress_valid && packet->stress >= 70 && packet->stress_trend_30m >= 8) {
        return "check-in-soon";
    }
    if (stress_valid && packet->stress >= 70) {
        return "offer-grounding";
    }
    if (sleep_valid && packet->sleep_total_min > 0 && packet->sleep_total_min < 360) {
        return "protect-rest";
    }
    if (hrv_valid && packet->hrv_ms > 0 && packet->hrv_ms < 35) {
        return "slow-breath";
    }
    return "steady";
}

static uint16_t sleep_debt_min(const family_wellness_packet_t *packet)
{
    if ((packet->flags & WELLNESS_FLAG_SLEEP_VALID) == 0 || packet->sleep_total_min >= 420) {
        return 0;
    }
    return (uint16_t)(420u - packet->sleep_total_min);
}

static void emit_guidance_context(const wellness_state_t *state, uint32_t age_ms, const char *status)
{
    const family_wellness_packet_t *packet = &state->latest;
    ESP_LOGI(TAG,
             "tts_context subject=%s status=%s age_ms=%" PRIu32
             " cue=%s stress=%u trend30m=%d hrv=%u hr=%u spo2=%u sleep=%u sleep_debt=%u batt=%u flags=0x%02x",
             subject_name(state->subject_id),
             status,
             age_ms,
             guidance_cue(packet),
             packet->stress,
             packet->stress_trend_30m,
             packet->hrv_ms,
             packet->heart_rate_bpm,
             packet->spo2_percent,
             packet->sleep_total_min,
             sleep_debt_min(packet),
             packet->battery_percent,
             packet->flags);
}

static void update_wellness_state(const family_wellness_packet_t *packet, const uint8_t *src)
{
    const uint32_t rx_ms = now_ms();
    portENTER_CRITICAL(&s_wellness_lock);
    const int idx = allocate_wellness_state(packet->subject_id);
    if (idx >= 0) {
        wellness_state_t *state = &s_wellness_states[idx];
        state->last_rx_ms = rx_ms;
        state->last_seq = packet->seq;
        state->latest = *packet;
        memcpy(state->source_mac, src, 6);
    }
    portEXIT_CRITICAL(&s_wellness_lock);
    if (idx >= 0) {
        wellness_state_t snapshot = {};
        portENTER_CRITICAL(&s_wellness_lock);
        snapshot = s_wellness_states[idx];
        portEXIT_CRITICAL(&s_wellness_lock);
        emit_guidance_context(&snapshot, 0, "fresh");
    } else {
        ESP_LOGW(TAG, "wellness state table full; dropped subject=%u seq=%" PRIu32, packet->subject_id, packet->seq);
    }
}

static void log_psychometer_packet(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (len != (int)sizeof(psychometer_packet_t)) {
        return;
    }
    psychometer_packet_t packet;
    memcpy(&packet, data, sizeof(packet));
    if (packet.magic != PSYCH_MAGIC || packet.version != 1 || packet.size != sizeof(packet)) {
        return;
    }
    const uint8_t *src = packet.mac;
    if (src[0] == 0 && src[1] == 0 && src[2] == 0 && src[3] == 0 && src[4] == 0 && src[5] == 0 && info != NULL) {
        src = info->src_addr;
    }
    ESP_LOGI(TAG,
             "psych packet mac=%02x:%02x:%02x:%02x:%02x:%02x seq=%" PRIu32
             " arousal=%d valence=%d focus=%s enc=%d press=%u age=%" PRIu32,
             src[0],
             src[1],
             src[2],
             src[3],
             src[4],
             src[5],
             packet.seq,
             packet.arousal,
             packet.valence,
             packet.focus_axis == 0 ? "arousal" : "valence",
             packet.encoder_steps,
             packet.button_presses,
             packet.uptime_ms);
}

static void log_family_wellness_packet(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (len != (int)sizeof(family_wellness_packet_t)) {
        return;
    }
    family_wellness_packet_t packet;
    memcpy(&packet, data, sizeof(packet));
    if (packet.magic != WELLNESS_MAGIC || packet.version != 1 || packet.size != sizeof(packet)) {
        return;
    }
    const uint8_t *src = packet.source_mac;
    if (src[0] == 0 && src[1] == 0 && src[2] == 0 && src[3] == 0 && src[4] == 0 && src[5] == 0 && info != NULL) {
        src = info->src_addr;
    }
    ESP_LOGI(TAG,
             "wellness packet mac=%02x:%02x:%02x:%02x:%02x:%02x subject=%u seq=%" PRIu32
             " flags=0x%02x stress=%u trend30m=%d hrv=%u hr=%u spo2=%u sleep=%u light=%u deep=%u rem=%u awake=%u batt=%u age=%" PRIu32,
             src[0],
             src[1],
             src[2],
             src[3],
             src[4],
             src[5],
             packet.subject_id,
             packet.seq,
             packet.flags,
             packet.stress,
             packet.stress_trend_30m,
             packet.hrv_ms,
             packet.heart_rate_bpm,
             packet.spo2_percent,
             packet.sleep_total_min,
             packet.sleep_light_min,
             packet.sleep_deep_min,
             packet.sleep_rem_min,
             packet.sleep_awake_min,
             packet.battery_percent,
             packet.uptime_ms);
    update_wellness_state(&packet, src);
}

static void log_family_identity_packet(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (len != (int)sizeof(family_identity_packet_t)) {
        return;
    }
    family_identity_packet_t packet;
    memcpy(&packet, data, sizeof(packet));
    if (packet.magic != IDENTITY_MAGIC || packet.version != 1 || packet.size != sizeof(packet)) {
        return;
    }
    packet.subject_name[sizeof(packet.subject_name) - 1] = '\0';
    sanitize_identity_name(packet.subject_name, sizeof(packet.subject_name), "Unknown");

    const uint8_t *src = packet.source_mac;
    if (src[0] == 0 && src[1] == 0 && src[2] == 0 && src[3] == 0 && src[4] == 0 && src[5] == 0 && info != NULL) {
        src = info->src_addr;
    }
    update_subject_name(packet.subject_id, packet.subject_name);
    ESP_LOGI(TAG,
             "identity packet mac=%02x:%02x:%02x:%02x:%02x:%02x subject=%u name=%s seq=%" PRIu32 " age=%" PRIu32,
             src[0],
             src[1],
             src[2],
             src[3],
             src[4],
             src[5],
             packet.subject_id,
             subject_name(packet.subject_id),
             packet.seq,
             packet.uptime_ms);
}

static void recv_cb(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (data == NULL || len < (int)sizeof(uint32_t)) {
        return;
    }
    uint32_t magic = 0;
    memcpy(&magic, data, sizeof(magic));
    if (magic == PSYCH_MAGIC) {
        log_psychometer_packet(info, data, len);
    } else if (magic == IDENTITY_MAGIC) {
        log_family_identity_packet(info, data, len);
    } else if (magic == WELLNESS_MAGIC) {
        log_family_wellness_packet(info, data, len);
    }
}

static void init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

static void init_espnow_rx(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE));

    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(recv_cb));
}

static void wellness_monitor_task(void *arg)
{
    (void)arg;
    for (;;) {
        wellness_state_t camille = {};
        bool have_camille = false;
        const uint32_t t_ms = now_ms();
        portENTER_CRITICAL(&s_wellness_lock);
        const int idx = find_wellness_state(WELLNESS_SUBJECT_CAMILLE);
        if (idx >= 0) {
            camille = s_wellness_states[idx];
            have_camille = true;
        }
        portEXIT_CRITICAL(&s_wellness_lock);

        if (!have_camille) {
            ESP_LOGI(TAG, "paired_state subject=%s status=waiting_for_wellness",
                     subject_name(WELLNESS_SUBJECT_CAMILLE));
        } else {
            const uint32_t age_ms = t_ms - camille.last_rx_ms;
            const char *status = age_ms > WELLNESS_STATE_STALE_MS ? "stale" : "fresh";
            ESP_LOGI(TAG,
                     "paired_state subject=%s status=%s age_ms=%" PRIu32 " seq=%" PRIu32
                     " source=%02x:%02x:%02x:%02x:%02x:%02x",
                     subject_name(WELLNESS_SUBJECT_CAMILLE),
                     status,
                     age_ms,
                     camille.last_seq,
                     camille.source_mac[0],
                     camille.source_mac[1],
                     camille.source_mac[2],
                     camille.source_mac[3],
                     camille.source_mac[4],
                     camille.source_mac[5]);
            emit_guidance_context(&camille, age_ms, status);
        }
        vTaskDelay(pdMS_TO_TICKS(WELLNESS_MONITOR_PERIOD_MS));
    }
}

void app_main(void)
{
    uint8_t mac[6] = {};
    init_nvs();
    load_identity_from_nvs();
    esp_efuse_mac_get_default(mac);
    ESP_LOGI(TAG,
             "Astrolabe ESP-NOW psychometer/wellness receiver name=%s mac=%02x:%02x:%02x:%02x:%02x:%02x channel=%d",
             s_device_name,
             mac[0],
             mac[1],
             mac[2],
             mac[3],
             mac[4],
             mac[5],
             ESPNOW_CHANNEL);
    init_espnow_rx();
    xTaskCreate(wellness_monitor_task, "wellness_monitor", 4096, NULL, 4, NULL);
    ESP_LOGI(TAG, "ready");
}
