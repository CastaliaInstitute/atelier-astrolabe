#include "faculty175_ble.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "cJSON.h"
#include "astrolabe_time.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

#include "faculty175_log.h"
#include "faculty175_device_settings.h"
#include "faculty175_motion.h"
#include "faculty175_ring.h"
#include "faculty175_wifi_settings.h"

void ble_store_config_init(void);

static const char *TAG = "faculty175_ble";
static const char *BLE_NVS_NS = "ble";
static const char *BLE_NVS_ENABLED = "enabled";
static const char *BLE_NVS_RING_ID = "ring_id";
static const char *BLE_NVS_IDENTITY_NS = "identity";
static const char *BLE_NVS_DEVICE_NAME = "device_name";
static const char *BLE_NVS_NAME = "name";
static const char *BLE_DEVICE_NAME_FALLBACK = "Astrolabe Faculty";
enum { BLE_APPEARANCE_GENERIC_TAG = 0x0200 };
enum {
    BLE_ASTROLABE_MFG_COMPANY_ID = 0xffff,
    BLE_ASTROLABE_MFG_MAGIC = 0xa7,
    BLE_ASTROLABE_MFG_VERSION = 2,
    BLE_ASTROLABE_MFG_FLAG_IMU_VALID = 0x01,
    BLE_ASTROLABE_OBS_TYPE_ASTROLABE = 0x01,
    BLE_ASTROLABE_OBS_TYPE_RING = 0x02,
};

typedef struct __attribute__((packed)) {
    uint16_t addr_hash;
    int8_t rssi;
    uint8_t type;
} ble_astrolabe_obs_t;

typedef struct __attribute__((packed)) {
    uint16_t company_id;
    uint8_t magic;
    uint8_t version;
    int8_t pitch_deg;
    int8_t roll_deg;
    int8_t accel_x_q6;
    int8_t accel_y_q6;
    int8_t accel_z_q6;
    uint8_t flags;
    uint8_t seq;
    uint8_t obs_count;
    ble_astrolabe_obs_t obs[FACULTY175_BLE_OBS_MAX];
} ble_astrolabe_mfg_t;

static const ble_uuid128_t BLE_SETTINGS_SERVICE_UUID =
    BLE_UUID128_INIT(0x41, 0x73, 0x74, 0x72, 0x6f, 0x6c, 0x61, 0x62, 0x65, 0x00, 0x17, 0x50, 0x00, 0x00, 0x00, 0x01);
static const ble_uuid128_t BLE_ENABLED_CHAR_UUID =
    BLE_UUID128_INIT(0x41, 0x73, 0x74, 0x72, 0x6f, 0x6c, 0x61, 0x62, 0x65, 0x00, 0x17, 0x50, 0x00, 0x00, 0x00, 0x02);
static const ble_uuid128_t BLE_SETTINGS_JSON_CHAR_UUID =
    BLE_UUID128_INIT(0x41, 0x73, 0x74, 0x72, 0x6f, 0x6c, 0x61, 0x62, 0x65, 0x00, 0x17, 0x50, 0x00, 0x00, 0x00, 0x03);

static bool s_enabled = true;
static bool s_started;
static bool s_synced;
static bool s_advertising;
static bool s_scanning;
static uint8_t s_own_addr_type;
static char s_json_rx[768];
static size_t s_json_rx_len;
static bool s_json_rx_active;
static char s_device_name[FACULTY175_BLE_PEER_NAME_MAX] = "Astrolabe Faculty";
static faculty175_ble_peer_t s_peers[FACULTY175_BLE_PEER_MAX];
static portMUX_TYPE s_peer_lock = portMUX_INITIALIZER_UNLOCKED;
static faculty175_ble_ring_telem_t s_ring_telem[FACULTY175_BLE_RING_TELEM_MAX];
static portMUX_TYPE s_ring_telem_lock = portMUX_INITIALIZER_UNLOCKED;
static uint16_t s_paired_ring_id;
static bool s_paired_ring_id_set;
static int8_t s_last_ring_rssi;
static bool s_have_last_ring_rssi;
static uint32_t s_next_scan_ms;
static uint32_t s_serial_quiet_until_ms;
static uint8_t s_imu_adv_seq;

static int ble_gap_event(struct ble_gap_event *event, void *arg);
static esp_err_t ble_advertise(void);
static int ble_settings_enabled_access(uint16_t conn_handle,
                                       uint16_t attr_handle,
                                       struct ble_gatt_access_ctxt *ctxt,
                                       void *arg);
static int ble_settings_json_access(uint16_t conn_handle,
                                    uint16_t attr_handle,
                                    struct ble_gatt_access_ctxt *ctxt,
                                    void *arg);

static const struct ble_gatt_svc_def k_ble_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &BLE_SETTINGS_SERVICE_UUID.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                .uuid = &BLE_ENABLED_CHAR_UUID.u,
                .access_cb = ble_settings_enabled_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
            },
            {
                .uuid = &BLE_SETTINGS_JSON_CHAR_UUID.u,
                .access_cb = ble_settings_json_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
            },
            {0},
        },
    },
    {0},
};

static esp_err_t ble_nvs_get_enabled(bool *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t value = 1;
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(BLE_NVS_NS, NVS_READONLY, &nvs);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *out = true;
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_get_u8(nvs, BLE_NVS_ENABLED, &value);
    nvs_close(nvs);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK;
        value = 1;
    }
    *out = value != 0;
    return err;
}

static esp_err_t ble_nvs_get_ring_id(uint16_t *out, bool *set)
{
    if (out == NULL || set == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = 0;
    *set = false;
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(BLE_NVS_NS, NVS_READONLY, &nvs);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }
    uint16_t value = 0;
    err = nvs_get_u16(nvs, BLE_NVS_RING_ID, &value);
    nvs_close(nvs);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err == ESP_OK) {
        *out = value;
        *set = true;
    }
    return err;
}

