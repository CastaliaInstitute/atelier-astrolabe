#include "faculty175_ble.h"


#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "astrolabe_time.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_app_desc.h"
#include "esp_ota_ops.h"
#include "esp_wifi.h"
#include "faculty175_faces.h"
#include "faculty175_ota.h"
#include "faculty175_usb.h"
#if __has_include("secrets.local.h")
#include "secrets.local.h"
#else
#include "secrets.example.h"
#endif
#include "nvs.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

#include "faculty175_log.h"
#include "faculty175_device_settings.h"
#include "faculty175_wifi_settings.h"

void ble_store_config_init(void);

static const char *TAG = "faculty175_ble";
static const char *BLE_NVS_NS = "ble";
static const char *BLE_NVS_ENABLED = "enabled";
static const char *BLE_DEVICE_NAME = "Astrolabe 185B Cyber";
enum { BLE_APPEARANCE_GENERIC_TAG = 0x0200 };

static const ble_uuid128_t BLE_SETTINGS_SERVICE_UUID =
    BLE_UUID128_INIT(0x41, 0x73, 0x74, 0x72, 0x6f, 0x6c, 0x61, 0x62, 0x65, 0x00, 0x17, 0x50, 0x00, 0x00, 0x00, 0x01);
static const ble_uuid128_t BLE_ENABLED_CHAR_UUID =
    BLE_UUID128_INIT(0x41, 0x73, 0x74, 0x72, 0x6f, 0x6c, 0x61, 0x62, 0x65, 0x00, 0x17, 0x50, 0x00, 0x00, 0x00, 0x02);
static const ble_uuid128_t BLE_SETTINGS_JSON_CHAR_UUID =
    BLE_UUID128_INIT(0x41, 0x73, 0x74, 0x72, 0x6f, 0x6c, 0x61, 0x62, 0x65, 0x00, 0x17, 0x50, 0x00, 0x00, 0x00, 0x03);

static const ble_uuid128_t BLE_CONTROL_JSON_CHAR_UUID =
    BLE_UUID128_INIT(0x41, 0x73, 0x74, 0x72, 0x6f, 0x6c, 0x61, 0x62, 0x65, 0x00, 0x17, 0x50, 0x00, 0x00, 0x00, 0x06);

static bool s_enabled = true;
static bool s_started;
static bool s_synced;
static bool s_advertising;
static uint8_t s_own_addr_type;
static char s_json_rx[768];
static size_t s_json_rx_len;
static bool s_json_rx_active;

static int ble_gap_event(struct ble_gap_event *event, void *arg);
static int ble_settings_enabled_access(uint16_t conn_handle,
                                       uint16_t attr_handle,
                                       struct ble_gatt_access_ctxt *ctxt,
                                       void *arg);
static int ble_settings_json_access(uint16_t conn_handle,
                                    uint16_t attr_handle,
                                    struct ble_gatt_access_ctxt *ctxt,
                                    void *arg);

static int ble_control_json_access(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);

static const struct ble_gatt_svc_def k_ble_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &BLE_SETTINGS_SERVICE_UUID.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                .uuid = &BLE_ENABLED_CHAR_UUID.u,
                .access_cb = ble_settings_enabled_access,
                .flags = BLE_GATT_CHR_F_READ | (ASTROLABE185B_CYBER_FEATURES ? 0 : BLE_GATT_CHR_F_WRITE),
            },
            {
                .uuid = &BLE_SETTINGS_JSON_CHAR_UUID.u,
                .access_cb = ble_settings_json_access,
                .flags = BLE_GATT_CHR_F_READ | (ASTROLABE185B_CYBER_FEATURES ? 0 : BLE_GATT_CHR_F_WRITE),
            },
            { .uuid = &BLE_CONTROL_JSON_CHAR_UUID.u, .access_cb = ble_control_json_access,
              /*
               * Android on F101 reports GATT 133 when resuming link encryption
               * for this characteristic, even after it has completed LE bonding.
               * The Cyber control payload is independently authenticated with
               * MYNAH_REMOTE_CONTROL_KEY, so keep the transport available while
               * rejecting every command that does not carry that key.
               */
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE },
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

