#include "faculty175_ble.h"

#include <math.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "cJSON.h"
#include "astrolabe_time.h"
#include "esp_attr.h"
#include "esp_app_desc.h"
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
#include "faculty175_charts.h"
#include "faculty175_device_settings.h"
#include "faculty175_cycle_health.h"
#include "faculty175_face_psych_state.h"
#include "faculty175_faces.h"
#include "faculty175_motion.h"
#include "faculty175_ota.h"
#include "faculty175_pmu.h"
#include "faculty175_research.h"
#include "faculty175_relationship_weather.h"
#include "faculty175_ring.h"
#include "faculty175_spotify.h"
#include "faculty175_voice.h"
#include "faculty175_wifi_settings.h"

void ble_store_config_init(void);

static const char *TAG = "faculty175_ble";
static const char *BLE_NVS_NS = "ble";
static const char *BLE_NVS_ENABLED = "enabled";
static const char *BLE_NVS_RING_ID = "ring_id";
static const char *BLE_NVS_NEAR_RSSI = "near_rssi";
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

enum {
    COLMI_PACKET_LEN = 16,
    COLMI_CMD_BATTERY = 0x03,
    COLMI_CMD_RAW_DATA = 0xa1,
    COLMI_CMD_REALTIME_START = 0x69,
    COLMI_CMD_REALTIME_STOP = 0x6a,
    COLMI_REALTIME_HEART_RATE = 0x01,
    COLMI_REALTIME_SPO2 = 0x03,
    COLMI_REALTIME_HRV = 0x0a,
};

typedef enum {
    COLMI_CLIENT_IDLE = 0,
    COLMI_CLIENT_SCAN,
    COLMI_CLIENT_CONNECTING,
    COLMI_CLIENT_DISC_SERVICE,
    COLMI_CLIENT_DISC_RX,
    COLMI_CLIENT_DISC_TX,
    COLMI_CLIENT_DISC_CCC,
    COLMI_CLIENT_READY,
    COLMI_CLIENT_READING_BATTERY,
    COLMI_CLIENT_READING_HR,
    COLMI_CLIENT_READING_SPO2,
    COLMI_CLIENT_READING_HRV,
    COLMI_CLIENT_STREAMING_IMU,
    COLMI_CLIENT_DONE,
    COLMI_CLIENT_ERROR,
} colmi_client_state_t;

typedef enum {
    COLMI_ACTION_NONE = 0,
    COLMI_ACTION_BATTERY,
    COLMI_ACTION_START_HR,
    COLMI_ACTION_START_SPO2,
    COLMI_ACTION_START_HRV,
    COLMI_ACTION_START_RAW_IMU,
} colmi_client_action_t;

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
static const ble_uuid128_t BLE_STATE_JSON_CHAR_UUID =
    BLE_UUID128_INIT(0x41, 0x73, 0x74, 0x72, 0x6f, 0x6c, 0x61, 0x62, 0x65, 0x00, 0x17, 0x50, 0x00, 0x00, 0x00, 0x04);
static const ble_uuid128_t BLE_HEALTH_JSON_CHAR_UUID =
    BLE_UUID128_INIT(0x41, 0x73, 0x74, 0x72, 0x6f, 0x6c, 0x61, 0x62, 0x65, 0x00, 0x17, 0x50, 0x00, 0x00, 0x00, 0x05);
static const ble_uuid128_t COLMI_UART_SERVICE_UUID =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3, 0xa3, 0xb5, 0xf0, 0xff, 0x40, 0x6e);
static const ble_uuid128_t COLMI_UART_RX_UUID =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e);
static const ble_uuid128_t COLMI_UART_TX_UUID =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3, 0xa3, 0xb5, 0x03, 0x00, 0x40, 0x6e);

static bool s_enabled = true;
static bool s_started;
static bool s_power_test_active;
static bool s_power_test_previous_enabled;
static bool s_power_scenario_suspended;
static bool s_synced;
static bool s_advertising;
static bool s_scanning;
static uint8_t s_own_addr_type;
EXT_RAM_BSS_ATTR static char s_json_rx[768];
EXT_RAM_BSS_ATTR static char s_state_json[2048];
EXT_RAM_BSS_ATTR static char s_health_json[512];
static size_t s_json_rx_len;
static bool s_json_rx_active;
static char s_device_name[FACULTY175_BLE_PEER_NAME_MAX] = "Astrolabe Faculty";

static bool ble_json_escape(char *out,
                            size_t cap,
                            const char *value);
EXT_RAM_BSS_ATTR static faculty175_ble_peer_t s_peers[FACULTY175_BLE_PEER_MAX];
static portMUX_TYPE s_peer_lock = portMUX_INITIALIZER_UNLOCKED;
EXT_RAM_BSS_ATTR static faculty175_ble_ring_telem_t s_ring_telem[FACULTY175_BLE_RING_TELEM_MAX];
static portMUX_TYPE s_ring_telem_lock = portMUX_INITIALIZER_UNLOCKED;
static uint16_t s_paired_ring_id;
static bool s_paired_ring_id_set;
static int8_t s_last_ring_rssi;
static bool s_have_last_ring_rssi;
static uint32_t s_next_scan_ms;
static uint32_t s_serial_quiet_until_ms;
static uint8_t s_imu_adv_seq;
static bool s_raw_scan_log;
static uint32_t s_raw_scan_until_ms;
static colmi_client_state_t s_colmi_state;
static ble_addr_t s_colmi_addr;
static uint16_t s_colmi_conn_handle;
static uint16_t s_colmi_svc_start;
static uint16_t s_colmi_svc_end;
static uint16_t s_colmi_rx_handle;
static uint16_t s_colmi_tx_handle;
static uint16_t s_colmi_ccc_handle;
static uint32_t s_colmi_started_ms;
static uint32_t s_colmi_last_packet_ms;
static uint32_t s_colmi_connect_after_ms;
static uint32_t s_colmi_measure_started_ms;
static uint32_t s_colmi_action_due_ms;
static colmi_client_action_t s_colmi_action;
static faculty175_ring_vitals_t s_colmi_working_vitals;
static bool s_colmi_have_conn;
static bool s_colmi_want_scan;
static bool s_colmi_connect_started;
static bool s_colmi_have_packet;
static uint8_t s_colmi_last_packet[COLMI_PACKET_LEN];
static bool s_lunasay_ring_control_active;
static bool s_lunasay_ring_near;
static uint32_t s_lunasay_ring_near_ms;
static uint32_t s_lunasay_ring_retry_ms;
static faculty175_ble_ring_event_t s_lunasay_ring_event;
static portMUX_TYPE s_lunasay_ring_event_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_colmi_imu_have_sample;
static float s_colmi_imu_x_g;
static float s_colmi_imu_y_g;
static uint32_t s_colmi_imu_last_swipe_ms;
static uint32_t s_colmi_imu_stream_started_ms;

static int ble_gap_event(struct ble_gap_event *event, void *arg);
static esp_err_t ble_advertise(void);
static void ble_ring_telem_store(const faculty175_ble_peer_t *peer);

static int16_t colmi_i12(uint8_t high, uint8_t low_nibble)
{
    int16_t value = (int16_t)(((uint16_t)high << 4) | (low_nibble & 0x0f));
    if ((value & 0x0800) != 0) {
        value -= 0x1000;
    }
    return value;
}

static void lunasay_ring_event_post(faculty175_ble_ring_event_t event)
{
    if (event == FACULTY175_BLE_RING_EVENT_NONE) {
        return;
    }
    portENTER_CRITICAL(&s_lunasay_ring_event_lock);
    /* Keep an unconsumed swipe: losing a real gesture is worse than a later
     * proximity notification, and a single-slot queue bounds internal RAM. */
    if (s_lunasay_ring_event == FACULTY175_BLE_RING_EVENT_NONE ||
        event != FACULTY175_BLE_RING_EVENT_NEAR) {
        s_lunasay_ring_event = event;
    }
    portEXIT_CRITICAL(&s_lunasay_ring_event_lock);
}
static esp_err_t colmi_client_start(void);
static void colmi_client_finish(void);
static void colmi_client_on_ring_found(const ble_addr_t *addr, uint8_t event_type, int8_t rssi);
static int ble_settings_enabled_access(uint16_t conn_handle,
                                       uint16_t attr_handle,
                                       struct ble_gatt_access_ctxt *ctxt,
                                       void *arg);
static int ble_settings_json_access(uint16_t conn_handle,
                                    uint16_t attr_handle,
                                    struct ble_gatt_access_ctxt *ctxt,
                                    void *arg);
static int ble_state_json_access(uint16_t conn_handle,
                                 uint16_t attr_handle,
                                 struct ble_gatt_access_ctxt *ctxt,
                                 void *arg);
static int ble_health_json_access(uint16_t conn_handle,
                                  uint16_t attr_handle,
                                  struct ble_gatt_access_ctxt *ctxt,
                                  void *arg);