static esp_err_t ble_nvs_set_ring_id(uint16_t id, bool set)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(BLE_NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    if (set) {
        err = nvs_set_u16(nvs, BLE_NVS_RING_ID, id);
    } else {
        err = nvs_erase_key(nvs, BLE_NVS_RING_ID);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            err = ESP_OK;
        }
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

static uint32_t ble_now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static void ble_defer_radar_scan(uint32_t delay_ms)
{
    const uint32_t now = ble_now_ms();
    const uint32_t until = now + delay_ms;
    s_serial_quiet_until_ms = until;
    if (!s_scanning && (s_next_scan_ms == 0 || (int32_t)(s_next_scan_ms - until) < 0)) {
        s_next_scan_ms = until;
    }
}

void faculty175_ble_serial_activity(void)
{
    if (s_scanning) {
        (void)ble_gap_disc_cancel();
        s_scanning = false;
        vTaskDelay(pdMS_TO_TICKS(120));
        if (s_started && s_synced && s_enabled && !s_advertising) {
            (void)ble_advertise();
        }
    }
    ble_defer_radar_scan(10000u);
}

static void ble_copy_text(char *dst, size_t dst_len, const uint8_t *src, size_t src_len)
{
    if (dst == NULL || dst_len == 0) {
        return;
    }
    if (src == NULL || src_len == 0) {
        dst[0] = '\0';
        return;
    }
    if (src_len >= dst_len) {
        src_len = dst_len - 1;
    }
    memcpy(dst, src, src_len);
    dst[src_len] = '\0';
}

static bool ble_name_mentions_astrolabe(const char *name)
{
    if (name == NULL) {
        return false;
    }
    const char *needle = "astrolabe";
    const size_t needle_len = strlen(needle);
    for (const char *p = name; *p != '\0'; ++p) {
        if (strncasecmp(p, needle, needle_len) == 0) {
            return true;
        }
    }
    return false;
}

static bool ble_name_mentions_ring(const char *name)
{
    if (name == NULL) {
        return false;
    }
    static const char *const needles[] = {
        "colmi",
        "r02",
        "r10",
        "ring",
    };
    for (size_t i = 0; i < sizeof(needles) / sizeof(needles[0]); ++i) {
        const char *needle = needles[i];
        const size_t needle_len = strlen(needle);
        for (const char *p = name; *p != '\0'; ++p) {
            if (strncasecmp(p, needle, needle_len) == 0) {
                return true;
            }
        }
    }
    return false;
}

static bool ble_fields_have_astrolabe_uuid(const struct ble_hs_adv_fields *fields)
{
    if (fields == NULL) {
        return false;
    }
    for (uint8_t i = 0; i < fields->num_uuids128; ++i) {
        if (ble_uuid_cmp(&fields->uuids128[i].u, &BLE_SETTINGS_SERVICE_UUID.u) == 0) {
            return true;
        }
    }
    return false;
}

static uint16_t ble_peer_bearing(const uint8_t addr[6], uint32_t seen_ms)
{
    uint32_t h = 2166136261u;
    for (int i = 0; i < 6; ++i) {
        h ^= addr[i];
        h *= 16777619u;
    }
    h ^= seen_ms / 30000u;
    return (uint16_t)(h % 360u);
}

static uint8_t ble_rssi_range_pct(int8_t rssi)
{
    if (rssi >= -45) {
        return 18;
    }
    if (rssi <= -95) {
        return 100;
    }
    return (uint8_t)(18 + ((-45 - rssi) * 82) / 50);
}

static uint8_t ble_rssi_confidence_pct(int8_t rssi, bool astrolabe)
{
    int v = astrolabe ? 52 : 34;
    if (rssi > -65) {
        v += 26;
    } else if (rssi > -82) {
        v += 14;
    }
    if (v > 96) {
        v = 96;
    }
    return (uint8_t)v;
}

static uint16_t ble_addr_hash16(const uint8_t addr[6])
{
    uint32_t h = 2166136261u;
    for (int i = 0; i < 6; ++i) {
        h ^= addr[i];
        h *= 16777619u;
    }
    return (uint16_t)((h >> 16) ^ h);
}

static int8_t ble_clamp_deg_i8(float deg, int lo, int hi)
{
    if (deg < (float)lo) {
        return (int8_t)lo;
    }
    if (deg > (float)hi) {
        return (int8_t)hi;
    }
    return (int8_t)lrintf(deg);
}

static int8_t ble_accel_q6(float g)
{
    const int v = (int)lrintf(g * 64.0f);
    if (v < -128) {
        return -128;
    }
    if (v > 127) {
        return 127;
    }
    return (int8_t)v;
}

static uint8_t ble_motion_score(float ax, float ay, float az)
{
    const float mag = sqrtf((ax * ax) + (ay * ay) + (az * az));
    const float dynamic = fabsf(mag - 1.0f);
    int score = (int)lrintf(dynamic * 160.0f);
    if (score < 0) {
        score = 0;
    }
    if (score > 100) {
        score = 100;
    }
    return (uint8_t)score;
}

static const char *ble_ring_gesture_label(int8_t rssi_delta, uint8_t motion_score)
{
    if (motion_score >= 48 && rssi_delta >= 8) {
        return "reach";
    }
    if (motion_score >= 48 && rssi_delta <= -8) {
        return "withdraw";
    }
    if (motion_score >= 62) {
        return "flick";
    }
    if (rssi_delta >= 10) {
        return "closer";
    }
    if (rssi_delta <= -10) {
        return "farther";
    }
    return "steady";
}

static void ble_ring_telem_store(const faculty175_ble_peer_t *peer)
{
    if (peer == NULL || !peer->ring) {
        return;
    }
    if (s_paired_ring_id_set && peer->addr_hash != s_paired_ring_id) {
        return;
    }

    float ax = 0.0f;
    float ay = 0.0f;
    float az = 0.0f;
    const bool imu_ok = faculty175_motion_accel_g(&ax, &ay, &az);
    float pitch = 0.0f;
    float roll = 0.0f;
    const bool pitch_roll_ok = faculty175_motion_pitch_roll(&pitch, &roll);
    const int8_t delta = s_have_last_ring_rssi ? (int8_t)(peer->rssi - s_last_ring_rssi) : 0;
    s_last_ring_rssi = peer->rssi;
    s_have_last_ring_rssi = true;

    faculty175_ble_ring_telem_t sample = {
        .valid = true,
        .age_ms = ble_now_ms(),
        .ring_id = peer->addr_hash,
        .rssi = peer->rssi,
        .rssi_delta = delta,
        .local_imu_valid = imu_ok,
        .pitch_deg = pitch_roll_ok ? ble_clamp_deg_i8(pitch, -90, 90) : 0,
        .roll_deg = pitch_roll_ok ? ble_clamp_deg_i8(roll, -90, 90) : 0,
        .accel_x_q6 = imu_ok ? ble_accel_q6(ax) : 0,
        .accel_y_q6 = imu_ok ? ble_accel_q6(ay) : 0,
        .accel_z_q6 = imu_ok ? ble_accel_q6(az) : 0,
        .motion_score = imu_ok ? ble_motion_score(ax, ay, az) : 0,
    };
    strncpy(sample.gesture, ble_ring_gesture_label(sample.rssi_delta, sample.motion_score), sizeof(sample.gesture) - 1);
    strncpy(sample.name, peer->name, sizeof(sample.name) - 1);

    portENTER_CRITICAL(&s_ring_telem_lock);
    memmove(&s_ring_telem[1], &s_ring_telem[0], sizeof(s_ring_telem[0]) * (FACULTY175_BLE_RING_TELEM_MAX - 1));
    s_ring_telem[0] = sample;
    portEXIT_CRITICAL(&s_ring_telem_lock);
}

static ble_astrolabe_mfg_t ble_build_mfg_payload(void)
{
    ble_astrolabe_mfg_t payload = {
        .company_id = BLE_ASTROLABE_MFG_COMPANY_ID,
        .magic = BLE_ASTROLABE_MFG_MAGIC,
        .version = BLE_ASTROLABE_MFG_VERSION,
        .seq = ++s_imu_adv_seq,
    };
    float ax = 0.0f;
    float ay = 0.0f;
    float az = 0.0f;
    if (faculty175_motion_accel_g(&ax, &ay, &az)) {
        const float horiz = sqrtf((ay * ay) + (az * az));
        const float pitch = atan2f(-ax, horiz) * 180.0f / (float)M_PI;
        const float roll = atan2f(ay, az) * 180.0f / (float)M_PI;
        payload.pitch_deg = ble_clamp_deg_i8(pitch, -90, 90);
        payload.roll_deg = ble_clamp_deg_i8(roll, -90, 90);
        payload.accel_x_q6 = ble_accel_q6(ax);
        payload.accel_y_q6 = ble_accel_q6(ay);
        payload.accel_z_q6 = ble_accel_q6(az);
        payload.flags = BLE_ASTROLABE_MFG_FLAG_IMU_VALID;
    }
    faculty175_ble_peer_t peers[FACULTY175_BLE_PEER_MAX] = {};
    const size_t peer_count = faculty175_ble_peers_snapshot(peers, FACULTY175_BLE_PEER_MAX);
    for (size_t i = 0; i < peer_count && payload.obs_count < FACULTY175_BLE_OBS_MAX; ++i) {
        if (!peers[i].astrolabe && !peers[i].ring) {
            continue;
        }
        ble_astrolabe_obs_t *obs = &payload.obs[payload.obs_count++];
        obs->addr_hash = ble_addr_hash16(peers[i].addr);
        obs->rssi = peers[i].rssi;
        obs->type = peers[i].astrolabe ? BLE_ASTROLABE_OBS_TYPE_ASTROLABE : BLE_ASTROLABE_OBS_TYPE_RING;
    }
    return payload;
}

static bool ble_parse_mfg_payload(const struct ble_hs_adv_fields *fields, ble_astrolabe_mfg_t *out)
{
    if (fields == NULL || out == NULL || fields->mfg_data == NULL ||
        fields->mfg_data_len < offsetof(ble_astrolabe_mfg_t, obs_count)) {
        return false;
    }
    ble_astrolabe_mfg_t payload = {};
    size_t copy_len = fields->mfg_data_len;
    if (copy_len > sizeof(payload)) {
        copy_len = sizeof(payload);
    }
    memcpy(&payload, fields->mfg_data, copy_len);
    if (payload.company_id != BLE_ASTROLABE_MFG_COMPANY_ID ||
        payload.magic != BLE_ASTROLABE_MFG_MAGIC ||
        payload.version > BLE_ASTROLABE_MFG_VERSION ||
        payload.version == 0) {
        return false;
    }
    if (payload.version < 2) {
        payload.obs_count = 0;
        memset(payload.obs, 0, sizeof(payload.obs));
    } else if (payload.obs_count > FACULTY175_BLE_OBS_MAX) {
        payload.obs_count = FACULTY175_BLE_OBS_MAX;
    }
    *out = payload;
    return true;
}

static void ble_peer_store(const ble_addr_t *addr,
                           const struct ble_hs_adv_fields *fields,
                           int8_t rssi,
                           uint32_t now_ms)
{
    if (addr == NULL || fields == NULL) {
        return;
    }

    char name[FACULTY175_BLE_PEER_NAME_MAX];
    ble_copy_text(name, sizeof(name), fields->name, fields->name_len);
    const uint16_t addr_hash = ble_addr_hash16(addr->val);
    ble_astrolabe_mfg_t mfg = {};
    const bool has_mfg = ble_parse_mfg_payload(fields, &mfg);
    const bool astrolabe = has_mfg || ble_fields_have_astrolabe_uuid(fields) || ble_name_mentions_astrolabe(name);
    const bool paired_ring = s_paired_ring_id_set && addr_hash == s_paired_ring_id;
    const bool ring = !astrolabe && (paired_ring || ble_name_mentions_ring(name));
    if (!astrolabe && !ring && name[0] == '\0') {
        return;
    }

    faculty175_ble_peer_t peer = {
        .valid = true,
        .astrolabe = astrolabe,
        .ring = ring,
        .known = astrolabe || ring,
        .addr_hash = addr_hash,
        .rssi = rssi,
        .tx_power = fields->tx_pwr_lvl_is_present ? fields->tx_pwr_lvl : 0,
        .seen_ms = now_ms,
        .bearing_deg = ble_peer_bearing(addr->val, now_ms),
        .range_pct = ble_rssi_range_pct(rssi),
        .confidence_pct = ble_rssi_confidence_pct(rssi, astrolabe),
        .imu_valid = has_mfg && (mfg.flags & BLE_ASTROLABE_MFG_FLAG_IMU_VALID),
        .imu_pitch_deg = mfg.pitch_deg,
        .imu_roll_deg = mfg.roll_deg,
        .accel_x_q6 = mfg.accel_x_q6,
        .accel_y_q6 = mfg.accel_y_q6,
        .accel_z_q6 = mfg.accel_z_q6,
        .imu_seq = mfg.seq,
        .observation_count = has_mfg ? mfg.obs_count : 0,
    };
    for (uint8_t i = 0; i < peer.observation_count && i < FACULTY175_BLE_OBS_MAX; ++i) {
        peer.observations[i].valid = true;
        peer.observations[i].addr_hash = mfg.obs[i].addr_hash;
        peer.observations[i].rssi = mfg.obs[i].rssi;
        peer.observations[i].astrolabe = (mfg.obs[i].type & BLE_ASTROLABE_OBS_TYPE_ASTROLABE) != 0;
        peer.observations[i].ring = (mfg.obs[i].type & BLE_ASTROLABE_OBS_TYPE_RING) != 0;
    }
    if (peer.imu_valid && peer.confidence_pct <= 86) {
        peer.confidence_pct += 10;
    } else if (peer.imu_valid) {
        peer.confidence_pct = 96;
    }
    memcpy(peer.addr, addr->val, sizeof(peer.addr));
    if (name[0] != '\0') {
        strncpy(peer.name, name, sizeof(peer.name) - 1);
    } else {
        snprintf(peer.name, sizeof(peer.name), "BLE %04X", peer.addr_hash);
    }

    portENTER_CRITICAL(&s_peer_lock);
    int slot = -1;
    int oldest = 0;
    for (int i = 0; i < FACULTY175_BLE_PEER_MAX; ++i) {
        if (s_peers[i].valid && memcmp(s_peers[i].addr, peer.addr, sizeof(peer.addr)) == 0) {
            slot = i;
            break;
        }
        if (!s_peers[i].valid && slot < 0) {
            slot = i;
        }
        if (s_peers[i].seen_ms < s_peers[oldest].seen_ms) {
            oldest = i;
        }
    }
    if (slot < 0) {
        slot = oldest;
    }
    s_peers[slot] = peer;
    portEXIT_CRITICAL(&s_peer_lock);

    ble_ring_telem_store(&peer);
}

static void ble_load_device_name(void)
{
    strncpy(s_device_name, BLE_DEVICE_NAME_FALLBACK, sizeof(s_device_name) - 1);
    nvs_handle_t nvs;
    if (nvs_open(BLE_NVS_IDENTITY_NS, NVS_READONLY, &nvs) != ESP_OK) {
        return;
    }
    size_t len = sizeof(s_device_name);
    esp_err_t err = nvs_get_str(nvs, BLE_NVS_DEVICE_NAME, s_device_name, &len);
    if (err != ESP_OK || s_device_name[0] == '\0') {
        len = sizeof(s_device_name);
        err = nvs_get_str(nvs, BLE_NVS_NAME, s_device_name, &len);
    }
    nvs_close(nvs);
    if (err != ESP_OK || s_device_name[0] == '\0') {
        strncpy(s_device_name, BLE_DEVICE_NAME_FALLBACK, sizeof(s_device_name) - 1);
    }
    s_device_name[sizeof(s_device_name) - 1] = '\0';
}

static esp_err_t ble_apply_settings_json(const char *body)
{
    cJSON *root = cJSON_Parse(body);
    if (root == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = ESP_OK;
    const cJSON *tz = cJSON_GetObjectItemCaseSensitive(root, "tz");
    if (cJSON_IsString(tz) && tz->valuestring != NULL && tz->valuestring[0] != '\0') {
        err = astrolabe_time_set_timezone(tz->valuestring);
    }
    const cJSON *epoch = cJSON_GetObjectItemCaseSensitive(root, "epoch");
    if (err == ESP_OK && cJSON_IsNumber(epoch) && epoch->valuedouble > 1704067200.0) {
        err = astrolabe_time_set_epoch((time_t)epoch->valuedouble);
    }
    const cJSON *location = cJSON_GetObjectItemCaseSensitive(root, "location");
    if (err == ESP_OK && cJSON_IsObject(location)) {
        const cJSON *lat = cJSON_GetObjectItemCaseSensitive(location, "lat");
        const cJSON *lon = cJSON_GetObjectItemCaseSensitive(location, "lon");
        const cJSON *source = cJSON_GetObjectItemCaseSensitive(location, "source");
        if (cJSON_IsNumber(lat) && cJSON_IsNumber(lon)) {
            err = faculty175_location_settings_save(lat->valuedouble,
                                                    lon->valuedouble,
                                                    cJSON_IsString(source) ? source->valuestring : "pwa-ble");
        }
    }
    const cJSON *wifi = cJSON_GetObjectItemCaseSensitive(root, "wifi");
    if (err == ESP_OK && cJSON_IsObject(wifi)) {
        const cJSON *travel_router = cJSON_GetObjectItemCaseSensitive(wifi, "travelRouter");
        if (cJSON_IsBool(travel_router)) {
            err = faculty175_wifi_settings_set_travel_router_enabled(cJSON_IsTrue(travel_router));
        }
        const cJSON *ssid = cJSON_GetObjectItemCaseSensitive(wifi, "ssid");
        const cJSON *pass = cJSON_GetObjectItemCaseSensitive(wifi, "password");
        if (err == ESP_OK && cJSON_IsString(ssid) && ssid->valuestring != NULL && ssid->valuestring[0] != '\0') {
            err = faculty175_wifi_settings_save(ssid->valuestring,
                                                cJSON_IsString(pass) && pass->valuestring != NULL ? pass->valuestring : "");
        }
    }
    const cJSON *ring = cJSON_GetObjectItemCaseSensitive(root, "ring");
    if (err == ESP_OK && cJSON_IsObject(ring)) {
        char *ring_json = cJSON_PrintUnformatted((cJSON *)ring);
        if (ring_json != NULL) {
            const esp_err_t ring_err = faculty175_ring_handle_json(ring_json);
            cJSON_free(ring_json);
            if (ring_err != ESP_OK && ring_err != ESP_ERR_NOT_FOUND) {
                err = ring_err;
            }
        }
    }
    cJSON_Delete(root);
    return err;
}

static int ble_settings_json_access(uint16_t conn_handle,
                                    uint16_t attr_handle,
                                    struct ble_gatt_access_ctxt *ctxt,
                                    void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;
    if (ctxt == NULL) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        astrolabe_time_status_t t = {};
        astrolabe_time_status(&t);
        faculty175_location_settings_t loc = {};
        const bool loc_ok = faculty175_location_settings_load(&loc) == ESP_OK;
        char body[320];
        snprintf(body,
                 sizeof(body),
                 "{\"ok\":true,\"tz\":\"%s\",\"epoch\":%lld,\"location\":{\"valid\":%s,\"lat\":%.5f,\"lon\":%.5f},"
                 "\"wifi\":{\"ap\":%s,\"travelRouter\":%s,\"ssid\":\"%s\",\"url\":\"%s\"}}",
                 t.tz,
                 (long long)t.epoch,
                 loc_ok ? "true" : "false",
                 loc_ok ? loc.lat_deg : 0.0,
                 loc_ok ? loc.lon_deg : 0.0,
                 faculty175_wifi_settings_ap_active() ? "true" : "false",
                 faculty175_wifi_settings_travel_router_enabled() ? "true" : "false",
                 faculty175_wifi_settings_ssid(),
                 faculty175_wifi_settings_url());
        return os_mbuf_append(ctxt->om, body, strlen(body)) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    char chunk[96];
    uint16_t len = 0;
    const int rc = ble_hs_mbuf_to_flat(ctxt->om, chunk, sizeof(chunk) - 1, &len);
    if (rc != 0) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    chunk[len] = '\0';
    if (strcmp(chunk, "BEGIN") == 0) {
        s_json_rx_len = 0;
        s_json_rx[0] = '\0';
        s_json_rx_active = true;
        return 0;
    }
    if (strcmp(chunk, "END") == 0) {
        if (!s_json_rx_active) {
            return BLE_ATT_ERR_UNLIKELY;
        }
        s_json_rx_active = false;
        const esp_err_t err = ble_apply_settings_json(s_json_rx);
        FACULTY175_LOG_STAGE(TAG, "ble-settings", "apply %s", esp_err_to_name(err));
        return err == ESP_OK ? 0 : BLE_ATT_ERR_UNLIKELY;
    }
    if (!s_json_rx_active) {
        const esp_err_t err = ble_apply_settings_json(chunk);
        FACULTY175_LOG_STAGE(TAG, "ble-settings", "apply %s", esp_err_to_name(err));
        return err == ESP_OK ? 0 : BLE_ATT_ERR_UNLIKELY;
    }
    if (s_json_rx_len + len >= sizeof(s_json_rx)) {
        s_json_rx_active = false;
        s_json_rx_len = 0;
        return BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    memcpy(s_json_rx + s_json_rx_len, chunk, len);
    s_json_rx_len += len;
    s_json_rx[s_json_rx_len] = '\0';
    return 0;
}

static esp_err_t ble_nvs_set_enabled(bool enabled)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(BLE_NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_u8(nvs, BLE_NVS_ENABLED, enabled ? 1 : 0);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

static esp_err_t ble_advertise(void)
{
    if (!s_started || !s_synced || !s_enabled) {
        return ESP_ERR_INVALID_STATE;
    }

    struct ble_hs_adv_fields fields = {};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = &BLE_SETTINGS_SERVICE_UUID;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        s_advertising = false;
        ESP_LOGE(TAG, "adv fields failed rc=%d", rc);
        return ESP_FAIL;
    }

    ble_astrolabe_mfg_t mfg = ble_build_mfg_payload();
    struct ble_hs_adv_fields rsp = {};
    const char *name = ble_svc_gap_device_name();
    rsp.name = (uint8_t *)name;
    rsp.name_len = strlen(name);
    rsp.name_is_complete = 1;
    rsp.mfg_data = (const uint8_t *)&mfg;
    rsp.mfg_data_len = sizeof(mfg);
    rsp.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    rsp.tx_pwr_lvl_is_present = 1;
    rc = ble_gap_adv_rsp_set_fields(&rsp);
    while (rc != 0 && rsp.name_len > 0) {
        rsp.name_len = rsp.name_len > 4 ? rsp.name_len - 4 : 0;
        rsp.name_is_complete = 0;
        rc = ble_gap_adv_rsp_set_fields(&rsp);
    }
    if (rc != 0 && rsp.tx_pwr_lvl_is_present) {
        rsp.tx_pwr_lvl_is_present = 0;
        rc = ble_gap_adv_rsp_set_fields(&rsp);
        while (rc != 0 && rsp.name_len > 0) {
            rsp.name_len = rsp.name_len > 4 ? rsp.name_len - 4 : 0;
            rsp.name_is_complete = 0;
            rc = ble_gap_adv_rsp_set_fields(&rsp);
        }
    }
    if (rc != 0) {
        s_advertising = false;
        ESP_LOGE(TAG, "scan response failed rc=%d", rc);
        return ESP_FAIL;
    }

    struct ble_gap_adv_params params = {};
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER, &params, ble_gap_event, NULL);
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        s_advertising = false;
        ESP_LOGE(TAG, "adv start failed rc=%d", rc);
        return ESP_FAIL;
    }
    s_advertising = true;
    return ESP_OK;
}

static void ble_on_reset(int reason)
{
    s_synced = false;
    s_advertising = false;
    ESP_LOGW(TAG, "stack reset reason=%d", reason);
}

static void ble_on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "ensure addr failed rc=%d", rc);
        return;
    }
    rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "infer addr failed rc=%d", rc);
        return;
    }
    s_synced = true;
    (void)ble_advertise();
}

