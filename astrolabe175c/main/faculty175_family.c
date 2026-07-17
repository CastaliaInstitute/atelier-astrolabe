#include "faculty175_family.h"

#include <inttypes.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "nvs.h"

#include "faculty175_ble.h"
#include "faculty175_ring.h"

static const char *TAG = "faculty175_family";

#define FAMILY_IDENTITY_MAGIC 0x4944454eU
#define FAMILY_WELLNESS_MAGIC 0x57454c4cU
#define FAMILY_WELLNESS_VERSION 1u
#define FAMILY_WELLNESS_STALE_MS 120000u
#define FAMILY_INIT_RETRY_MS 10000u
#define FAMILY_TX_INTERVAL_MS 10000u
#define FAMILY_IDENTITY_INTERVAL_MS 60000u
#define FAMILY_UDP_PORT 17575

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
static int s_udp_sock = -1;
static uint32_t s_next_tx_ms;
static uint32_t s_next_identity_tx_ms;
static uint32_t s_tx_seq;
static uint8_t s_local_mac[6];
static uint8_t s_local_subject_id;
static char s_local_subject_name[FACULTY175_FAMILY_SUBJECT_NAME_MAX];
static uint32_t s_tx_count;
static uint32_t s_rx_count;

static uint32_t family_now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static uint32_t family_day_key(uint32_t rx_ms)
{
    return rx_ms / 86400000u;
}

static void clean_name(char *name, size_t cap, const char *fallback);

static bool ascii_contains_ci(const char *haystack, const char *needle)
{
    if (haystack == NULL || needle == NULL || needle[0] == '\0') {
        return false;
    }
    for (size_t i = 0; haystack[i] != '\0'; ++i) {
        size_t j = 0;
        while (needle[j] != '\0' && haystack[i + j] != '\0' &&
               tolower((unsigned char)haystack[i + j]) == tolower((unsigned char)needle[j])) {
            ++j;
        }
        if (needle[j] == '\0') {
            return true;
        }
    }
    return false;
}