static void ble_ring_telem_store(const faculty175_ble_peer_t *peer);

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
            {
                .uuid = &BLE_STATE_JSON_CHAR_UUID.u,
                .access_cb = ble_state_json_access,
                .flags = BLE_GATT_CHR_F_READ,
            },
            {
                .uuid = &BLE_HEALTH_JSON_CHAR_UUID.u,
                .access_cb = ble_health_json_access,
                .flags = BLE_GATT_CHR_F_READ,
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

static int8_t ble_near_rssi_clamp(int8_t threshold)
{
    if (threshold < -90) return -90;
    if (threshold > -45) return -45;
    return threshold;
}

int8_t faculty175_ble_near_rssi_threshold(void)
{
    int8_t value = -65;
    nvs_handle_t nvs;
    if (nvs_open(BLE_NVS_NS, NVS_READONLY, &nvs) == ESP_OK) {
        int8_t stored = 0;
        if (nvs_get_i8(nvs, BLE_NVS_NEAR_RSSI, &stored) == ESP_OK) {
            value = ble_near_rssi_clamp(stored);
        }
        nvs_close(nvs);
    }
    return value;
}

esp_err_t faculty175_ble_set_near_rssi_threshold(int8_t threshold)
{
    const int8_t value = ble_near_rssi_clamp(threshold);
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(BLE_NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;
    err = nvs_set_i8(nvs, BLE_NVS_NEAR_RSSI, value);
    if (err == ESP_OK) err = nvs_commit(nvs);
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

static bool ble_parse_public_mac_text(const char *text, uint8_t out_addr[6])
{
    if (text == NULL || out_addr == NULL) {
        return false;
    }
    unsigned b[6] = {};
    char tail = '\0';
    if (sscanf(text,
               " %2x:%2x:%2x:%2x:%2x:%2x %c",
               &b[0],
               &b[1],
               &b[2],
               &b[3],
               &b[4],
               &b[5],
               &tail) != 6) {
        return false;
    }
    for (int i = 0; i < 6; ++i) {
        if (b[i] > 0xffu) {
            return false;
        }
        out_addr[i] = (uint8_t)b[5 - i];
    }
    return true;
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

static void ble_format_addr(const uint8_t addr[6], char *out, size_t cap)
{
    if (out == NULL || cap == 0) {
        return;
    }
    if (addr == NULL) {
        snprintf(out, cap, "00:00:00:00:00:00");
        return;
    }
    snprintf(out,
             cap,
             "%02x:%02x:%02x:%02x:%02x:%02x",
             addr[5],
             addr[4],
             addr[3],
             addr[2],
             addr[1],
             addr[0]);
}

static void ble_format_hex(const uint8_t *data, size_t len, char *out, size_t cap)
{
    if (out == NULL || cap == 0) {
        return;
    }
    out[0] = '\0';
    if (data == NULL || len == 0) {
        return;
    }
    size_t off = 0;
    const size_t max_len = len > 12 ? 12 : len;
    for (size_t i = 0; i < max_len && off + 3 < cap; ++i) {
        off += snprintf(out + off, cap - off, "%02x", data[i]);
    }
}

static void ble_format_hex_full(const uint8_t *data, size_t len, char *out, size_t cap)
{
    if (out == NULL || cap == 0) {
        return;
    }
    out[0] = '\0';
    if (data == NULL || len == 0) {
        return;
    }
    size_t off = 0;
    for (size_t i = 0; i < len && off + 3 < cap; ++i) {
        off += snprintf(out + off, cap - off, "%02x", data[i]);
    }
}

static void ble_store_raw_ring_fallback(const ble_addr_t *addr, int8_t rssi, uint32_t now_ms)
{
    if (addr == NULL) {
        return;
    }
    const uint16_t addr_hash = ble_addr_hash16(addr->val);
    if (!s_paired_ring_id_set || addr_hash != s_paired_ring_id) {
        return;
    }

    faculty175_ble_peer_t peer = {
        .valid = true,
        .ring = true,
        .known = true,
        .addr_hash = addr_hash,
        .rssi = rssi,
        .seen_ms = now_ms,
        .bearing_deg = ble_peer_bearing(addr->val, now_ms),
        .range_pct = ble_rssi_range_pct(rssi),
        .confidence_pct = ble_rssi_confidence_pct(rssi, false),
    };
    memcpy(peer.addr, addr->val, sizeof(peer.addr));
    snprintf(peer.name, sizeof(peer.name), "RING %04X", peer.addr_hash);

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

static const char *colmi_state_name(colmi_client_state_t state)
{
    switch (state) {
        case COLMI_CLIENT_IDLE:
            return "idle";
        case COLMI_CLIENT_SCAN:
            return "scan";
        case COLMI_CLIENT_CONNECTING:
            return "connecting";
        case COLMI_CLIENT_DISC_SERVICE:
            return "disc-service";
        case COLMI_CLIENT_DISC_RX:
            return "disc-rx";
        case COLMI_CLIENT_DISC_TX:
            return "disc-tx";
        case COLMI_CLIENT_DISC_CCC:
            return "disc-ccc";
        case COLMI_CLIENT_READY:
            return "ready";
        case COLMI_CLIENT_READING_BATTERY:
            return "battery";
        case COLMI_CLIENT_READING_HR:
            return "heart-rate";
        case COLMI_CLIENT_READING_SPO2:
            return "spo2";
        case COLMI_CLIENT_READING_HRV:
            return "hrv";
        case COLMI_CLIENT_STREAMING_IMU:
            return "raw-imu";
        case COLMI_CLIENT_DONE:
            return "done";
        case COLMI_CLIENT_ERROR:
            return "error";
        default:
            return "?";
    }
}

static uint8_t colmi_checksum(const uint8_t packet[COLMI_PACKET_LEN])
{
    uint16_t sum = 0;
    for (size_t i = 0; i < COLMI_PACKET_LEN - 1; ++i) {
        sum += packet[i];
    }
    return (uint8_t)(sum & 0xffu);
}

static void colmi_make_packet(uint8_t cmd, const uint8_t *sub, size_t sub_len, uint8_t out[COLMI_PACKET_LEN])
{
    memset(out, 0, COLMI_PACKET_LEN);
    out[0] = cmd;
    if (sub != NULL && sub_len > 0) {
        if (sub_len > COLMI_PACKET_LEN - 2) {
            sub_len = COLMI_PACKET_LEN - 2;
        }
        memcpy(&out[1], sub, sub_len);
    }
    out[COLMI_PACKET_LEN - 1] = colmi_checksum(out);
}

static bool colmi_packet_checksum_ok(const uint8_t packet[COLMI_PACKET_LEN])
{
    return packet[COLMI_PACKET_LEN - 1] == colmi_checksum(packet);
}

static bool colmi_adv_event_connectable(uint8_t event_type)
{
    return event_type == BLE_HCI_ADV_RPT_EVTYPE_ADV_IND || event_type == BLE_HCI_ADV_RPT_EVTYPE_DIR_IND;
}

static void colmi_schedule_action(colmi_client_action_t action, uint32_t delay_ms)
{
    s_colmi_action = action;
    s_colmi_action_due_ms = ble_now_ms() + delay_ms;
}

static esp_err_t colmi_send_packet(const uint8_t packet[COLMI_PACKET_LEN])
{
    if (!s_colmi_have_conn || s_colmi_rx_handle == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    const int rc = ble_gattc_write_no_rsp_flat(s_colmi_conn_handle,
                                               s_colmi_rx_handle,
                                               packet,
                                               COLMI_PACKET_LEN);
    if (rc != 0) {
        ESP_LOGW(TAG, "colmi send failed rc=%d", rc);
        s_colmi_state = COLMI_CLIENT_ERROR;
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t colmi_start_realtime(uint8_t kind)
{
    uint8_t sub[2] = {kind, 0x01};
    uint8_t packet[COLMI_PACKET_LEN];
    colmi_make_packet(COLMI_CMD_REALTIME_START, sub, sizeof(sub), packet);
    return colmi_send_packet(packet);
}

static esp_err_t colmi_stop_realtime(uint8_t kind)
{
    uint8_t sub[3] = {kind, 0x00, 0x00};
    uint8_t packet[COLMI_PACKET_LEN];
    colmi_make_packet(COLMI_CMD_REALTIME_STOP, sub, sizeof(sub), packet);
    return colmi_send_packet(packet);
}

static esp_err_t colmi_start_raw_imu(void)
{
    const uint8_t sub[] = {0x04};
    uint8_t packet[COLMI_PACKET_LEN];
    colmi_make_packet(COLMI_CMD_RAW_DATA, sub, sizeof(sub), packet);
    return colmi_send_packet(packet);
}

static void colmi_handle_raw_imu(const uint8_t data[COLMI_PACKET_LEN])
{
    if (data[1] != 0x03) {
        if (data[1] == 0xff) {
            ESP_LOGW(TAG, "colmi raw IMU stream rejected by ring firmware");
        }
        return;
    }

    /* The R02 packets order axes Y, Z, X and use signed 12-bit values at
     * 512 LSB/g.  Classify the strongest planar impulse as a four-way swipe;
     * gravity, normal hand orientation, and the return stroke are absorbed by
     * the delta threshold and cooldown. */
    const float y_g = (float)colmi_i12(data[2], data[3]) / 512.0f;
    const float x_g = (float)colmi_i12(data[6], data[7]) / 512.0f;
    const uint32_t now_ms = ble_now_ms();
    if (!s_lunasay_ring_near) {
        s_lunasay_ring_near = true;
        lunasay_ring_event_post(FACULTY175_BLE_RING_EVENT_NEAR);
        ESP_LOGI(TAG, "lunasay ring nearby");
    }
    s_lunasay_ring_near_ms = now_ms;

    if (s_colmi_imu_have_sample && now_ms - s_colmi_imu_stream_started_ms >= 250u) {
        const float x_impulse_g = x_g - s_colmi_imu_x_g;
        const float y_impulse_g = y_g - s_colmi_imu_y_g;
        const bool cooled_down = now_ms - s_colmi_imu_last_swipe_ms >= 850u;
        const bool horizontal = fabsf(x_impulse_g) >= fabsf(y_impulse_g);
        const float impulse_g = horizontal ? x_impulse_g : y_impulse_g;
        if (cooled_down && fabsf(impulse_g) >= 0.55f) {
            const faculty175_ble_ring_event_t event = horizontal
                ? (impulse_g > 0.0f ? FACULTY175_BLE_RING_EVENT_SWIPE_NEXT
                                    : FACULTY175_BLE_RING_EVENT_SWIPE_PREVIOUS)
                : (impulse_g > 0.0f ? FACULTY175_BLE_RING_EVENT_SWIPE_UP
                                    : FACULTY175_BLE_RING_EVENT_SWIPE_DOWN);
            s_colmi_imu_last_swipe_ms = now_ms;
            lunasay_ring_event_post(event);
            const char *direction = event == FACULTY175_BLE_RING_EVENT_SWIPE_NEXT ? "right"
                                  : event == FACULTY175_BLE_RING_EVENT_SWIPE_PREVIOUS ? "left"
                                  : event == FACULTY175_BLE_RING_EVENT_SWIPE_UP ? "up" : "down";
            ESP_LOGI(TAG, "lunasay ring swipe %s impulse=%.2fg x=%.2f y=%.2f",
                     direction, (double)impulse_g, (double)x_impulse_g, (double)y_impulse_g);
        }
    }
    s_colmi_imu_x_g = x_g;
    s_colmi_imu_y_g = y_g;
    s_colmi_imu_have_sample = true;
}

static void colmi_stop_current_realtime(void)
{
    if (s_colmi_state == COLMI_CLIENT_READING_HR) {
        (void)colmi_stop_realtime(COLMI_REALTIME_HEART_RATE);
    } else if (s_colmi_state == COLMI_CLIENT_READING_SPO2) {
        (void)colmi_stop_realtime(COLMI_REALTIME_SPO2);
    } else if (s_colmi_state == COLMI_CLIENT_READING_HRV) {
        (void)colmi_stop_realtime(COLMI_REALTIME_HRV);
    }
}

static void colmi_advance_after_realtime(uint8_t kind)
{
    if (kind == COLMI_REALTIME_HEART_RATE && s_colmi_state == COLMI_CLIENT_READING_HR) {
        colmi_stop_current_realtime();
        s_colmi_state = COLMI_CLIENT_READY;
        colmi_schedule_action(COLMI_ACTION_START_SPO2, 1500u);
    } else if (kind == COLMI_REALTIME_SPO2 && s_colmi_state == COLMI_CLIENT_READING_SPO2) {
        colmi_stop_current_realtime();
        s_colmi_state = COLMI_CLIENT_READY;
        colmi_schedule_action(COLMI_ACTION_START_HRV, 1500u);
    } else if (kind == COLMI_REALTIME_HRV && s_colmi_state == COLMI_CLIENT_READING_HRV) {
        colmi_stop_current_realtime();
        colmi_client_finish();
    }
}

static void colmi_client_finish(void)
{
    s_colmi_working_vitals.updated_ms = ble_now_ms();
    faculty175_ring_update_vitals(&s_colmi_working_vitals);
    s_colmi_action = COLMI_ACTION_NONE;
    s_colmi_action_due_ms = 0;
    s_colmi_state = COLMI_CLIENT_DONE;
    if (s_colmi_have_conn) {
        (void)ble_gap_terminate(s_colmi_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
}

static void colmi_client_advance_after_packet(uint8_t cmd, uint8_t kind, uint8_t value)
{
    if (cmd == COLMI_CMD_BATTERY && s_colmi_state == COLMI_CLIENT_READING_BATTERY) {
        s_colmi_working_vitals.battery_percent = value;
        s_colmi_working_vitals.battery_valid = value <= 100;
        s_colmi_state = COLMI_CLIENT_READY;
        colmi_schedule_action(COLMI_ACTION_START_HR, 1500u);
        return;
    }
    if (cmd != COLMI_CMD_REALTIME_START || value == 0 || kind == 0) {
        return;
    }
    if (kind == COLMI_REALTIME_HEART_RATE && s_colmi_state == COLMI_CLIENT_READING_HR) {
        s_colmi_working_vitals.heart_rate_bpm = value;
        s_colmi_working_vitals.heart_rate_valid = value >= 30 && value <= 240;
        colmi_advance_after_realtime(kind);
    } else if (kind == COLMI_REALTIME_SPO2 && s_colmi_state == COLMI_CLIENT_READING_SPO2) {
        s_colmi_working_vitals.spo2_percent = value;
        s_colmi_working_vitals.spo2_valid = value >= 50 && value <= 100;
        colmi_advance_after_realtime(kind);
    } else if (kind == COLMI_REALTIME_HRV && s_colmi_state == COLMI_CLIENT_READING_HRV) {
        s_colmi_working_vitals.hrv_ms = value;
        s_colmi_working_vitals.hrv_valid = value > 0;
        colmi_advance_after_realtime(kind);
    }
}

static void colmi_client_advance_after_realtime_error(uint8_t kind)
{
    colmi_advance_after_realtime(kind);
}

static void colmi_run_scheduled_action(uint32_t now_ms)
{
    if (s_colmi_action == COLMI_ACTION_NONE || (int32_t)(now_ms - s_colmi_action_due_ms) < 0) {
        return;
    }
    const colmi_client_action_t action = s_colmi_action;
    s_colmi_action = COLMI_ACTION_NONE;
    s_colmi_action_due_ms = 0;

    if (!s_colmi_have_conn) {
        s_colmi_state = COLMI_CLIENT_ERROR;
        return;
    }

    esp_err_t err = ESP_OK;
    switch (action) {
        case COLMI_ACTION_BATTERY: {
            s_colmi_state = COLMI_CLIENT_READING_BATTERY;
            s_colmi_measure_started_ms = now_ms;
            uint8_t packet[COLMI_PACKET_LEN];
            colmi_make_packet(COLMI_CMD_BATTERY, NULL, 0, packet);
            err = colmi_send_packet(packet);
            ESP_LOGI(TAG, "colmi battery request %s", esp_err_to_name(err));
            break;
        }
        case COLMI_ACTION_START_HR:
            s_colmi_state = COLMI_CLIENT_READING_HR;
            s_colmi_measure_started_ms = now_ms;
            err = colmi_start_realtime(COLMI_REALTIME_HEART_RATE);
            ESP_LOGI(TAG, "colmi realtime start hr %s", esp_err_to_name(err));
            break;
        case COLMI_ACTION_START_SPO2:
            s_colmi_state = COLMI_CLIENT_READING_SPO2;
            s_colmi_measure_started_ms = now_ms;
            err = colmi_start_realtime(COLMI_REALTIME_SPO2);
            ESP_LOGI(TAG, "colmi realtime start spo2 %s", esp_err_to_name(err));
            break;
        case COLMI_ACTION_START_HRV:
            s_colmi_state = COLMI_CLIENT_READING_HRV;
            s_colmi_measure_started_ms = now_ms;
            err = colmi_start_realtime(COLMI_REALTIME_HRV);
            ESP_LOGI(TAG, "colmi realtime start hrv %s", esp_err_to_name(err));
            break;
        case COLMI_ACTION_START_RAW_IMU:
            s_colmi_state = COLMI_CLIENT_STREAMING_IMU;
            s_colmi_imu_have_sample = false;
            s_colmi_imu_stream_started_ms = now_ms;
            err = colmi_start_raw_imu();
            ESP_LOGI(TAG, "colmi raw IMU stream start %s", esp_err_to_name(err));
            break;
        case COLMI_ACTION_NONE:
        default:
            return;
    }
    if (err != ESP_OK) {
        s_colmi_state = COLMI_CLIENT_ERROR;
    }
}

static void colmi_handle_measure_timeout(uint32_t now_ms)
{
    const uint32_t timeout_ms = s_colmi_state == COLMI_CLIENT_READING_BATTERY ? 8000u : 45000u;
    if (s_colmi_state != COLMI_CLIENT_READING_BATTERY && s_colmi_state != COLMI_CLIENT_READING_HR &&
        s_colmi_state != COLMI_CLIENT_READING_SPO2 && s_colmi_state != COLMI_CLIENT_READING_HRV) {
        return;
    }
    if (s_colmi_measure_started_ms == 0 || (int32_t)(now_ms - (s_colmi_measure_started_ms + timeout_ms)) < 0) {
        return;
    }
    ESP_LOGW(TAG, "colmi measurement timeout state=%s", colmi_state_name(s_colmi_state));
    if (s_colmi_state == COLMI_CLIENT_READING_BATTERY) {
        s_colmi_state = COLMI_CLIENT_READY;
        colmi_schedule_action(COLMI_ACTION_START_HR, 500u);
    } else if (s_colmi_state == COLMI_CLIENT_READING_HR) {
        colmi_advance_after_realtime(COLMI_REALTIME_HEART_RATE);
    } else if (s_colmi_state == COLMI_CLIENT_READING_SPO2) {
        colmi_advance_after_realtime(COLMI_REALTIME_SPO2);
    } else if (s_colmi_state == COLMI_CLIENT_READING_HRV) {
        colmi_advance_after_realtime(COLMI_REALTIME_HRV);
    }
}

static void colmi_handle_packet(const uint8_t *data, size_t len)
{
    if (data == NULL || len != COLMI_PACKET_LEN) {
        return;
    }
    memcpy(s_colmi_last_packet, data, COLMI_PACKET_LEN);
    s_colmi_have_packet = true;
    s_colmi_last_packet_ms = ble_now_ms();
    if (!colmi_packet_checksum_ok(data)) {
        ESP_LOGW(TAG, "colmi packet checksum mismatch cmd=0x%02x", data[0]);
    }
    if (data[0] == COLMI_CMD_BATTERY) {
        colmi_client_advance_after_packet(data[0], 0, data[1]);
    } else if (data[0] == COLMI_CMD_RAW_DATA) {
        if (s_colmi_state == COLMI_CLIENT_STREAMING_IMU) {
            colmi_handle_raw_imu(data);
        }
    } else if (data[0] == COLMI_CMD_REALTIME_START) {
        if (data[2] != 0) {
            ESP_LOGW(TAG, "colmi realtime kind=%u error=%u", data[1], data[2]);
            colmi_client_advance_after_realtime_error(data[1]);
            return;
        }
        colmi_client_advance_after_packet(data[0], data[1], data[3]);
    }
}

static int colmi_subscribe_cb(uint16_t conn_handle,
                              const struct ble_gatt_error *error,
                              struct ble_gatt_attr *attr,
                              void *arg)
{
    (void)conn_handle;
    (void)attr;
    (void)arg;
    if (error != NULL && error->status != 0) {
        ESP_LOGW(TAG, "colmi subscribe failed status=%u", (unsigned)error->status);
        s_colmi_state = COLMI_CLIENT_ERROR;
        return 0;
    }
    s_colmi_state = COLMI_CLIENT_READY;
    if (s_lunasay_ring_control_active) {
        colmi_schedule_action(COLMI_ACTION_START_RAW_IMU, 120u);
    } else {
        colmi_schedule_action(COLMI_ACTION_BATTERY, 500u);
    }
    return 0;
}

static void colmi_write_ccc(uint16_t conn_handle, uint16_t ccc_handle, const char *source)
{
    const uint8_t notify_on[2] = {1, 0};
    const int rc = ble_gattc_write_flat(conn_handle,
                                        ccc_handle,
                                        notify_on,
                                        sizeof(notify_on),
                                        colmi_subscribe_cb,
                                        NULL);
    if (rc != 0) {
        ESP_LOGW(TAG, "colmi subscribe write failed source=%s handle=%u rc=%d", source, (unsigned)ccc_handle, rc);
        s_colmi_state = COLMI_CLIENT_ERROR;
    } else {
        ESP_LOGI(TAG, "colmi subscribe write source=%s handle=%u", source, (unsigned)ccc_handle);
    }
}

static int colmi_dsc_cb(uint16_t conn_handle,
                        const struct ble_gatt_error *error,
                        uint16_t chr_val_handle,
                        const struct ble_gatt_dsc *dsc,
                        void *arg)
{
    (void)chr_val_handle;
    (void)arg;
    if (error == NULL) {
        return 0;
    }
    if (error->status == 0 && dsc != NULL && ble_uuid_u16(&dsc->uuid.u) == BLE_GATT_DSC_CLT_CFG_UUID16) {
        s_colmi_ccc_handle = dsc->handle;
        return 0;
    }
    if (error->status == BLE_HS_EDONE) {
        if (s_colmi_ccc_handle == 0) {
            ESP_LOGW(TAG, "colmi tx ccc not found");
            s_colmi_state = COLMI_CLIENT_ERROR;
            return 0;
        }
        colmi_write_ccc(conn_handle, s_colmi_ccc_handle, "discovery");
    } else {
        ESP_LOGW(TAG, "colmi dsc discovery failed status=%u", (unsigned)error->status);
        s_colmi_state = COLMI_CLIENT_ERROR;
    }
    return 0;
}

static int colmi_tx_chr_cb(uint16_t conn_handle,
                           const struct ble_gatt_error *error,
                           const struct ble_gatt_chr *chr,
                           void *arg)
{
    (void)arg;
    if (error == NULL) {
        return 0;
    }
    if (error->status == 0 && chr != NULL) {
        s_colmi_tx_handle = chr->val_handle;
        return 0;
    }
    if (error->status == BLE_HS_EDONE) {
        if (s_colmi_tx_handle == 0) {
            ESP_LOGW(TAG, "colmi tx characteristic not found");
            s_colmi_state = COLMI_CLIENT_ERROR;
            return 0;
        }
        s_colmi_state = COLMI_CLIENT_DISC_CCC;
        const int rc = ble_gattc_disc_all_dscs(conn_handle,
                                               s_colmi_tx_handle + 1,
                                               s_colmi_svc_end,
                                               colmi_dsc_cb,
                                               NULL);
        if (rc != 0) {
            s_colmi_ccc_handle = s_colmi_tx_handle + 1;
            ESP_LOGW(TAG, "colmi ccc discovery start failed rc=%d; trying direct handle=%u",
                     rc,
                     (unsigned)s_colmi_ccc_handle);
            colmi_write_ccc(conn_handle, s_colmi_ccc_handle, "direct");
        }
    } else {
        ESP_LOGW(TAG, "colmi tx discovery failed status=%u", (unsigned)error->status);
        s_colmi_state = COLMI_CLIENT_ERROR;
    }
    return 0;
}

static int colmi_rx_chr_cb(uint16_t conn_handle,
                           const struct ble_gatt_error *error,
                           const struct ble_gatt_chr *chr,
                           void *arg)
{
    (void)arg;
    if (error == NULL) {
        return 0;
    }
    if (error->status == 0 && chr != NULL) {
        s_colmi_rx_handle = chr->val_handle;
        return 0;
    }
    if (error->status == BLE_HS_EDONE) {
        if (s_colmi_rx_handle == 0) {
            ESP_LOGW(TAG, "colmi rx characteristic not found");
            s_colmi_state = COLMI_CLIENT_ERROR;
            return 0;
        }
        s_colmi_state = COLMI_CLIENT_DISC_TX;
        const int rc = ble_gattc_disc_chrs_by_uuid(conn_handle,
                                                   s_colmi_svc_start,
                                                   s_colmi_svc_end,
                                                   &COLMI_UART_TX_UUID.u,
                                                   colmi_tx_chr_cb,
                                                   NULL);
        if (rc != 0) {
            ESP_LOGW(TAG, "colmi tx discovery start failed rc=%d", rc);
            s_colmi_state = COLMI_CLIENT_ERROR;
        }
    } else {
        ESP_LOGW(TAG, "colmi rx discovery failed status=%u", (unsigned)error->status);
        s_colmi_state = COLMI_CLIENT_ERROR;
    }
    return 0;
}

static int colmi_svc_cb(uint16_t conn_handle,
                        const struct ble_gatt_error *error,
                        const struct ble_gatt_svc *service,
                        void *arg)
{
    (void)arg;
    if (error == NULL) {
        return 0;
    }
    if (error->status == 0 && service != NULL) {
        s_colmi_svc_start = service->start_handle;
        s_colmi_svc_end = service->end_handle;
        return 0;
    }
    if (error->status == BLE_HS_EDONE) {
        if (s_colmi_svc_start == 0 || s_colmi_svc_end == 0) {
            ESP_LOGW(TAG, "colmi uart service not found");
            s_colmi_state = COLMI_CLIENT_ERROR;
            return 0;
        }
        s_colmi_state = COLMI_CLIENT_DISC_RX;
        const int rc = ble_gattc_disc_chrs_by_uuid(conn_handle,
                                                   s_colmi_svc_start,
                                                   s_colmi_svc_end,
                                                   &COLMI_UART_RX_UUID.u,
                                                   colmi_rx_chr_cb,
                                                   NULL);
        if (rc != 0) {
            ESP_LOGW(TAG, "colmi rx discovery start failed rc=%d", rc);
            s_colmi_state = COLMI_CLIENT_ERROR;
        }
    } else {
        ESP_LOGW(TAG, "colmi service discovery failed status=%u", (unsigned)error->status);
        s_colmi_state = COLMI_CLIENT_ERROR;
    }
    return 0;
}

static void colmi_client_on_ring_found(const ble_addr_t *addr, uint8_t event_type, int8_t rssi)
{
    if (addr == NULL || !s_colmi_want_scan || s_colmi_state != COLMI_CLIENT_SCAN) {
        return;
    }
    char addr_text[24];
    ble_format_addr(addr->val, addr_text, sizeof(addr_text));
    if (!colmi_adv_event_connectable(event_type)) {
        ESP_LOGI(TAG, "colmi ring seen addr=%s type=%u rssi=%d; waiting for connectable adv", addr_text,
                 (unsigned)event_type, rssi);
        return;
    }
    s_colmi_addr = *addr;
    s_colmi_want_scan = false;
    s_colmi_connect_started = false;
    s_colmi_connect_after_ms = ble_now_ms() + 250u;
    s_colmi_state = COLMI_CLIENT_CONNECTING;
    ESP_LOGI(TAG, "colmi ring connect target addr=%s type=%u rssi=%d", addr_text, (unsigned)event_type, rssi);
    if (s_scanning) {
        const int rc = ble_gap_disc_cancel();
        ESP_LOGI(TAG, "colmi scan cancel rc=%d", rc);
        s_scanning = false;
    }
}

static esp_err_t colmi_client_start(void)
{
    if (!s_started || !s_synced || !s_enabled || !s_paired_ring_id_set) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_colmi_have_conn) {
        return ESP_OK;
    }
    memset(&s_colmi_addr, 0, sizeof(s_colmi_addr));
    memset(&s_colmi_working_vitals, 0, sizeof(s_colmi_working_vitals));
    s_colmi_conn_handle = 0;
    s_colmi_svc_start = 0;
    s_colmi_svc_end = 0;
    s_colmi_rx_handle = 0;
    s_colmi_tx_handle = 0;
    s_colmi_ccc_handle = 0;
    s_colmi_started_ms = ble_now_ms();
    s_colmi_last_packet_ms = 0;
    s_colmi_connect_after_ms = 0;
    s_colmi_measure_started_ms = 0;
    s_colmi_action_due_ms = 0;
    s_colmi_action = COLMI_ACTION_NONE;
    s_colmi_have_packet = false;
    s_colmi_connect_started = false;
    s_colmi_want_scan = true;
    s_colmi_state = COLMI_CLIENT_SCAN;
    if (s_advertising) {
        (void)ble_gap_adv_stop();
        s_advertising = false;
    }
    return faculty175_ble_scan_start(15000u);
}

static void colmi_client_tick(uint32_t now_ms)
{
    if (s_colmi_state == COLMI_CLIENT_IDLE || s_colmi_state == COLMI_CLIENT_DONE || s_colmi_state == COLMI_CLIENT_ERROR) {
        return;
    }
    colmi_run_scheduled_action(now_ms);
    colmi_handle_measure_timeout(now_ms);
    if (s_colmi_state == COLMI_CLIENT_CONNECTING && s_scanning &&
        s_colmi_connect_after_ms != 0 && (int32_t)(now_ms - (s_colmi_connect_after_ms + 1000u)) >= 0) {
        ESP_LOGW(TAG, "colmi forcing scan state clear before connect retry");
        s_scanning = false;
    }
    if (s_colmi_state == COLMI_CLIENT_CONNECTING && !s_scanning && !s_colmi_connect_started &&
        (s_colmi_connect_after_ms == 0 || (int32_t)(now_ms - s_colmi_connect_after_ms) >= 0)) {
        struct ble_gap_conn_params params = {};
        params.scan_itvl = 0x40;
        params.scan_window = 0x30;
        params.itvl_min = 24;
        params.itvl_max = 40;
        params.latency = 0;
        params.supervision_timeout = 400;
        params.min_ce_len = 0;
        params.max_ce_len = 0;
        const int rc = ble_gap_connect(s_own_addr_type,
                                       &s_colmi_addr,
                                       12000,
                                       &params,
                                       ble_gap_event,
                                       NULL);
        if (rc != 0) {
            ESP_LOGW(TAG, "colmi connect start failed rc=%d", rc);
            if (rc == BLE_HS_EALREADY || rc == BLE_HS_EBUSY || rc == BLE_HS_EAGAIN) {
                s_colmi_connect_after_ms = now_ms + 350u;
            } else {
                s_colmi_state = COLMI_CLIENT_ERROR;
            }
        } else {
            s_colmi_connect_started = true;
            ESP_LOGI(TAG, "colmi connect started");
        }
        return;
    }
    if ((int32_t)(now_ms - s_colmi_started_ms) >= 140000) {
        ESP_LOGW(TAG, "colmi client timeout state=%s", colmi_state_name(s_colmi_state));
        s_colmi_state = COLMI_CLIENT_ERROR;
        s_scanning = false;
        if (s_colmi_have_conn) {
            (void)ble_gap_terminate(s_colmi_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        }
    }
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
    const cJSON *relationship =
        cJSON_GetObjectItemCaseSensitive(root, "relationship");
    if (err == ESP_OK && cJSON_IsObject(relationship)) {
        const cJSON *target_slot =
            cJSON_GetObjectItemCaseSensitive(relationship, "targetSlot");
        const cJSON *date =
            cJSON_GetObjectItemCaseSensitive(relationship, "date");
        if (cJSON_IsNumber(target_slot) &&
            !faculty175_charts_set_active_slot(target_slot->valueint)) {
            err = ESP_ERR_INVALID_ARG;
        }
        if (err == ESP_OK && cJSON_IsString(date) &&
            date->valuestring != NULL) {
            err = faculty175_relationship_weather_select_date(
                date->valuestring);
        }
    }
    const cJSON *cycle = cJSON_GetObjectItemCaseSensitive(root, "cycle");
    if (err == ESP_OK && cJSON_IsObject(cycle)) {
        const cJSON *start = cJSON_GetObjectItemCaseSensitive(cycle, "startDate");
        const cJSON *cycle_length = cJSON_GetObjectItemCaseSensitive(cycle, "length");
        const cJSON *period_length = cJSON_GetObjectItemCaseSensitive(cycle, "periodLength");
        const cJSON *bleeding_start = cJSON_GetObjectItemCaseSensitive(cycle, "bleedingStarted");
        const cJSON *bleeding_stop = cJSON_GetObjectItemCaseSensitive(cycle, "bleedingStopped");
        if (cJSON_IsString(start) && start->valuestring != NULL) {
            err = faculty175_cycle_health_set_start(start->valuestring);
        }
        if (err == ESP_OK && cJSON_IsNumber(cycle_length) && cJSON_IsNumber(period_length)) {
            err = faculty175_cycle_health_set_lengths((uint8_t)cycle_length->valueint,
                                                      (uint8_t)period_length->valueint);
        }
        if (err == ESP_OK && cJSON_IsTrue(bleeding_start)) {
            err = faculty175_cycle_health_mark_bleeding_started_today();
        }
        if (err == ESP_OK && cJSON_IsTrue(bleeding_stop)) {
            err = faculty175_cycle_health_mark_bleeding_stopped_today();
        }
    }
    const cJSON *personal = cJSON_GetObjectItemCaseSensitive(root, "personal");
    if (err == ESP_OK && cJSON_IsObject(personal)) {
        faculty175_personal_settings_t settings = {};
        const cJSON *name = cJSON_GetObjectItemCaseSensitive(personal, "name");
        const cJSON *pronouns = cJSON_GetObjectItemCaseSensitive(personal, "pronouns");
        const cJSON *birth_date = cJSON_GetObjectItemCaseSensitive(personal, "birthDate");
        const cJSON *birth_time = cJSON_GetObjectItemCaseSensitive(personal, "birthTime");
        const cJSON *birthplace = cJSON_GetObjectItemCaseSensitive(personal, "birthplace");
        if (cJSON_IsString(name) && name->valuestring != NULL) faculty175_strlcpy(settings.display_name, name->valuestring, sizeof(settings.display_name));
        if (cJSON_IsString(pronouns) && pronouns->valuestring != NULL) faculty175_strlcpy(settings.pronouns, pronouns->valuestring, sizeof(settings.pronouns));
        if (cJSON_IsString(birth_date) && birth_date->valuestring != NULL) faculty175_strlcpy(settings.birth_date, birth_date->valuestring, sizeof(settings.birth_date));
        if (cJSON_IsString(birth_time) && birth_time->valuestring != NULL) faculty175_strlcpy(settings.birth_time, birth_time->valuestring, sizeof(settings.birth_time));
        if (cJSON_IsString(birthplace) && birthplace->valuestring != NULL) faculty175_strlcpy(settings.birthplace, birthplace->valuestring, sizeof(settings.birthplace));
        err = faculty175_personal_settings_save(&settings);
    }
    const cJSON *privacy = cJSON_GetObjectItemCaseSensitive(root, "privacy");
    if (err == ESP_OK && cJSON_IsObject(privacy)) {
        const cJSON *clear_history =
            cJSON_GetObjectItemCaseSensitive(privacy, "clearReadingHistory");
        if (cJSON_IsTrue(clear_history)) {
            err = faculty175_voice_clear_lunasay_reading_history();
            if (err == ESP_OK) {
                err = faculty175_research_clear_local_feedback();
            }
        }
    }
    const cJSON *research = cJSON_GetObjectItemCaseSensitive(root, "research");
    if (err == ESP_OK && cJSON_IsObject(research)) {
        const cJSON *consent = cJSON_GetObjectItemCaseSensitive(research, "consent");
        const cJSON *version = cJSON_GetObjectItemCaseSensitive(research, "consentVersion");
        if (!cJSON_IsBool(consent)) {
            err = ESP_ERR_INVALID_ARG;
        } else {
            err = faculty175_research_set_consent(
                cJSON_IsTrue(consent),
                cJSON_IsString(version) && version->valuestring != NULL
                    ? version->valuestring
                    : "research-v2");
        }
    }
    const cJSON *mood = cJSON_GetObjectItemCaseSensitive(root, "mood");
    if (err == ESP_OK && cJSON_IsObject(mood)) {
        const cJSON *label = cJSON_GetObjectItemCaseSensitive(mood, "label");
        const cJSON *check_in = cJSON_GetObjectItemCaseSensitive(mood, "checkIn");
        if (!cJSON_IsString(label) || label->valuestring == NULL ||
            !faculty175_face_psych_state_set_mood(
                label->valuestring, cJSON_IsTrue(check_in))) {
            err = ESP_ERR_INVALID_ARG;
        }
    }
    const cJSON *reflection =
        cJSON_GetObjectItemCaseSensitive(root, "reflection");
    if (err == ESP_OK && cJSON_IsObject(reflection)) {
        const cJSON *face =
            cJSON_GetObjectItemCaseSensitive(reflection, "face");
        const cJSON *rating =
            cJSON_GetObjectItemCaseSensitive(reflection, "rating");
        const cJSON *reading_date =
            cJSON_GetObjectItemCaseSensitive(reflection, "readingDate");
        if (!cJSON_IsString(face) || face->valuestring == NULL ||
            !cJSON_IsString(rating) || rating->valuestring == NULL ||
            !cJSON_IsString(reading_date) ||
            reading_date->valuestring == NULL) {
            err = ESP_ERR_INVALID_ARG;
        } else {
            err = faculty175_research_record_feedback(
                face->valuestring,
                rating->valuestring,
                reading_date->valuestring);
        }
    }
    const cJSON *spotify = cJSON_GetObjectItemCaseSensitive(root, "spotify");
    if (err == ESP_OK && cJSON_IsObject(spotify)) {
        const cJSON *client_id = cJSON_GetObjectItemCaseSensitive(spotify, "clientId");
        const cJSON *refresh_token = cJSON_GetObjectItemCaseSensitive(spotify, "refreshToken");
        if (!cJSON_IsString(client_id) || client_id->valuestring == NULL ||
            !cJSON_IsString(refresh_token) || refresh_token->valuestring == NULL) {
            err = ESP_ERR_INVALID_ARG;
        } else {
            err = faculty175_spotify_configure(client_id->valuestring, refresh_token->valuestring);
            if (err == ESP_OK) {
                err = faculty175_faces_set_enabled(FACULTY175_FACE_SPOTIFY, true);
            }
            if (err == ESP_OK) {
                err = faculty175_faces_set_navigation_enabled(FACULTY175_FACE_SPOTIFY, true);
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
        faculty175_cycle_health_status_t cycle = {};
        (void)faculty175_cycle_health_status(&cycle);
        faculty175_ring_vitals_t ring_vitals = {};
        const bool have_ring_vitals = faculty175_ring_latest_vitals(&ring_vitals);
        char body[512];
        snprintf(body,
                 sizeof(body),
                 "{\"ok\":true,\"tz\":\"%s\",\"epoch\":%lld,\"location\":{\"valid\":%s,\"lat\":%.5f,\"lon\":%.5f},"
                 "\"cycle\":{\"configured\":%s,\"day\":%u,\"length\":%u,\"periodLength\":%u,\"phase\":\"%s\",\"startDate\":\"%s\"},"
                 "\"ring\":{\"available\":%s,\"heartRate\":%u,\"hrv\":%u,\"spo2\":%u},"
                 "\"wifi\":{\"ap\":%s,\"travelRouter\":%s,\"ssid\":\"%s\",\"url\":\"%s\"}}",
                 t.tz,
                 (long long)t.epoch,
                 loc_ok ? "true" : "false",
                 loc_ok ? loc.lat_deg : 0.0,
                 loc_ok ? loc.lon_deg : 0.0,
                 cycle.configured ? "true" : "false",
                 cycle.day,
                 cycle.cycle_length,
                 cycle.period_length,
                 faculty175_cycle_health_phase_label(cycle.phase),
                 cycle.start_date,
                 have_ring_vitals ? "true" : "false",
                 ring_vitals.heart_rate_valid ? ring_vitals.heart_rate_bpm : 0,
                 ring_vitals.hrv_valid ? ring_vitals.hrv_ms : 0,
                 ring_vitals.spo2_valid ? ring_vitals.spo2_percent : 0,
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

static bool ble_json_escape(char *out,
                            size_t cap,
                            const char *value)
{
    if (out == NULL || cap == 0 || value == NULL) {
        return false;
    }
    size_t used = 0;
    for (const unsigned char *p = (const unsigned char *)value;
         *p != '\0';
         ++p) {
        const char *escape = NULL;
        if (*p == '"' || *p == '\\') {
            escape = *p == '"' ? "\\\"" : "\\\\";
        }
        if (escape != NULL) {
            if (used + 2 >= cap) return false;
            out[used++] = escape[0];
            out[used++] = escape[1];
        } else {
            if (used + 1 >= cap) return false;
            out[used++] = *p < 0x20 ? ' ' : (char)*p;
        }
    }
    out[used] = '\0';
    return true;
}

static int ble_health_json_access(uint16_t conn_handle,
                                  uint16_t attr_handle,
                                  struct ble_gatt_access_ctxt *ctxt,
                                  void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;
    if (ctxt == NULL || ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    faculty175_pmu_status_t power = {};
    const bool have_power = faculty175_pmu_status(&power);
    faculty175_ota_status_t ota = {};
    faculty175_ota_get_status(&ota);
    const esp_app_desc_t *app = esp_app_get_description();

    char firmware[72] = {};
    /* Keep the complete attribute under the 512-byte BLE value boundary even
     * when every byte in the diagnostic needs JSON escaping. */
    char ota_last_raw[25] = {};
    char ota_last[50] = {};
    snprintf(ota_last_raw, sizeof(ota_last_raw), "%.24s", ota.last);
    if (!ble_json_escape(firmware,
                         sizeof(firmware),
                         app != NULL ? app->version : "") ||
        !ble_json_escape(ota_last, sizeof(ota_last), ota_last_raw)) {
        return BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    const int len = snprintf(
        s_health_json,
        sizeof(s_health_json),
        "{\"device\":{\"firmware\":\"%s\",\"uptimeMs\":%llu,"
        "\"battery\":{\"available\":%s,\"present\":%s,\"percent\":%d,"
        "\"millivolts\":%u,\"charging\":%s,\"usbPower\":%s},"
        "\"ble\":{\"enabled\":%s,\"advertising\":%s},"
        "\"ota\":{\"active\":%s,\"autoStarted\":%s,\"paused\":%s,"
        "\"networkReady\":%s,\"heapReady\":%s,\"intervalSeconds\":%u,"
        "\"lastPollUptimeMs\":%u,\"last\":\"%s\"},"
        "\"capabilities\":{\"clearReadingHistory\":true}}}",
        firmware,
        (unsigned long long)(esp_timer_get_time() / 1000),
        have_power ? "true" : "false",
        have_power && power.battery_present ? "true" : "false",
        have_power ? power.battery_percent : -1,
        have_power ? power.battery_mv : 0,
        have_power && power.charging ? "true" : "false",
        have_power && power.vbus_in ? "true" : "false",
        s_enabled ? "true" : "false",
        s_advertising ? "true" : "false",
        ota.active ? "true" : "false",
        ota.auto_started ? "true" : "false",
        ota.auto_paused ? "true" : "false",
        ota.network_ready ? "true" : "false",
        ota.heap_ready ? "true" : "false",
        ota.auto_interval_s,
        ota.last_poll_uptime_ms,
        ota_last);
    if (len <= 0 || (size_t)len >= sizeof(s_health_json)) {
        return BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return os_mbuf_append(ctxt->om, s_health_json, (size_t)len) == 0
        ? 0
        : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static bool ble_json_append(char *out,
                            size_t cap,
                            size_t *used,
                            const char *format,
                            ...)
{
    if (out == NULL || used == NULL || format == NULL || *used >= cap) {
        return false;
    }
    va_list args;
    va_start(args, format);
    const int wrote = vsnprintf(out + *used, cap - *used, format, args);
    va_end(args);
    if (wrote < 0 || (size_t)wrote >= cap - *used) {
        return false;
    }
    *used += (size_t)wrote;
    return true;
}

static int ble_state_json_access(uint16_t conn_handle,
                                 uint16_t attr_handle,
                                 struct ble_gatt_access_ctxt *ctxt,
                                 void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;
    if (ctxt == NULL || ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    faculty175_research_status_t research = {};
    faculty175_research_status(&research);
    uint8_t arousal = 0;
    uint8_t valence = 0;
    faculty175_face_psych_state_mood_values(&arousal, &valence);
    faculty175_relationship_weather_snapshot_t relationship = {};
    const bool relationship_available =
        faculty175_relationship_weather_snapshot(&relationship);
    char primary_name[65] = {};
    char target_name[65] = {};
    if (relationship_available) {
        (void)ble_json_escape(primary_name,
                              sizeof(primary_name),
                              relationship.primary_name);
        (void)ble_json_escape(target_name,
                              sizeof(target_name),
                              relationship.target_name);
    }
    char arc[FACULTY175_RELATIONSHIP_ARC_DAYS + 1] = {};
    if (relationship_available) {
        for (int day = 0;
             day < FACULTY175_RELATIONSHIP_ARC_DAYS;
             ++day) {
            arc[day] = (char)('0' + relationship.arc[day]);
        }
    }
    char resonance[320] = "{}";
    (void)faculty175_research_resonance_json(resonance, sizeof(resonance));
    size_t len = 0;
    bool ok = ble_json_append(
        s_state_json,
        sizeof(s_state_json),
        &len,
        "{\"mood\":{\"label\":\"%s\",\"arousal\":%u,\"valence\":%u},"
        "\"research\":{\"consent\":%s,\"consentVersion\":\"%s\","
        "\"pending\":%s,\"status\":\"%s\","
        "\"localFeedbackCount\":%u,"
        "\"lastFeedback\":{\"face\":\"%s\",\"rating\":\"%s\"}},"
        "\"resonance\":%s,"
        "\"relationship\":{\"available\":%s,\"selectedDate\":\"%s\","
        "\"offsetDays\":%d,\"activeSlot\":%d,\"primaryName\":\"%s\","
        "\"targetName\":\"%s\",\"condition\":%d,\"arc\":\"%s\","
        "\"profiles\":[",
        faculty175_face_psych_state_mood_label(),
        arousal,
        valence,
        research.consent_enabled ? "true" : "false",
        research.consent_version,
        research.pending ? "true" : "false",
        faculty175_research_state_label(research.state),
        research.local_feedback_count,
        research.last_feedback_face,
        research.last_feedback_rating,
        resonance,
        relationship_available ? "true" : "false",
        relationship_available ? relationship.selected_date : "",
        relationship_available ? relationship.offset_days : 0,
        faculty175_charts_active_slot(),
        primary_name,
        target_name,
        relationship_available ? relationship.arc[0] :
            FACULTY175_RELATIONSHIP_CHANGEABLE,
        arc);
    faculty175_charts_ensure_family_seed();
    bool first = true;
    for (int slot = 0; ok && slot < FACULTY175_CHART_PROFILE_SLOTS; ++slot) {
        faculty175_birth_chart_t profile = {};
        if (!faculty175_charts_profile_get(slot, &profile) || !profile.valid) {
            continue;
        }
        char name[65] = {};
        char role[32] = {};
        if (!ble_json_escape(name, sizeof(name), profile.name) ||
            !ble_json_escape(role,
                             sizeof(role),
                             faculty175_charts_role_label(profile.role))) {
            ok = false;
            break;
        }
        ok = ble_json_append(
            s_state_json,
            sizeof(s_state_json),
            &len,
            "%s{\"slot\":%d,\"name\":\"%s\",\"role\":\"%s\"}",
            first ? "" : ",",
            slot,
            name,
            role);
        first = false;
    }
    ok = ok && ble_json_append(
        s_state_json,
        sizeof(s_state_json),
        &len,
        "]}}");
    if (!ok) {
        return BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return os_mbuf_append(ctxt->om, s_state_json, len) == 0
        ? 0
        : BLE_ATT_ERR_INSUFFICIENT_RES;
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
    if (!s_started || !s_synced || !s_enabled || s_power_scenario_suspended) {
        return ESP_ERR_INVALID_STATE;
    }

    ble_astrolabe_mfg_t mfg = ble_build_mfg_payload();
    struct ble_hs_adv_fields fields = {};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.mfg_data = (const uint8_t *)&mfg;
    fields.mfg_data_len = sizeof(mfg);

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGW(TAG, "adv mfg payload disabled rc=%d", rc);
        fields.mfg_data = NULL;
        fields.mfg_data_len = 0;
        rc = ble_gap_adv_set_fields(&fields);
        if (rc != 0) {
            s_advertising = false;
            ESP_LOGE(TAG, "adv fields failed rc=%d", rc);
            return ESP_FAIL;
        }
    }

    struct ble_hs_adv_fields rsp = {};
    const char *name = ble_svc_gap_device_name();
    rsp.name = (uint8_t *)name;
    rsp.name_len = strlen(name);
    rsp.name_is_complete = 1;
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
        ESP_LOGW(TAG, "scan response disabled rc=%d", rc);
        (void)ble_gap_adv_rsp_set_data(NULL, 0);
    }

    struct ble_gap_adv_params params = {};
    params.conn_mode = BLE_GAP_CONN_MODE_NON;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    params.itvl_min = BLE_GAP_ADV_ITVL_MS(1000);
    params.itvl_max = BLE_GAP_ADV_ITVL_MS(1200);
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
            if (s_colmi_state == COLMI_CLIENT_CONNECTING) {
                if (event->connect.status == 0) {
                    s_colmi_have_conn = true;
                    s_colmi_conn_handle = event->connect.conn_handle;
                    s_colmi_state = COLMI_CLIENT_DISC_SERVICE;
                    const int rc = ble_gattc_disc_svc_by_uuid(s_colmi_conn_handle,
                                                              &COLMI_UART_SERVICE_UUID.u,
                                                              colmi_svc_cb,
                                                              NULL);
                    if (rc != 0) {
                        ESP_LOGW(TAG, "colmi service discovery start failed rc=%d", rc);
                        s_colmi_state = COLMI_CLIENT_ERROR;
                    }
                } else {
                    ESP_LOGW(TAG, "colmi connect failed status=%d", event->connect.status);
                    s_colmi_state = COLMI_CLIENT_ERROR;
                    (void)ble_advertise();
                }
            } else if (event->connect.status != 0) {
                (void)ble_advertise();
            }
            break;
        case BLE_GAP_EVENT_DISCONNECT:
            if (s_colmi_have_conn && event->disconnect.conn.conn_handle == s_colmi_conn_handle) {
                s_colmi_have_conn = false;
                s_colmi_conn_handle = 0;
                if (s_lunasay_ring_control_active) {
                    s_colmi_state = COLMI_CLIENT_IDLE;
                    s_lunasay_ring_retry_ms = ble_now_ms() + 1200u;
                }
            }
            s_advertising = false;
            if (!s_scanning) {
                (void)ble_advertise();
            }
            break;
        case BLE_GAP_EVENT_ADV_COMPLETE:
            s_advertising = false;
            if (!s_scanning) {
                (void)ble_advertise();
            }
            break;
        case BLE_GAP_EVENT_DISC: {
            struct ble_hs_adv_fields fields = {};
            const uint32_t now = ble_now_ms();
            const int parse_rc = ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data);
            if (parse_rc == 0) {
                if (s_raw_scan_log) {
                    char name[FACULTY175_BLE_PEER_NAME_MAX];
                    char addr[24];
                    ble_copy_text(name, sizeof(name), fields.name, fields.name_len);
                    ble_format_addr(event->disc.addr.val, addr, sizeof(addr));
                    printf("ble_raw: addr=%s id=%04x type=%u rssi=%d name=\"%s\" mfg=%u svc16=%u svc128=%u len=%u\n",
                           addr,
                           ble_addr_hash16(event->disc.addr.val),
                           event->disc.addr.type,
                           event->disc.rssi,
                           name,
                           (unsigned)fields.mfg_data_len,
                           (unsigned)fields.num_uuids16,
                           (unsigned)fields.num_uuids128,
                           (unsigned)event->disc.length_data);
                }
                ble_peer_store(&event->disc.addr, &fields, event->disc.rssi, now);
            } else if (s_raw_scan_log) {
                char addr[24];
                char hex[32];
                ble_format_addr(event->disc.addr.val, addr, sizeof(addr));
                ble_format_hex(event->disc.data, event->disc.length_data, hex, sizeof(hex));
                printf("ble_raw: addr=%s id=%04x type=%u rssi=%d parse_rc=%d len=%u data=%s\n",
                       addr,
                       ble_addr_hash16(event->disc.addr.val),
                       event->disc.addr.type,
                       event->disc.rssi,
                       parse_rc,
                       (unsigned)event->disc.length_data,
                       hex);
            }
            if (parse_rc != 0) {
                ble_store_raw_ring_fallback(&event->disc.addr, event->disc.rssi, now);
            }
            if (s_colmi_want_scan && s_paired_ring_id_set &&
                ble_addr_hash16(event->disc.addr.val) == s_paired_ring_id) {
                colmi_client_on_ring_found(&event->disc.addr, event->disc.event_type, event->disc.rssi);
            }
            break;
        }
        case BLE_GAP_EVENT_DISC_COMPLETE:
            s_scanning = false;
            s_raw_scan_log = false;
            s_raw_scan_until_ms = 0;
            s_next_scan_ms = ble_now_ms() + 2500u;
            if (!s_colmi_want_scan && s_colmi_state == COLMI_CLIENT_CONNECTING) {
                s_colmi_connect_after_ms = ble_now_ms() + 250u;
            } else {
                if (s_colmi_want_scan && s_colmi_state == COLMI_CLIENT_SCAN) {
                    s_colmi_want_scan = false;
                    s_colmi_state = COLMI_CLIENT_ERROR;
                }
                (void)ble_advertise();
            }
            break;
        case BLE_GAP_EVENT_NOTIFY_RX: {
            if (s_colmi_have_conn && event->notify_rx.conn_handle == s_colmi_conn_handle &&
                event->notify_rx.attr_handle == s_colmi_tx_handle) {
                uint8_t packet[COLMI_PACKET_LEN];
                if (OS_MBUF_PKTLEN(event->notify_rx.om) == COLMI_PACKET_LEN &&
                    os_mbuf_copydata(event->notify_rx.om, 0, COLMI_PACKET_LEN, packet) == 0) {
                    colmi_handle_packet(packet, sizeof(packet));
                }
            }
            break;
        }
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

    bool configured_enabled = true;
    esp_err_t err = ble_nvs_get_enabled(&configured_enabled);
    s_enabled = s_power_test_active ? true : configured_enabled;
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
    if (duration_ms > 20000u) {
        duration_ms = 20000u;
    }
    if (!s_raw_scan_log && s_colmi_state != COLMI_CLIENT_SCAN && duration_ms > 6000u) {
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
    if (now_ms == 0) {
        now_ms = ble_now_ms();
    }
    colmi_client_tick(now_ms);
    /* LunaSay's paired ring owns the central link while it is used as an
     * input device.  Do not interrupt its raw-IMU stream with background
     * radar scans. */
    if (s_lunasay_ring_control_active) {
        return;
    }
    if (!s_enabled || !s_started || !s_synced || s_scanning) {
        return;
    }
    if (s_colmi_state != COLMI_CLIENT_IDLE && s_colmi_state != COLMI_CLIENT_DONE && s_colmi_state != COLMI_CLIENT_ERROR) {
        return;
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

void faculty175_ble_lunasay_ring_tick(uint32_t now_ms)
{
    if (now_ms == 0) {
        now_ms = ble_now_ms();
    }
    s_lunasay_ring_control_active = s_enabled && s_started && s_synced && s_paired_ring_id_set;
    if (!s_lunasay_ring_control_active) {
        s_lunasay_ring_near = false;
        return;
    }

    if (s_lunasay_ring_near && now_ms - s_lunasay_ring_near_ms > 4000u) {
        s_lunasay_ring_near = false;
        ESP_LOGI(TAG, "lunasay ring no longer nearby");
    }

    if (!s_colmi_have_conn &&
        (s_colmi_state == COLMI_CLIENT_IDLE || s_colmi_state == COLMI_CLIENT_DONE || s_colmi_state == COLMI_CLIENT_ERROR) &&
        (s_lunasay_ring_retry_ms == 0 || (int32_t)(now_ms - s_lunasay_ring_retry_ms) >= 0)) {
        const esp_err_t err = colmi_client_start();
        s_lunasay_ring_retry_ms = now_ms + (err == ESP_OK ? 6000u : 3000u);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "lunasay ring connect start %s", esp_err_to_name(err));
        }
    }
}

bool faculty175_ble_lunasay_ring_event_consume(faculty175_ble_ring_event_t *out)
{
    if (out == NULL) {
        return false;
    }
    portENTER_CRITICAL(&s_lunasay_ring_event_lock);
    const faculty175_ble_ring_event_t event = s_lunasay_ring_event;
    s_lunasay_ring_event = FACULTY175_BLE_RING_EVENT_NONE;
    portEXIT_CRITICAL(&s_lunasay_ring_event_lock);
    *out = event;
    return event != FACULTY175_BLE_RING_EVENT_NONE;
}

bool faculty175_ble_lunasay_ring_near(void)
{
    return s_lunasay_ring_near;
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

esp_err_t faculty175_ble_ring_pair(uint16_t ring_id)
{
    if (ring_id == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t err = ble_nvs_set_ring_id(ring_id, true);
    if (err == ESP_OK) {
        s_paired_ring_id = ring_id;
        s_paired_ring_id_set = true;
        s_have_last_ring_rssi = false;
        s_lunasay_ring_retry_ms = 0;
    }
    return err;
}

esp_err_t faculty175_ble_ring_unpair(void)
{
    const esp_err_t err = ble_nvs_set_ring_id(0, false);
    if (err != ESP_OK) {
        return err;
    }
    s_paired_ring_id = 0;
    s_paired_ring_id_set = false;
    s_have_last_ring_rssi = false;
    s_lunasay_ring_control_active = false;
    s_lunasay_ring_near = false;
    s_colmi_want_scan = false;
    if (s_scanning) {
        (void)ble_gap_disc_cancel();
        s_scanning = false;
    }
    if (s_colmi_have_conn) {
        (void)ble_gap_terminate(s_colmi_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
    s_colmi_state = COLMI_CLIENT_IDLE;
    return ESP_OK;
}

bool faculty175_ble_ring_paired(uint16_t *ring_id)
{
    if (ring_id != NULL) {
        *ring_id = s_paired_ring_id;
    }
    return s_paired_ring_id_set;
}

bool faculty175_ble_nearby_unpaired_ring(uint16_t *ring_id, int8_t *rssi)
{
    if (ring_id != NULL) {
        *ring_id = 0;
    }
    if (rssi != NULL) {
        *rssi = -127;
    }
    uint16_t paired_id = 0;
    if (faculty175_ble_ring_paired(&paired_id)) {
        return false;
    }

    faculty175_ble_peer_t peers[FACULTY175_BLE_PEER_MAX] = {};
    const size_t count = faculty175_ble_peers_snapshot(peers, FACULTY175_BLE_PEER_MAX);
    for (size_t i = 0; i < count; ++i) {
        if (!peers[i].valid || !peers[i].ring || peers[i].addr_hash == 0 ||
            peers[i].rssi < faculty175_ble_near_rssi_threshold()) {
            continue;
        }
        if (ring_id != NULL) {
            *ring_id = peers[i].addr_hash;
        }
        if (rssi != NULL) {
            *rssi = peers[i].rssi;
        }
        return true;
    }
    return false;
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

void faculty175_ble_prepare_deep_sleep(void)
{
    if (s_advertising) {
        (void)ble_gap_adv_stop();
    }
    if (s_scanning) {
        (void)ble_gap_disc_cancel();
    }
    s_advertising = false;
    s_scanning = false;
    FACULTY175_LOG_STAGE(TAG, "ble", "radio quiesced for deep sleep");
}

void faculty175_ble_resume_after_deep_sleep_abort(void)
{
    if (s_enabled && s_started && s_synced && !s_advertising && !s_scanning) {
        const esp_err_t err = ble_advertise();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "deep-sleep abort BLE restore failed: %s", esp_err_to_name(err));
            return;
        }
    }
    FACULTY175_LOG_STAGE(TAG, "ble", "deep-sleep abort restored radio preference");
}

esp_err_t faculty175_ble_power_test_set(bool enabled)
{
    if (enabled) {
        if (s_power_test_active) {
            return ESP_OK;
        }
        bool configured_enabled = true;
        const esp_err_t read_err = ble_nvs_get_enabled(&configured_enabled);
        if (read_err != ESP_OK) {
            ESP_LOGW(TAG, "power-test preference read failed %s", esp_err_to_name(read_err));
        }
        s_power_test_previous_enabled = configured_enabled;
        s_power_test_active = true;
        s_power_scenario_suspended = false;
        s_enabled = true;
        const esp_err_t init_err = faculty175_ble_init();
        if (init_err != ESP_OK) {
            s_power_test_active = false;
            s_enabled = s_power_test_previous_enabled;
            return init_err;
        }
        if (s_synced && !s_advertising && !s_scanning) {
            return ble_advertise();
        }
        return ESP_OK;
    }

    if (!s_power_test_active) {
        return ESP_OK;
    }
    if (s_advertising) {
        (void)ble_gap_adv_stop();
    }
    if (s_scanning) {
        (void)ble_gap_disc_cancel();
    }
    s_advertising = false;
    s_scanning = false;
    s_power_test_active = false;
    s_enabled = s_power_test_previous_enabled;
    if (s_enabled && s_started && s_synced) {
        return ble_advertise();
    }
    return ESP_OK;
}

bool faculty175_ble_power_test_active(void)
{
    return s_power_test_active;
}

void faculty175_ble_power_scenario_suspend(bool suspended)
{
    if (suspended && s_power_test_active) {
        return;
    }
    if (s_power_scenario_suspended == suspended) {
        return;
    }
    s_power_scenario_suspended = suspended;
    if (suspended) {
        if (s_advertising) {
            (void)ble_gap_adv_stop();
        }
        if (s_scanning) {
            (void)ble_gap_disc_cancel();
        }
        s_advertising = false;
        s_scanning = false;
        FACULTY175_LOG_STAGE(TAG, "ble", "power scenario suspended radio");
        return;
    }
    if (s_enabled && s_started && s_synced && !s_advertising && !s_scanning) {
        (void)ble_advertise();
    }
    FACULTY175_LOG_STAGE(TAG, "ble", "power scenario restored radio preference");
}

bool faculty175_ble_power_scenario_suspended(void)
{
    return s_power_scenario_suspended;
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

    const bool explicit_scan_cmd = strcasecmp(sub, "scan") == 0 || strcasecmp(sub, "ring scan") == 0 ||
                                   strcasecmp(sub, "ring vitals") == 0 || strcasecmp(sub, "ring connect") == 0 ||
                                   strncasecmp(sub, "raw", 3) == 0;
    if (!explicit_scan_cmd) {
        ble_defer_radar_scan(10000u);
    }

    if (*sub == '\0' || strcasecmp(sub, "status") == 0) {
        printf("ble: enabled=%s started=%s synced=%s advertising=%s scanning=%s power_test=%s scenario_suspended=%s name=\"%s\"\n",
               s_enabled ? "yes" : "no",
               s_started ? "yes" : "no",
               s_synced ? "yes" : "no",
               s_advertising ? "yes" : "no",
               s_scanning ? "yes" : "no",
               s_power_test_active ? "yes" : "no",
               s_power_scenario_suspended ? "yes" : "no",
               s_device_name);
    } else if (strcasecmp(sub, "power-test on") == 0) {
        const esp_err_t err = faculty175_ble_power_test_set(true);
        printf("ble: power-test on %s\n", esp_err_to_name(err));
    } else if (strcasecmp(sub, "power-test off") == 0) {
        const esp_err_t err = faculty175_ble_power_test_set(false);
        printf("ble: power-test off %s\n", esp_err_to_name(err));
    } else if (strcasecmp(sub, "power-test status") == 0) {
        printf("ble: power-test active=%s advertising=%s scanning=%s\n",
               s_power_test_active ? "yes" : "no",
               s_advertising ? "yes" : "no",
               s_scanning ? "yes" : "no");
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
    } else if (strcasecmp(sub, "raw") == 0 || strncasecmp(sub, "raw ", 4) == 0) {
        uint32_t duration_ms = 10000u;
        if (strncasecmp(sub, "raw ", 4) == 0) {
            char *end = NULL;
            const unsigned long parsed = strtoul(sub + 4, &end, 10);
            if (end != sub + 4 && parsed > 0) {
                duration_ms = (uint32_t)parsed;
            }
        }
        if (duration_ms < 1000u) {
            duration_ms = 1000u;
        } else if (duration_ms > 20000u) {
            duration_ms = 20000u;
        }
        s_raw_scan_log = true;
        s_raw_scan_until_ms = ble_now_ms() + duration_ms;
        const esp_err_t err = faculty175_ble_scan_start(duration_ms);
        printf("ble: raw %s duration_ms=%lu\n", esp_err_to_name(err), (unsigned long)duration_ms);
        if (err != ESP_OK) {
            s_raw_scan_log = false;
            s_raw_scan_until_ms = 0;
        } else {
            while (s_scanning && s_raw_scan_log && (int32_t)(s_raw_scan_until_ms - ble_now_ms()) > 0) {
                vTaskDelay(pdMS_TO_TICKS(250));
            }
            printf("ble: raw done scanning=%s\n", s_scanning ? "yes" : "no");
        }
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
        faculty175_ring_vitals_t vitals = {};
        const bool have_vitals = faculty175_ring_latest_vitals(&vitals);
        printf("ble: ring paired=%s colmi=%s", s_paired_ring_id_set ? "yes" : "no", colmi_state_name(s_colmi_state));
        if (s_paired_ring_id_set) {
            printf(" id=%04x", s_paired_ring_id);
        }
        printf(" samples=%u scanning=%s\n", (unsigned)count, s_scanning ? "yes" : "no");
        if (have_vitals) {
            const uint32_t now = ble_now_ms();
            const uint32_t age = vitals.updated_ms <= now ? now - vitals.updated_ms : 0;
            printf("  vitals age=%ums hr=%s%u hrv=%s%u spo2=%s%u batt=%s%u\n",
                   (unsigned)age,
                   vitals.heart_rate_valid ? "" : "?",
                   vitals.heart_rate_bpm,
                   vitals.hrv_valid ? "" : "?",
                   vitals.hrv_ms,
                   vitals.spo2_valid ? "" : "?",
                   vitals.spo2_percent,
                   vitals.battery_valid ? "" : "?",
                   vitals.battery_percent);
        }
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
        uint16_t id = 0;
        uint8_t mac_addr[6] = {};
        bool parsed = false;
        if (ble_parse_public_mac_text(id_text, mac_addr)) {
            id = ble_addr_hash16(mac_addr);
            parsed = true;
        } else {
            char *end = NULL;
            const unsigned long parsed_id = strtoul(id_text, &end, 16);
            if (end != id_text && parsed_id <= 0xfffful) {
                id = (uint16_t)parsed_id;
                parsed = true;
            }
        }
        if (!parsed) {
            printf("ble: ring pair ESP_ERR_INVALID_ARG\n");
        } else {
            const esp_err_t err = faculty175_ble_ring_pair(id);
            printf("ble: ring pair %s id=%04x\n", esp_err_to_name(err), (unsigned)id);
        }
    } else if (strcasecmp(sub, "ring clear") == 0 || strcasecmp(sub, "ring unpair") == 0) {
        const esp_err_t err = faculty175_ble_ring_unpair();
        printf("ble: ring clear %s\n", esp_err_to_name(err));
    } else if (strcasecmp(sub, "ring scan") == 0) {
        const esp_err_t err = faculty175_ble_scan_start(3000u);
        printf("ble: ring scan %s\n", esp_err_to_name(err));
    } else if (strcasecmp(sub, "ring connect") == 0 || strcasecmp(sub, "ring vitals") == 0) {
        const esp_err_t err = colmi_client_start();
        printf("ble: ring vitals %s state=%s\n", esp_err_to_name(err), colmi_state_name(s_colmi_state));
    } else if (strcasecmp(sub, "ring packet") == 0) {
        char hex[48];
        ble_format_hex_full(s_colmi_last_packet, s_colmi_have_packet ? COLMI_PACKET_LEN : 0, hex, sizeof(hex));
        printf("ble: ring packet state=%s have=%s data=%s\n",
               colmi_state_name(s_colmi_state),
               s_colmi_have_packet ? "yes" : "no",
               hex);
    } else {
        printf("ble commands:\n");
        printf("  ble status\n");
        printf("  ble on\n");
        printf("  ble off\n");
        printf("  ble name <device name>\n");
        printf("  ble scan\n");
        printf("  ble raw [ms]\n");
        printf("  ble peers\n");
        printf("  ble ring\n");
        printf("  ble ring pair <short-id|public-mac>\n");
        printf("  ble ring clear\n");
        printf("  ble ring scan\n");
        printf("  ble ring vitals\n");
        printf("  ble ring packet\n");
    }
    fflush(stdout);
    return true;
}