static void ble_host_task(void *param)
{
    (void)param;
    nimble_port_run();
    vTaskDelete(NULL);
}

static int ble_gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    if (event == NULL) {
        return 0;
    }
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            s_advertising = false;
            if (event->connect.status != 0) {
                (void)ble_advertise();
            }
            break;
        case BLE_GAP_EVENT_DISCONNECT:
        case BLE_GAP_EVENT_ADV_COMPLETE:
            s_advertising = false;
            if (!s_scanning) {
                (void)ble_advertise();
            }
            break;
        case BLE_GAP_EVENT_DISC: {
            struct ble_hs_adv_fields fields = {};
            if (ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data) == 0) {
                ble_peer_store(&event->disc.addr, &fields, event->disc.rssi, ble_now_ms());
            }
            break;
        }
        case BLE_GAP_EVENT_DISC_COMPLETE:
            s_scanning = false;
            s_next_scan_ms = ble_now_ms() + 2500u;
            (void)ble_advertise();
            break;
        default:
            break;
    }
    return 0;
}

static int ble_settings_enabled_access(uint16_t conn_handle,
                                       uint16_t attr_handle,
                                       struct ble_gatt_access_ctxt *ctxt,
                                       void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;
    if (ctxt == NULL) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        const uint8_t value = s_enabled ? 1 : 0;
        return os_mbuf_append(ctxt->om, &value, sizeof(value)) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint8_t value = 0;
        const int rc = ble_hs_mbuf_to_flat(ctxt->om, &value, sizeof(value), NULL);
        if (rc != 0) {
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        const esp_err_t err = faculty175_ble_set_enabled(value != 0);
        return err == ESP_OK ? 0 : BLE_ATT_ERR_UNLIKELY;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

esp_err_t faculty175_ble_init(void)
{
    esp_log_level_set("NimBLE", ESP_LOG_NONE);
    esp_log_level_set("BLE_INIT", ESP_LOG_NONE);

    esp_err_t err = ble_nvs_get_enabled(&s_enabled);
    uint16_t ring_id = 0;
    bool ring_id_set = false;
    const esp_err_t ring_id_err = ble_nvs_get_ring_id(&ring_id, &ring_id_set);
    if (ring_id_err == ESP_OK) {
        s_paired_ring_id = ring_id;
        s_paired_ring_id_set = ring_id_set;
    } else {
        ESP_LOGW(TAG, "paired ring read failed %s", esp_err_to_name(ring_id_err));
    }
    ble_load_device_name();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "enabled read failed %s; defaulting on", esp_err_to_name(err));
        s_enabled = true;
    }
    if (!s_enabled) {
        FACULTY175_LOG_STAGE(TAG, "ble", "disabled by NVS");
        return ESP_OK;
    }
    if (s_started) {
        return ESP_OK;
    }

    err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nimble init failed %s", esp_err_to_name(err));
        return err;
    }

    ble_svc_gap_init();
    ble_svc_gatt_init();
    int rc = ble_gatts_count_cfg(k_ble_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "gatt count failed rc=%d", rc);
        return ESP_FAIL;
    }
    rc = ble_gatts_add_svcs(k_ble_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "gatt add failed rc=%d", rc);
        return ESP_FAIL;
    }
    rc = ble_svc_gap_device_name_set(s_device_name);
    if (rc != 0) {
        ESP_LOGE(TAG, "device name failed rc=%d", rc);
        return ESP_FAIL;
    }
    rc = ble_svc_gap_device_appearance_set(BLE_APPEARANCE_GENERIC_TAG);
    if (rc != 0) {
        ESP_LOGE(TAG, "appearance failed rc=%d", rc);
        return ESP_FAIL;
    }

    ble_hs_cfg.reset_cb = ble_on_reset;
    ble_hs_cfg.sync_cb = ble_on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_store_config_init();

    s_started = true;
    nimble_port_freertos_init(ble_host_task);
    return ESP_OK;
}