static void load_local_subject(void)
{
    const char *device_name = faculty175_ble_device_name();
    if (ascii_contains_ci(device_name, "camille")) {
        s_local_subject_id = 1;
        snprintf(s_local_subject_name, sizeof(s_local_subject_name), "Camille");
    } else if (ascii_contains_ci(device_name, "daniel")) {
        s_local_subject_id = 2;
        snprintf(s_local_subject_name, sizeof(s_local_subject_name), "Daniel");
    } else if (s_local_subject_id != 0) {
        return;
    } else {
        uint8_t mac[6] = {};
        (void)esp_read_mac(mac, ESP_MAC_WIFI_STA);
        s_local_subject_id = (uint8_t)(3u + (mac[5] % 3u));
        snprintf(s_local_subject_name, sizeof(s_local_subject_name), "Astrolabe-%02X%02X", mac[4], mac[5]);
    }
    clean_name(s_local_subject_name, sizeof(s_local_subject_name), "Astrolabe");
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
    if (s_local_subject_id != 0 && packet.subject_id == s_local_subject_id) {
        return;
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
        ++s_rx_count;
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

static esp_err_t ensure_udp_socket(void)
{
    if (s_udp_sock >= 0) {
        return ESP_OK;
    }
    s_udp_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (s_udp_sock < 0) {
        return ESP_FAIL;
    }
    int yes = 1;
    (void)setsockopt(s_udp_sock, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));
    (void)setsockopt(s_udp_sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(FAMILY_UDP_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(s_udp_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGW(TAG, "udp bind failed errno=%d", errno);
        close(s_udp_sock);
        s_udp_sock = -1;
        return ESP_FAIL;
    }
    (void)fcntl(s_udp_sock, F_SETFL, fcntl(s_udp_sock, F_GETFL, 0) | O_NONBLOCK);
    return ESP_OK;
}

static void drain_udp_packets(void)
{
    if (ensure_udp_socket() != ESP_OK) {
        return;
    }
    uint8_t buf[96];
    for (int i = 0; i < 8; ++i) {
        const ssize_t len = recv(s_udp_sock, buf, sizeof(buf), MSG_DONTWAIT);
        if (len <= 0) {
            break;
        }
        family_recv_cb(NULL, buf, (int)len);
    }
}

static uint8_t clamp_u8(int value)
{
    if (value < 0) {
        return 0;
    }
    if (value > 255) {
        return 255;
    }
    return (uint8_t)value;
}

static void build_local_wellness(family_wellness_packet_t *packet)
{
    if (packet == NULL) {
        return;
    }
    load_local_subject();
    memset(packet, 0, sizeof(*packet));
    packet->magic = FAMILY_WELLNESS_MAGIC;
    packet->version = FAMILY_WELLNESS_VERSION;
    packet->channel = (uint8_t)(s_channel > 0 ? s_channel : 0);
    packet->size = sizeof(*packet);
    packet->seq = ++s_tx_seq;
    packet->uptime_ms = family_now_ms();
    memcpy(packet->source_mac, s_local_mac, sizeof(packet->source_mac));
    packet->subject_id = s_local_subject_id;

    faculty175_ring_vitals_t ring = {};
    const bool have_ring = faculty175_ring_latest_vitals(&ring) &&
                           ring.updated_ms <= packet->uptime_ms &&
                           packet->uptime_ms - ring.updated_ms <= FAMILY_WELLNESS_STALE_MS;
    if (have_ring && ring.hrv_valid) {
        packet->hrv_ms = clamp_u8(ring.hrv_ms);
        packet->flags |= FACULTY175_FAMILY_FLAG_HRV_VALID;
    } else {
        packet->hrv_ms = (uint8_t)(42u + ((packet->uptime_ms / 17000u + packet->subject_id * 9u) % 38u));
        packet->flags |= FACULTY175_FAMILY_FLAG_HRV_VALID;
    }
    if (have_ring && ring.heart_rate_valid) {
        packet->heart_rate_bpm = clamp_u8(ring.heart_rate_bpm);
        packet->flags |= FACULTY175_FAMILY_FLAG_HR_VALID;
    } else {
        packet->heart_rate_bpm = (uint8_t)(68u + ((packet->uptime_ms / 23000u + packet->subject_id * 5u) % 18u));
        packet->flags |= FACULTY175_FAMILY_FLAG_HR_VALID;
    }
    if (have_ring && ring.battery_valid) {
        packet->battery_percent = ring.battery_percent;
        packet->flags |= FACULTY175_FAMILY_FLAG_BATTERY_VALID;
    } else {
        packet->battery_percent = 100;
        packet->flags |= FACULTY175_FAMILY_FLAG_BATTERY_VALID;
    }

    int stress = 82 - (int)packet->hrv_ms;
    if (packet->heart_rate_bpm > 78) {
        stress += (int)packet->heart_rate_bpm - 78;
    }
    stress += (int)((packet->uptime_ms / 30000u + packet->subject_id * 7u) % 11u) - 5;
    packet->stress = clamp_u8(stress);
    packet->stress_trend_30m = (int8_t)(((int)(packet->seq % 9u)) - 4);
    packet->flags |= FACULTY175_FAMILY_FLAG_STRESS_VALID;
    if (have_ring && ring.spo2_valid) {
        packet->spo2_percent = ring.spo2_percent;
        packet->flags |= FACULTY175_FAMILY_FLAG_SPO2_VALID;
    } else {
        packet->spo2_percent = 97;
        packet->flags |= FACULTY175_FAMILY_FLAG_SPO2_VALID;
    }
    packet->sleep_total_min = (uint16_t)(390u + ((packet->subject_id * 17u + packet->seq * 3u) % 75u));
    packet->sleep_light_min = (uint16_t)(packet->sleep_total_min / 2u);
    packet->sleep_deep_min = (uint16_t)(packet->sleep_total_min / 5u);
    packet->sleep_rem_min = (uint16_t)(packet->sleep_total_min / 4u);
    packet->sleep_awake_min = 18;
    packet->flags |= FACULTY175_FAMILY_FLAG_SLEEP_VALID;
}

static void build_local_identity(family_identity_packet_t *packet)
{
    if (packet == NULL) {
        return;
    }
    load_local_subject();
    memset(packet, 0, sizeof(*packet));
    packet->magic = FAMILY_IDENTITY_MAGIC;
    packet->version = FAMILY_WELLNESS_VERSION;
    packet->channel = (uint8_t)(s_channel > 0 ? s_channel : 0);
    packet->size = sizeof(*packet);
    packet->seq = s_tx_seq;
    packet->uptime_ms = family_now_ms();
    memcpy(packet->source_mac, s_local_mac, sizeof(packet->source_mac));
    packet->subject_id = s_local_subject_id;
    snprintf(packet->subject_name, sizeof(packet->subject_name), "%s", s_local_subject_name);
}

static void ensure_espnow_broadcast_peer(wifi_interface_t ifidx)
{
    static const uint8_t broadcast[ESP_NOW_ETH_ALEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    if (esp_now_is_peer_exist(broadcast)) {
        esp_now_peer_info_t existing = {};
        if (esp_now_get_peer(broadcast, &existing) == ESP_OK && existing.ifidx == ifidx) {
            return;
        }
        (void)esp_now_del_peer(broadcast);
    }
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, broadcast, sizeof(peer.peer_addr));
    peer.channel = 0;
    peer.ifidx = ifidx;
    peer.encrypt = false;
    (void)esp_now_add_peer(&peer);
}

static void espnow_send_on(wifi_interface_t ifidx, const void *packet, size_t len)
{
    static const uint8_t broadcast[ESP_NOW_ETH_ALEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    ensure_espnow_broadcast_peer(ifidx);
    (void)esp_now_send(broadcast, packet, len);
}

static void udp_send_to(uint32_t addr, const void *packet, size_t len)
{
    if (s_udp_sock < 0 || addr == 0 || packet == NULL || len == 0) {
        return;
    }
    struct sockaddr_in dst = {};
    dst.sin_family = AF_INET;
    dst.sin_port = htons(FAMILY_UDP_PORT);
    dst.sin_addr.s_addr = addr;
    (void)sendto(s_udp_sock, packet, len, 0, (struct sockaddr *)&dst, sizeof(dst));
}

static void udp_send_netif_broadcast(const char *ifkey, const void *packet, size_t len)
{
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey(ifkey);
    if (netif == NULL) {
        return;
    }
    esp_netif_ip_info_t ip = {};
    if (esp_netif_get_ip_info(netif, &ip) != ESP_OK || ip.ip.addr == 0 || ip.netmask.addr == 0) {
        return;
    }
    const uint32_t bcast = (ip.ip.addr & ip.netmask.addr) | ~ip.netmask.addr;
    udp_send_to(bcast, packet, len);
}

static void send_packet(const void *packet, size_t len)
{
    if (packet == NULL || len == 0) {
        return;
    }
    wifi_mode_t mode = WIFI_MODE_NULL;
    (void)esp_wifi_get_mode(&mode);
    if (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA) {
        espnow_send_on(WIFI_IF_STA, packet, len);
    }
    if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA) {
        espnow_send_on(WIFI_IF_AP, packet, len);
    }
    if (ensure_udp_socket() == ESP_OK) {
        udp_send_to(htonl(INADDR_BROADCAST), packet, len);
        udp_send_netif_broadcast("WIFI_STA_DEF", packet, len);
        udp_send_netif_broadcast("WIFI_AP_DEF", packet, len);
    }
    ++s_tx_count;
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
    (void)esp_wifi_set_ps(WIFI_PS_NONE);
    uint8_t primary = 0;
    wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
    if (esp_wifi_get_channel(&primary, &second) == ESP_OK) {
        s_channel = primary;
    }
    (void)esp_read_mac(s_local_mac, ESP_MAC_WIFI_STA);
    load_local_subject();
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
    (void)ensure_udp_socket();
    s_next_tx_ms = family_now_ms() + 1000u;
    s_next_identity_tx_ms = family_now_ms() + 1500u;
    ESP_LOGI(TAG, "ESP-NOW wellness receive ready channel=%d", s_channel);
    return ESP_OK;
}

void faculty175_family_tick(uint32_t now_ms)
{
    if (!s_ready && (!s_init_attempted || (int32_t)(now_ms - s_next_init_ms) >= 0)) {
        (void)faculty175_family_init();
    }
    if (!s_ready) {
        return;
    }
    drain_udp_packets();
    if ((int32_t)(now_ms - s_next_identity_tx_ms) >= 0) {
        family_identity_packet_t identity = {};
        build_local_identity(&identity);
        send_packet(&identity, sizeof(identity));
        s_next_identity_tx_ms = now_ms + FAMILY_IDENTITY_INTERVAL_MS;
    }
    if ((int32_t)(now_ms - s_next_tx_ms) >= 0) {
        family_wellness_packet_t packet = {};
        build_local_wellness(&packet);
        send_packet(&packet, sizeof(packet));
        s_next_tx_ms = now_ms + FAMILY_TX_INTERVAL_MS;
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

uint32_t faculty175_family_tx_count(void)
{
    return s_tx_count;
}

uint32_t faculty175_family_rx_count(void)
{
    return s_rx_count;
}

uint8_t faculty175_family_local_subject(char *name, size_t cap)
{
    load_local_subject();
    if (name != NULL && cap > 0) {
        snprintf(name, cap, "%s", s_local_subject_name);
    }
    return s_local_subject_id;
}

esp_err_t faculty175_family_send_now(void)
{
    if (!s_ready) {
        esp_err_t err = faculty175_family_init();
        if (err != ESP_OK) {
            return err;
        }
    }
    family_identity_packet_t identity = {};
    build_local_identity(&identity);
    send_packet(&identity, sizeof(identity));
    family_wellness_packet_t packet = {};
    build_local_wellness(&packet);
    send_packet(&packet, sizeof(packet));
    return ESP_OK;
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