static esp_err_t ble_apply_control_json(const cJSON *control)
{
    const cJSON *key = cJSON_GetObjectItemCaseSensitive(control, "key");
    const char *expected = MYNAH_REMOTE_CONTROL_KEY;
    if (expected[0] == '\0' || !cJSON_IsString(key) || key->valuestring == NULL ||
        strlen(key->valuestring) != strlen(expected)) return ESP_ERR_INVALID_STATE;
    unsigned difference = 0;
    for (size_t i = 0; i < strlen(expected); ++i) difference |= (unsigned char)expected[i] ^ (unsigned char)key->valuestring[i];
    if (difference != 0) return ESP_ERR_INVALID_STATE;
    const cJSON *face = cJSON_GetObjectItemCaseSensitive(control, "face");
    const cJSON *ota = cJSON_GetObjectItemCaseSensitive(control, "ota");
    const cJSON *wifi = cJSON_GetObjectItemCaseSensitive(control, "wifi");
    const cJSON *bootloader = cJSON_GetObjectItemCaseSensitive(control, "bootloader");
    if ((cJSON_IsString(face) ? 1 : 0) + (cJSON_IsObject(ota) ? 1 : 0) + (cJSON_IsObject(wifi) ? 1 : 0) + (cJSON_IsTrue(bootloader) ? 1 : 0) != 1) return ESP_ERR_INVALID_ARG;
    if (cJSON_IsTrue(bootloader)) return faculty175_usb_enter_bootloader();
    if (cJSON_IsObject(wifi)) {
        const cJSON *ssid = cJSON_GetObjectItemCaseSensitive(wifi, "ssid");
        const cJSON *pass = cJSON_GetObjectItemCaseSensitive(wifi, "password");
        if (!cJSON_IsString(ssid) || strlen(ssid->valuestring) == 0 || strlen(ssid->valuestring) > 32) return ESP_ERR_INVALID_ARG;
        char password[64] = {};
        if (cJSON_IsString(pass)) {
            if (strlen(pass->valuestring) > 63) return ESP_ERR_INVALID_ARG;
            strlcpy(password, pass->valuestring, sizeof(password));
        } else if (pass == NULL) {
            faculty175_wifi_known_t known[FACULTY175_WIFI_KNOWN_MAX];
            const size_t count = faculty175_wifi_settings_load_known(known, FACULTY175_WIFI_KNOWN_MAX);
            bool found = false;
            for (size_t i = 0; i < count; ++i) {
                if (strcmp(known[i].ssid, ssid->valuestring) == 0) {
                    strlcpy(password, known[i].pass, sizeof(password));
                    found = true;
                    break;
                }
            }
            memset(known, 0, sizeof(known));
            if (!found) return ESP_ERR_NOT_FOUND;
        } else return ESP_ERR_INVALID_ARG;
        esp_err_t result = faculty175_wifi_settings_save(ssid->valuestring, password);
        if (result != ESP_OK) return result;
        wifi_mode_t mode;
        result = esp_wifi_get_mode(&mode);
        if (result != ESP_OK) return result;
        (void)esp_wifi_disconnect();
        result = esp_wifi_set_mode(mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA ? WIFI_MODE_APSTA : WIFI_MODE_STA);
        wifi_config_t config = {};
        memcpy(config.sta.ssid, ssid->valuestring, strlen(ssid->valuestring));
        memcpy(config.sta.password, password, strlen(password));
        memset(password, 0, sizeof(password));
        if (result == ESP_OK) result = esp_wifi_set_config(WIFI_IF_STA, &config);
        if (result == ESP_OK) result = esp_wifi_connect();
        return result;
    }
    if (cJSON_IsString(face)) {
        const faculty175_face_desc_t *target = faculty175_faces_find(face->valuestring);
        return target != NULL ? faculty175_faces_set(target->id) : ESP_ERR_INVALID_ARG;
    }
    if (cJSON_IsObject(ota)) {
        const faculty175_face_desc_t *current = faculty175_faces_current();
        if (current == NULL || current->id != FACULTY175_FACE_OTA) return ESP_ERR_INVALID_STATE;
        const cJSON *url = cJSON_GetObjectItemCaseSensitive(ota, "url");
        const cJSON *sha = cJSON_GetObjectItemCaseSensitive(ota, "sha256");
        if (!cJSON_IsString(url) || !cJSON_IsString(sha)) return ESP_ERR_INVALID_ARG;
        return faculty175_ota_fetch_verified(url->valuestring, sha->valuestring);
    }
    return ESP_ERR_INVALID_ARG;
}