bool faculty175_ble_enabled(void)
{
    return s_enabled;
}

bool faculty175_ble_advertising(void)
{
    return s_advertising;
}

bool faculty175_ble_scanning(void)
{
    return s_scanning;
}

const char *faculty175_ble_device_name(void)
{
    return s_device_name;
}

esp_err_t faculty175_ble_set_device_name(const char *name)
{
    if (name == NULL || name[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    char next[FACULTY175_BLE_PEER_NAME_MAX] = {};
    snprintf(next, sizeof(next), "Astrolabe %.21s", name);
    if (ble_name_mentions_astrolabe(name)) {
        strncpy(next, name, sizeof(next) - 1);
    }
    next[sizeof(next) - 1] = '\0';

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(BLE_NVS_IDENTITY_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_str(nvs, BLE_NVS_DEVICE_NAME, next);
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, BLE_NVS_NAME, next);
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    if (err != ESP_OK) {
        return err;
    }
    strncpy(s_device_name, next, sizeof(s_device_name) - 1);
    s_device_name[sizeof(s_device_name) - 1] = '\0';
    if (s_started) {
        const int rc = ble_svc_gap_device_name_set(s_device_name);
        if (rc != 0) {
            return ESP_FAIL;
        }
        if (s_advertising) {
            (void)ble_gap_adv_stop();
            s_advertising = false;
        }
        if (!s_scanning) {
            return ble_advertise();
        }
    }
    return ESP_OK;
}

esp_err_t faculty175_ble_scan_start(uint32_t duration_ms)
{
    if (!s_started || !s_synced || !s_enabled) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_scanning) {
        return ESP_OK;
    }
    if (duration_ms < 600u) {
        duration_ms = 600u;
    }
    if (duration_ms > 6000u) {
        duration_ms = 6000u;
    }
    s_scanning = true;
    if (s_advertising) {
        (void)ble_gap_adv_stop();
        s_advertising = false;
    }
    struct ble_gap_disc_params params = {};
    params.passive = 0;
    params.itvl = 0x40;
    params.window = 0x30;
    params.filter_policy = 0;
    params.limited = 0;
    params.filter_duplicates = 0;
    const int rc = ble_gap_disc(s_own_addr_type, (int32_t)duration_ms, &params, ble_gap_event, NULL);
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        s_scanning = false;
        s_next_scan_ms = ble_now_ms() + 1800u + (esp_random() % 1400u);
        (void)ble_advertise();
        ESP_LOGW(TAG, "scan start failed rc=%d", rc);
        return ESP_FAIL;
    }
    s_next_scan_ms = ble_now_ms() + duration_ms + 900u + (esp_random() % 1600u);
    return ESP_OK;
}

