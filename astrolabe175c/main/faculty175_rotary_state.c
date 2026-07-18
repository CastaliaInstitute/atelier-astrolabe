#include "faculty175_rotary_state.h"

#include <string.h>

#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"

#include "faculty175_log.h"
#include "faculty175_util.h"

static const char *TAG = "faculty175_rotary_state";

#define FACULTY175_ROTARY_MAGIC 0x5253u
#define FACULTY175_ROTARY_VERSION 1u
#define FACULTY175_ROTARY_VERSION_STYLE 2u
#define FACULTY175_ROTARY_PAIR_HYSTERESIS_DBM 4
#define FACULTY175_ROTARY_STYLE_SKIN_TONE_COUNT 6
#define FACULTY175_ROTARY_STYLE_HAIR_COLOR_COUNT 6
#define FACULTY175_ROTARY_STYLE_EYE_COLOR_COUNT 6
#define FACULTY175_ROTARY_STYLE_FACIAL_HAIR_COUNT 4
#define FACULTY175_ROTARY_STYLE_GLASSES_COUNT 2

typedef struct __attribute__((packed)) {
    uint16_t magic;
    uint8_t version;
    uint8_t state;
} faculty175_rotary_state_packet_t;

typedef struct {
    bool active;
    uint8_t mac[6];
    uint8_t state;
    int8_t rssi_dbm;
    bool has_style;
    uint8_t skin_tone;
    uint8_t hair_color;
    uint8_t eye_color;
    uint8_t facial_hair;
    uint8_t glasses;
    uint32_t last_seen_ms;
} faculty175_rotary_peer_state_t;

static const char *k_state_labels[FACULTY175_ROTARY_STATE_KIND_COUNT] = {
    "neutral",
    "calm",
    "energized",
    "focused",
    "hopeful",
    "frustrated",
    "anxious",
    "heavy",
};

static const char *k_state_emojis[FACULTY175_ROTARY_STATE_KIND_COUNT] = {
    ":-|",
    ":-)",
    ":-D",
    ":-!",
    ":-)",
    ">:(",
    ":-/",
    ":'(",
};

static faculty175_rotary_peer_state_t s_peer_states[FACULTY175_ROTARY_STATE_MAX_PEERS];
static bool s_rotary_state_ready;
static int8_t s_paired_peer_idx = -1;
static portMUX_TYPE s_rotary_state_lock = portMUX_INITIALIZER_UNLOCKED;

static int peer_index_from_mac(const uint8_t *mac)
{
    for (int i = 0; i < FACULTY175_ROTARY_STATE_MAX_PEERS; ++i) {
        if (s_peer_states[i].active && memcmp(s_peer_states[i].mac, mac, sizeof(s_peer_states[i].mac)) == 0) {
            return i;
        }
    }
    return -1;
}

static bool peer_index_is_valid(int idx)
{
    return idx >= 0 && idx < FACULTY175_ROTARY_STATE_MAX_PEERS;
}

static uint8_t normalize_u8(uint8_t value, uint8_t max, uint8_t fallback)
{
    return value < max ? value : fallback;
}

static bool parse_style_fields(const uint8_t *data,
                              int len,
                              bool *has_style,
                              uint8_t *skin_tone,
                              uint8_t *hair_color,
                              uint8_t *eye_color,
                              uint8_t *facial_hair,
                              uint8_t *glasses)
{
    if (data == NULL || has_style == NULL || skin_tone == NULL || hair_color == NULL ||
        eye_color == NULL || facial_hair == NULL || glasses == NULL) {
        return false;
    }
    if (len <= (int)sizeof(faculty175_rotary_state_packet_t)) {
        *has_style = false;
        *skin_tone = 0;
        *hair_color = 0;
        *eye_color = 0;
        *facial_hair = 0;
        *glasses = 0;
        return false;
    }

    const size_t base_size = sizeof(faculty175_rotary_state_packet_t);
    const uint8_t *style = data + base_size;
    const int style_len = len - (int)base_size;
    if (style_len <= 0) {
        *has_style = false;
        *skin_tone = 0;
        *hair_color = 0;
        *eye_color = 0;
        *facial_hair = 0;
        *glasses = 0;
        return false;
    }

    *has_style = true;
    *skin_tone = normalize_u8(style[0], FACULTY175_ROTARY_STYLE_SKIN_TONE_COUNT, 0);
    *hair_color = style_len >= 2 ? normalize_u8(style[1], FACULTY175_ROTARY_STYLE_HAIR_COLOR_COUNT, 0) : 0;
    *eye_color = style_len >= 3 ? normalize_u8(style[2], FACULTY175_ROTARY_STYLE_EYE_COLOR_COUNT, 0) : 0;
    *facial_hair = style_len >= 4 ? normalize_u8(style[3], FACULTY175_ROTARY_STYLE_FACIAL_HAIR_COUNT, 0) : 0;
    *glasses = style_len >= 5 ? normalize_u8(style[4], FACULTY175_ROTARY_STYLE_GLASSES_COUNT, 0) : 0;
    return true;
}