static int ble_control_json_access(uint16_t conn_handle, uint16_t attr_handle,
                                  struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle; (void)attr_handle; (void)arg;
    static char buffer[1024];
    static size_t length;
    static bool receiving;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        faculty175_ota_status_t ota = {};
        faculty175_ota_get_status(&ota);
        const faculty175_face_desc_t *face = faculty175_faces_current();
        const esp_app_desc_t *app = esp_app_get_description();
        cJSON *status = cJSON_CreateObject();
        if (status == NULL) return BLE_ATT_ERR_INSUFFICIENT_RES;
        cJSON_AddNumberToObject(status, "controlVersion", 1);
        cJSON_AddStringToObject(status, "board", "185b");
        cJSON_AddBoolToObject(status, "sdPresent", faculty175_usb_sd_present());
        cJSON_AddNumberToObject(status, "sdCapacityBytes", faculty175_usb_sd_capacity());
        cJSON_AddStringToObject(status, "face", face != NULL ? face->slug : "");
        cJSON_AddStringToObject(status, "version", app->version);
        char elf_hash[65];
        for (size_t i = 0; i < 32; ++i) snprintf(elf_hash + i * 2, 3, "%02x", app->app_elf_sha256[i]);
        cJSON_AddStringToObject(status, "elfSha256", elf_hash);
        const esp_partition_t *running = esp_ota_get_running_partition();
        cJSON_AddStringToObject(status, "partition", running != NULL ? running->label : "");
        cJSON_AddStringToObject(status, "wifiUrl", faculty175_wifi_settings_url());
        cJSON_AddBoolToObject(status, "wifiReady", ota.network_ready);
        cJSON_AddBoolToObject(status, "otaReady", ota.network_ready && !ota.active && esp_ota_get_next_update_partition(NULL) != NULL);
        cJSON_AddBoolToObject(status, "otaActive", ota.active);
        cJSON_AddStringToObject(status, "otaLast", ota.last);
        char *json = cJSON_PrintUnformatted(status);
        cJSON_Delete(status);
        if (json == NULL) return BLE_ATT_ERR_INSUFFICIENT_RES;
        const int rc = os_mbuf_append(ctxt->om, json, strlen(json));
        cJSON_free(json);
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_UNLIKELY;
    char chunk[96];
    uint16_t count = 0;
    if (ble_hs_mbuf_to_flat(ctxt->om, chunk, sizeof(chunk) - 1, &count) != 0) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    chunk[count] = '\0';
    if (strcmp(chunk, "BEGIN") == 0) { length = 0; receiving = true; buffer[0] = '\0'; return 0; }
    if (!receiving) return BLE_ATT_ERR_UNLIKELY;
    if (strcmp(chunk, "END") == 0) {
        receiving = false;
        cJSON *control = cJSON_Parse(buffer);
        const esp_err_t result = cJSON_IsObject(control) ? ble_apply_control_json(control) : ESP_ERR_INVALID_ARG;
        cJSON_Delete(control);
        memset(buffer, 0, sizeof(buffer));
        return result == ESP_OK ? 0 : BLE_ATT_ERR_UNLIKELY;
    }
    if (length + count >= sizeof(buffer)) { receiving = false; return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN; }
    memcpy(buffer + length, chunk, count);
    length += count;
    buffer[length] = '\0';
    return 0;
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

    struct ble_hs_adv_fields rsp = {};
    const char *name = ble_svc_gap_device_name();
    rsp.name = (uint8_t *)name;
    rsp.name_len = strlen(name);
    rsp.name_is_complete = 1;
    rsp.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    rsp.tx_pwr_lvl_is_present = 1;
    rsp.appearance = BLE_APPEARANCE_GENERIC_TAG;
    rsp.appearance_is_present = 1;
    rc = ble_gap_adv_rsp_set_fields(&rsp);
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
    FACULTY175_LOG_STAGE(TAG, "ble", "advertising %s", BLE_DEVICE_NAME);
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
#if ASTROLABE185B_CYBER_FEATURES
    /* Legacy profiles may have disabled BLE; Cyber needs its control link. */
    s_enabled = true;
    esp_err_t err = ESP_OK;
#else
    esp_err_t err = ble_nvs_get_enabled(&s_enabled);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "enabled read failed %s; defaulting on", esp_err_to_name(err));
        s_enabled = true;
    }
#endif
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
    rc = ble_svc_gap_device_name_set(BLE_DEVICE_NAME);
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
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
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
        s_advertising = false;
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

    if (*sub == '\0' || strcasecmp(sub, "status") == 0) {
        printf("ble: enabled=%s started=%s synced=%s advertising=%s name=\"%s\"\n",
               s_enabled ? "yes" : "no",
               s_started ? "yes" : "no",
               s_synced ? "yes" : "no",
               s_advertising ? "yes" : "no",
               BLE_DEVICE_NAME);
    } else if (strcasecmp(sub, "on") == 0 || strcasecmp(sub, "enable") == 0) {
        const esp_err_t err = faculty175_ble_set_enabled(true);
        printf("ble: enable %s\n", esp_err_to_name(err));
    } else if (strcasecmp(sub, "off") == 0 || strcasecmp(sub, "disable") == 0) {
        const esp_err_t err = faculty175_ble_set_enabled(false);
        printf("ble: disable %s\n", esp_err_to_name(err));
    } else {
        printf("ble commands:\n");
        printf("  ble status\n");
        printf("  ble on\n");
        printf("  ble off\n");
    }
    fflush(stdout);
    return true;
}