void faculty175_ble_radar_tick(uint32_t now_ms)
{
    if (!s_enabled || !s_started || !s_synced || s_scanning) {
        return;
    }
    if (now_ms == 0) {
        now_ms = ble_now_ms();
    }
    if ((int32_t)(now_ms - s_serial_quiet_until_ms) < 0) {
        return;
    }
    if (s_next_scan_ms == 0) {
        s_next_scan_ms = now_ms + 5000u + (esp_random() % 4000u);
        return;
    }
    if (now_ms >= s_next_scan_ms) {
        (void)faculty175_ble_scan_start(1800u);
    }
}

size_t faculty175_ble_peers_snapshot(faculty175_ble_peer_t *out, size_t cap)
{
    if (out == NULL || cap == 0) {
        return 0;
    }
    if (cap > FACULTY175_BLE_PEER_MAX) {
        cap = FACULTY175_BLE_PEER_MAX;
    }
    faculty175_ble_peer_t tmp[FACULTY175_BLE_PEER_MAX];
    portENTER_CRITICAL(&s_peer_lock);
    memcpy(tmp, s_peers, sizeof(tmp));
    portEXIT_CRITICAL(&s_peer_lock);

    const uint32_t now = ble_now_ms();
    size_t count = 0;
    for (size_t i = 0; i < FACULTY175_BLE_PEER_MAX; ++i) {
        if (!tmp[i].valid || now - tmp[i].seen_ms > 60000u) {
            continue;
        }
        size_t at = count;
        while (at > 0 && out[at - 1].rssi < tmp[i].rssi) {
            if (at < cap) {
                out[at] = out[at - 1];
            }
            --at;
        }
        if (at < cap) {
            out[at] = tmp[i];
        }
        if (count < cap) {
            ++count;
        }
    }
    return count;
}