static int peer_slot_new(void)
{
    int oldest_idx = 0;
    uint32_t oldest_seen_ms = UINT32_MAX;
    for (int i = 0; i < FACULTY175_ROTARY_STATE_MAX_PEERS; ++i) {
        if (!s_peer_states[i].active) {
            return i;
        }
        if (s_peer_states[i].last_seen_ms < oldest_seen_ms) {
            oldest_idx = i;
            oldest_seen_ms = s_peer_states[i].last_seen_ms;
        }
    }
    return oldest_idx;
}

static bool peer_fresh(const faculty175_rotary_peer_state_t *peer, uint32_t now_ms)
{
    if (peer == NULL || !peer->active) {
        return false;
    }
    return (now_ms - peer->last_seen_ms) <= FACULTY175_ROTARY_STATE_STALE_MS;
}

static int peer_best_index(uint32_t now_ms)
{
    int best_idx = -1;
    int8_t best_rssi = -128;
    uint32_t best_age_ms = UINT32_MAX;

    for (int i = 0; i < FACULTY175_ROTARY_STATE_MAX_PEERS; ++i) {
        if (!peer_fresh(&s_peer_states[i], now_ms)) {
            continue;
        }
        const uint32_t age_ms = now_ms - s_peer_states[i].last_seen_ms;
        if (s_peer_states[i].rssi_dbm > best_rssi ||
            (s_peer_states[i].rssi_dbm == best_rssi && age_ms < best_age_ms)) {
            best_idx = i;
            best_rssi = s_peer_states[i].rssi_dbm;
            best_age_ms = age_ms;
        }
    }
    return best_idx;
}

static void peer_prune_stale(uint32_t now_ms)
{
    for (int i = 0; i < FACULTY175_ROTARY_STATE_MAX_PEERS; ++i) {
        if (!peer_fresh(&s_peer_states[i], now_ms)) {
            s_peer_states[i].active = false;
        }
    }
    if (!peer_index_is_valid(s_paired_peer_idx) || !peer_fresh(&s_peer_states[s_paired_peer_idx], now_ms)) {
        s_paired_peer_idx = -1;
    }
}

static void peer_set_pair(uint32_t now_ms, int source_idx)
{
    if (!peer_index_is_valid(source_idx)) {
        return;
    }
    if (!peer_fresh(&s_peer_states[source_idx], now_ms)) {
        return;
    }

    if (!peer_index_is_valid(s_paired_peer_idx)) {
        s_paired_peer_idx = (int8_t)source_idx;
        return;
    }
    if (!peer_fresh(&s_peer_states[s_paired_peer_idx], now_ms)) {
        s_paired_peer_idx = (int8_t)source_idx;
        return;
    }

    if (source_idx == s_paired_peer_idx) {
        return;
    }

    if (s_peer_states[source_idx].rssi_dbm >=
        s_peer_states[s_paired_peer_idx].rssi_dbm + FACULTY175_ROTARY_PAIR_HYSTERESIS_DBM) {
        s_paired_peer_idx = (int8_t)source_idx;
    }
}