size_t faculty175_ble_ring_telemetry_snapshot(faculty175_ble_ring_telem_t *out, size_t cap)
{
    if (out == NULL || cap == 0) {
        return 0;
    }
    if (cap > FACULTY175_BLE_RING_TELEM_MAX) {
        cap = FACULTY175_BLE_RING_TELEM_MAX;
    }
    portENTER_CRITICAL(&s_ring_telem_lock);
    memcpy(out, s_ring_telem, sizeof(out[0]) * cap);
    portEXIT_CRITICAL(&s_ring_telem_lock);

    const uint32_t now = ble_now_ms();
    size_t count = 0;
    for (size_t i = 0; i < cap; ++i) {
        if (!out[i].valid) {
            break;
        }
        out[i].age_ms = now - out[i].age_ms;
        ++count;
    }
    return count;
}

esp_err_t faculty175_ble_set_enabled(bool enabled)
{
    esp_err_t err = ble_nvs_set_enabled(enabled);
    if (err != ESP_OK) {
        return err;
    }
    s_enabled = enabled;
    if (!enabled) {
        if (s_advertising) {
            (void)ble_gap_adv_stop();
        }
        if (s_scanning) {
            (void)ble_gap_disc_cancel();
        }
        s_advertising = false;
        s_scanning = false;
        FACULTY175_LOG_STAGE(TAG, "ble", "advertising off");
        return ESP_OK;
    }
    if (!s_started) {
        return faculty175_ble_init();
    }
    return ble_advertise();
}