static void peer_mark(uint8_t idx,
                      const uint8_t *mac,
                      uint8_t state,
                      int8_t rssi_dbm,
                      bool has_style,
                      uint8_t skin_tone,
                      uint8_t hair_color,
                      uint8_t eye_color,
                      uint8_t facial_hair,
                      uint8_t glasses,
                      uint32_t now_ms)
{
    if (idx < 0 || idx >= FACULTY175_ROTARY_STATE_MAX_PEERS || mac == NULL) {
        return;
    }
    s_peer_states[idx].active = true;
    memcpy(s_peer_states[idx].mac, mac, sizeof(s_peer_states[idx].mac));
    s_peer_states[idx].state = state;
    s_peer_states[idx].has_style = has_style;
    s_peer_states[idx].skin_tone = skin_tone;
    s_peer_states[idx].hair_color = hair_color;
    s_peer_states[idx].eye_color = eye_color;
    s_peer_states[idx].facial_hair = facial_hair;
    s_peer_states[idx].glasses = glasses;
    s_peer_states[idx].rssi_dbm = rssi_dbm;
    s_peer_states[idx].last_seen_ms = now_ms;
}

static void peer_touch_with_style(const uint8_t *mac,
                                 uint8_t state,
                                 int8_t rssi_dbm,
                                 bool has_style,
                                 uint8_t skin_tone,
                                 uint8_t hair_color,
                                 uint8_t eye_color,
                                 uint8_t facial_hair,
                                 uint8_t glasses,
                                 uint32_t now_ms)
{
    int idx = peer_index_from_mac(mac);
    if (idx < 0) {
        idx = peer_slot_new();
        if (idx < 0) {
            FACULTY175_LOG_STAGE_W(TAG,
                                   "rotary",
                                   "peer table full; ignoring new sender %02x:%02x:%02x:%02x:%02x:%02x",
                                   mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            return;
        }
    }
    peer_mark(idx, mac, state, rssi_dbm, has_style, skin_tone, hair_color, eye_color, facial_hair, glasses, now_ms);
    if (s_paired_peer_idx < 0) {
        s_paired_peer_idx = (int8_t)idx;
        return;
    }
    peer_set_pair(now_ms, idx);
}

static void peer_touch(const uint8_t *mac, uint8_t state, int8_t rssi_dbm, uint32_t now_ms)
{
    peer_touch_with_style(mac, state, rssi_dbm, false, 0, 0, 0, 0, 0, now_ms);
}

static void peer_label(const uint8_t *mac, char *out, size_t cap)
{
    if (out == NULL || cap == 0 || mac == NULL) {
        return;
    }
    snprintf(out,
             cap,
             "%02X%02X:%02X%02X:%02X%02X",
             mac[0],
             mac[1],
             mac[2],
             mac[3],
             mac[4],
             mac[5]);
}

static void process_packet(const uint8_t *mac, const uint8_t *data, int len, int8_t rssi_dbm)
{
    if (mac == NULL || data == NULL || len < (int)sizeof(faculty175_rotary_state_packet_t)) {
        return;
    }
    bool has_style = false;
    uint8_t skin_tone = 0;
    uint8_t hair_color = 0;
    uint8_t eye_color = 0;
    uint8_t facial_hair = 0;
    uint8_t glasses = 0;
    faculty175_rotary_state_packet_t packet = {};
    memcpy(&packet, data, sizeof(packet));
    if (packet.magic != FACULTY175_ROTARY_MAGIC ||
        (packet.version != FACULTY175_ROTARY_VERSION && packet.version != FACULTY175_ROTARY_VERSION_STYLE) ||
        packet.state >= FACULTY175_ROTARY_STATE_KIND_COUNT) {
        return;
    }
    const uint32_t now_ms = faculty175_log_ms();
    taskENTER_CRITICAL(&s_rotary_state_lock);
    if (packet.version == FACULTY175_ROTARY_VERSION_STYLE) {
        (void)parse_style_fields(data, len, &has_style, &skin_tone, &hair_color, &eye_color, &facial_hair, &glasses);
        peer_touch_with_style(mac,
                             packet.state,
                             rssi_dbm,
                             has_style,
                             skin_tone,
                             hair_color,
                             eye_color,
                             facial_hair,
                             glasses,
                             now_ms);
    } else {
        peer_touch(mac, packet.state, rssi_dbm, now_ms);
    }
    taskEXIT_CRITICAL(&s_rotary_state_lock);
    FACULTY175_LOG_STAGE(TAG,
                         "rotary",
                         "state=%u from %02x:%02x:%02x:%02x:%02x:%02x rssi=%d skin=%u hair=%u eyes=%u beard=%u glasses=%u",
                         (unsigned)packet.state,
                         mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                         (int)rssi_dbm,
                         (unsigned)skin_tone,
                         (unsigned)hair_color,
                         (unsigned)eye_color,
                         (unsigned)facial_hair,
                         (unsigned)glasses);
}

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
static void rotary_state_recv_cb(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (info == NULL || info->src_addr == NULL) {
        return;
    }
    const int8_t rssi_dbm = info->rx_ctrl != NULL ? info->rx_ctrl->rssi : -128;
    process_packet(info->src_addr, data, len, rssi_dbm);
}
#else
static void rotary_state_recv_cb(const uint8_t *mac_addr, const uint8_t *data, int len)
{
    process_packet(mac_addr, data, len, -128);
}
#endif

const char *faculty175_rotary_state_label(uint8_t state)
{
    if (state >= FACULTY175_ROTARY_STATE_KIND_COUNT) {
        return "unknown";
    }
    return k_state_labels[state];
}

const char *faculty175_rotary_state_emoji(uint8_t state)
{
    if (state >= FACULTY175_ROTARY_STATE_KIND_COUNT) {
        return ":-|";
    }
    return k_state_emojis[state];
}

static bool select_best(faculty175_rotary_state_t *out)
{
    if (out == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    const uint32_t now_ms = faculty175_log_ms();
    peer_prune_stale(now_ms);
    if (!peer_index_is_valid(s_paired_peer_idx) || !peer_fresh(&s_peer_states[s_paired_peer_idx], now_ms)) {
        s_paired_peer_idx = (int8_t)peer_best_index(now_ms);
    }
    if (!peer_index_is_valid(s_paired_peer_idx) || !peer_fresh(&s_peer_states[s_paired_peer_idx], now_ms)) {
        return false;
    }
    const uint32_t age_ms = now_ms - s_peer_states[s_paired_peer_idx].last_seen_ms;

    out->valid = true;
    out->state = s_peer_states[s_paired_peer_idx].state;
    out->has_style = s_peer_states[s_paired_peer_idx].has_style;
    out->skin_tone = s_peer_states[s_paired_peer_idx].skin_tone;
    out->hair_color = s_peer_states[s_paired_peer_idx].hair_color;
    out->eye_color = s_peer_states[s_paired_peer_idx].eye_color;
    out->facial_hair = s_peer_states[s_paired_peer_idx].facial_hair;
    out->glasses = s_peer_states[s_paired_peer_idx].glasses;
    out->rssi_dbm = s_peer_states[s_paired_peer_idx].rssi_dbm;
    out->age_ms = age_ms;
    peer_label(s_peer_states[s_paired_peer_idx].mac, out->source, sizeof(out->source));
    return true;
}

bool faculty175_rotary_state_get(faculty175_rotary_state_t *out)
{
    if (out == NULL) {
        return false;
    }
    bool found;
    taskENTER_CRITICAL(&s_rotary_state_lock);
    found = select_best(out);
    taskEXIT_CRITICAL(&s_rotary_state_lock);
    return found;
}

esp_err_t faculty175_rotary_state_init(void)
{
    if (s_rotary_state_ready) {
        return ESP_OK;
    }
    memset(s_peer_states, 0, sizeof(s_peer_states));
    s_paired_peer_idx = -1;
    const esp_err_t err = esp_now_init();
    if (err != ESP_OK && err != ESP_ERR_ESPNOW_EXIST) {
        return err;
    }
    const esp_err_t cb_err = esp_now_register_recv_cb(rotary_state_recv_cb);
    if (cb_err != ESP_OK) {
        return cb_err;
    }
    s_rotary_state_ready = true;
    FACULTY175_LOG_STAGE(TAG, "rotary", "receiver ready with up to %d peer slots", FACULTY175_ROTARY_STATE_MAX_PEERS);
    return ESP_OK;
}