bool faculty175_ble_handle(const char *line)
{
    if (line == NULL || (strcasecmp(line, "ble") != 0 && strncasecmp(line, "ble ", 4) != 0)) {
        return false;
    }
    const char *sub = strchr(line, ' ');
    sub = sub != NULL ? sub + 1 : "status";
    while (*sub == ' ') {
        ++sub;
    }

    const bool explicit_scan_cmd = strcasecmp(sub, "scan") == 0 || strcasecmp(sub, "ring scan") == 0;
    if (!explicit_scan_cmd) {
        ble_defer_radar_scan(10000u);
    }

    if (*sub == '\0' || strcasecmp(sub, "status") == 0) {
        printf("ble: enabled=%s started=%s synced=%s advertising=%s scanning=%s name=\"%s\"\n",
               s_enabled ? "yes" : "no",
               s_started ? "yes" : "no",
               s_synced ? "yes" : "no",
               s_advertising ? "yes" : "no",
               s_scanning ? "yes" : "no",
               s_device_name);
    } else if (strcasecmp(sub, "on") == 0 || strcasecmp(sub, "enable") == 0) {
        const esp_err_t err = faculty175_ble_set_enabled(true);
        printf("ble: enable %s\n", esp_err_to_name(err));
    } else if (strcasecmp(sub, "off") == 0 || strcasecmp(sub, "disable") == 0) {
        const esp_err_t err = faculty175_ble_set_enabled(false);
        printf("ble: disable %s\n", esp_err_to_name(err));
    } else if (strncasecmp(sub, "name ", 5) == 0) {
        const char *name = sub + 5;
        while (*name == ' ') {
            ++name;
        }
        const esp_err_t err = faculty175_ble_set_device_name(name);
        printf("ble: name %s \"%s\"\n", esp_err_to_name(err), s_device_name);
    } else if (strcasecmp(sub, "scan") == 0) {
        const esp_err_t err = faculty175_ble_scan_start(3000u);
        printf("ble: scan %s\n", esp_err_to_name(err));
    } else if (strcasecmp(sub, "peers") == 0) {
        faculty175_ble_peer_t peers[FACULTY175_BLE_PEER_MAX];
        const size_t count = faculty175_ble_peers_snapshot(peers, FACULTY175_BLE_PEER_MAX);
        printf("ble: peers=%u scanning=%s\n", (unsigned)count, s_scanning ? "yes" : "no");
        for (size_t i = 0; i < count; ++i) {
            printf("  %u %s id=%04x rssi=%d range=%u%% bearing=%u confidence=%u%% imu=%s pitch=%d roll=%d accel=%.2f,%.2f,%.2f seq=%u name=\"%s\"\n",
                   (unsigned)i,
                   peers[i].astrolabe ? "astrolabe" : (peers[i].ring ? "ring" : "ble"),
                   peers[i].addr_hash,
                   peers[i].rssi,
                   peers[i].range_pct,
                   peers[i].bearing_deg,
                   peers[i].confidence_pct,
                   peers[i].imu_valid ? "yes" : "no",
                   peers[i].imu_pitch_deg,
                   peers[i].imu_roll_deg,
                   (double)peers[i].accel_x_q6 / 64.0,
                   (double)peers[i].accel_y_q6 / 64.0,
                   (double)peers[i].accel_z_q6 / 64.0,
                   peers[i].imu_seq,
                   peers[i].name);
            for (uint8_t j = 0; j < peers[i].observation_count && j < FACULTY175_BLE_OBS_MAX; ++j) {
                const faculty175_ble_observation_t *obs = &peers[i].observations[j];
                if (!obs->valid) {
                    continue;
                }
                printf("    sees %s hash=%04x rssi=%d\n",
                       obs->astrolabe ? "astrolabe" : (obs->ring ? "ring" : "ble"),
                       obs->addr_hash,
                       obs->rssi);
            }
        }
    } else if (strcasecmp(sub, "ring") == 0 || strcasecmp(sub, "ring status") == 0) {
        enum { BLE_RING_PRINT_MAX = 8 };
        faculty175_ble_ring_telem_t samples[BLE_RING_PRINT_MAX];
        const size_t count = faculty175_ble_ring_telemetry_snapshot(samples, BLE_RING_PRINT_MAX);
        printf("ble: ring paired=%s", s_paired_ring_id_set ? "yes" : "no");
        if (s_paired_ring_id_set) {
            printf(" id=%04x", s_paired_ring_id);
        }
        printf(" samples=%u scanning=%s\n", (unsigned)count, s_scanning ? "yes" : "no");
        for (size_t i = 0; i < count; ++i) {
            printf("  %u ring id=%04x age=%ums rssi=%d delta=%+d imu=%s pitch=%d roll=%d accel=%.2f,%.2f,%.2f motion=%u gesture=%s name=\"%s\"\n",
                   (unsigned)i,
                   samples[i].ring_id,
                   (unsigned)samples[i].age_ms,
                   samples[i].rssi,
                   samples[i].rssi_delta,
                   samples[i].local_imu_valid ? "yes" : "no",
                   samples[i].pitch_deg,
                   samples[i].roll_deg,
                   (double)samples[i].accel_x_q6 / 64.0,
                   (double)samples[i].accel_y_q6 / 64.0,
                   (double)samples[i].accel_z_q6 / 64.0,
                   samples[i].motion_score,
                   samples[i].gesture,
                   samples[i].name);
        }
    } else if (strncasecmp(sub, "ring pair ", 10) == 0) {
        const char *id_text = sub + 10;
        while (*id_text == ' ') {
            ++id_text;
        }
        char *end = NULL;
        const unsigned long id = strtoul(id_text, &end, 16);
        if (end == id_text || id > 0xfffful) {
            printf("ble: ring pair ESP_ERR_INVALID_ARG\n");
        } else {
            const esp_err_t err = ble_nvs_set_ring_id((uint16_t)id, true);
            if (err == ESP_OK) {
                s_paired_ring_id = (uint16_t)id;
                s_paired_ring_id_set = true;
                s_have_last_ring_rssi = false;
            }
            printf("ble: ring pair %s id=%04x\n", esp_err_to_name(err), (unsigned)id);
        }
    } else if (strcasecmp(sub, "ring clear") == 0 || strcasecmp(sub, "ring unpair") == 0) {
        const esp_err_t err = ble_nvs_set_ring_id(0, false);
        if (err == ESP_OK) {
            s_paired_ring_id = 0;
            s_paired_ring_id_set = false;
            s_have_last_ring_rssi = false;
        }
        printf("ble: ring clear %s\n", esp_err_to_name(err));
    } else if (strcasecmp(sub, "ring scan") == 0) {
        const esp_err_t err = faculty175_ble_scan_start(3000u);
        printf("ble: ring scan %s\n", esp_err_to_name(err));
    } else {
        printf("ble commands:\n");
        printf("  ble status\n");
        printf("  ble on\n");
        printf("  ble off\n");
        printf("  ble name <device name>\n");
        printf("  ble scan\n");
        printf("  ble peers\n");
        printf("  ble ring\n");
        printf("  ble ring pair <short-id>\n");
        printf("  ble ring clear\n");
        printf("  ble ring scan\n");
    }
    fflush(stdout);
    return true;
}
