#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

static const char *TAG = "wand-idf";

static const ble_uuid128_t UUID_UART_SERVICE =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3, 0xa3, 0xb5, 0xf0, 0xff, 0x40, 0x6e);
static const ble_uuid128_t UUID_UART_WRITE =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e);
static const ble_uuid128_t UUID_UART_NOTIFY =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3, 0xa3, 0xb5, 0x03, 0x00, 0x40, 0x6e);
static const ble_uuid128_t UUID_MAIN_SERVICE =
    BLE_UUID128_INIT(0xc7, 0x5d, 0x2a, 0x01, 0xe3, 0x65, 0x26, 0xaf, 0x47, 0x4e, 0x11, 0xd7, 0x28, 0xf7, 0x5b, 0xde);
static const ble_uuid128_t UUID_MAIN_WRITE =
    BLE_UUID128_INIT(0xc7, 0x5d, 0x2a, 0x01, 0xe3, 0x65, 0x26, 0xaf, 0x47, 0x4e, 0x11, 0xd7, 0x2a, 0xf7, 0x5b, 0xde);
static const ble_uuid128_t UUID_MAIN_NOTIFY =
    BLE_UUID128_INIT(0xc7, 0x5d, 0x2a, 0x01, 0xe3, 0x65, 0x26, 0xaf, 0x47, 0x4e, 0x11, 0xd7, 0x29, 0xf7, 0x5b, 0xde);

typedef enum {
    DISC_UART,
    DISC_MAIN,
} disc_kind_t;

typedef struct {
    const ble_uuid128_t *svc_uuid;
    const ble_uuid128_t *write_uuid;
    const ble_uuid128_t *notify_uuid;
    uint16_t svc_start;
    uint16_t svc_end;
    uint16_t write_handle;
    uint16_t notify_handle;
    uint16_t cccd_handle;
    bool found;
    bool chars_done;
    bool desc_done;
} ring_service_t;

static ring_service_t s_uart = {
    .svc_uuid = &UUID_UART_SERVICE,
    .write_uuid = &UUID_UART_WRITE,
    .notify_uuid = &UUID_UART_NOTIFY,
};
static ring_service_t s_main = {
    .svc_uuid = &UUID_MAIN_SERVICE,
    .write_uuid = &UUID_MAIN_WRITE,
    .notify_uuid = &UUID_MAIN_NOTIFY,
};

static uint8_t s_own_addr_type;
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static ble_addr_t s_target_addr;
static int s_target_rssi = 0;
static bool s_connecting = false;
static bool s_connected = false;
static bool s_scan_passive = false;

static void log_heap(const char *label) {
    ESP_LOGI(TAG, "%s heap internal=%u largest=%u psram=%u",
             label,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
}

static uint8_t checksum(const uint8_t *data, size_t len_without_crc) {
    uint16_t sum = 0;
    for (size_t i = 0; i < len_without_crc; ++i) {
        sum += data[i];
    }
    return (uint8_t)(sum & 0xff);
}

static void make_command(uint8_t cmd, uint8_t a, uint8_t b, uint8_t out[16]) {
    memset(out, 0, 16);
    out[0] = cmd;
    out[1] = a;
    out[2] = b;
    out[15] = checksum(out, 15);
}

static int16_t parse_i12(uint8_t hi, uint8_t lo_nibble) {
    int16_t v = (int16_t)(((uint16_t)hi << 4) | (lo_nibble & 0x0f));
    if (v & 0x0800) {
        v -= 0x1000;
    }
    return v;
}

static void print_hex(const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        printf("%s%02x", i ? " " : "", data[i]);
    }
}

static bool uuid_eq(const ble_uuid_t *a, const ble_uuid128_t *b) {
    return a && b && ble_uuid_cmp(a, &b->u) == 0;
}

static bool adv_has_uuid128(const struct ble_hs_adv_fields *fields, const ble_uuid128_t *uuid) {
    for (uint8_t i = 0; i < fields->num_uuids128; ++i) {
        if (uuid_eq(&fields->uuids128[i].u, uuid)) {
            return true;
        }
    }
    for (uint8_t i = 0; i < fields->sol_num_uuids128; ++i) {
        if (uuid_eq(&fields->sol_uuids128[i].u, uuid)) {
            return true;
        }
    }
    if (fields->svc_data_uuid128_len >= 16 && fields->svc_data_uuid128) {
        ble_uuid128_t svc_data_uuid = BLE_UUID128_INIT(
            fields->svc_data_uuid128[0], fields->svc_data_uuid128[1],
            fields->svc_data_uuid128[2], fields->svc_data_uuid128[3],
            fields->svc_data_uuid128[4], fields->svc_data_uuid128[5],
            fields->svc_data_uuid128[6], fields->svc_data_uuid128[7],
            fields->svc_data_uuid128[8], fields->svc_data_uuid128[9],
            fields->svc_data_uuid128[10], fields->svc_data_uuid128[11],
            fields->svc_data_uuid128[12], fields->svc_data_uuid128[13],
            fields->svc_data_uuid128[14], fields->svc_data_uuid128[15]);
        if (uuid_eq(&svc_data_uuid.u, uuid)) {
            return true;
        }
    }
    return false;
}

static bool adv_has_uuid16(const struct ble_hs_adv_fields *fields, uint16_t uuid) {
    for (uint8_t i = 0; i < fields->num_uuids16; ++i) {
        if (ble_uuid_u16(&fields->uuids16[i].u) == uuid) {
            return true;
        }
    }
    for (uint8_t i = 0; i < fields->sol_num_uuids16; ++i) {
        if (ble_uuid_u16(&fields->sol_uuids16[i].u) == uuid) {
            return true;
        }
    }
    if (fields->svc_data_uuid16_len >= 2 && fields->svc_data_uuid16) {
        uint16_t svc_uuid = (uint16_t)fields->svc_data_uuid16[0] |
                            ((uint16_t)fields->svc_data_uuid16[1] << 8);
        if (svc_uuid == uuid) {
            return true;
        }
    }
    return false;
}

static bool adv_has_mfg_company(const struct ble_hs_adv_fields *fields, uint16_t company_id) {
    if (!fields->mfg_data || fields->mfg_data_len < 2) {
        return false;
    }
    const uint16_t got = (uint16_t)fields->mfg_data[0] |
                         ((uint16_t)fields->mfg_data[1] << 8);
    return got == company_id;
}

static bool adv_has_colmi_mfg_payload(const struct ble_hs_adv_fields *fields) {
    if (!fields->mfg_data || fields->mfg_data_len < 2) {
        return false;
    }
    for (uint8_t i = 0; i + 1 < fields->mfg_data_len; ++i) {
        if (fields->mfg_data[i] == 0xfe && fields->mfg_data[i + 1] == 0xe7) {
            return true;
        }
    }
    return false;
}

static void print_adv_raw(const uint8_t *data, uint8_t len) {
    printf("ADVRAW ");
    print_hex(data, len);
    printf("\n");
}

static void send_to_handle(uint16_t handle, const uint8_t *data, size_t len, const char *label) {
    if (!s_connected || handle == 0) {
        return;
    }
    printf("TX %s ", label);
    print_hex(data, len);
    printf("\n");
    int rc = ble_gattc_write_no_rsp_flat(s_conn_handle, handle, data, len);
    if (rc != 0) {
        ESP_LOGW(TAG, "write_no_rsp %s failed rc=%d; retrying write", label, rc);
        rc = ble_gattc_write_flat(s_conn_handle, handle, data, len, NULL, NULL);
        if (rc != 0) {
            ESP_LOGE(TAG, "write %s failed rc=%d", label, rc);
        }
    }
}

static void send_to_ring(const uint8_t *data, size_t len, const char *label) {
    if (s_uart.write_handle) {
        send_to_handle(s_uart.write_handle, data, len, label);
    }
    if (s_main.write_handle && s_main.write_handle != s_uart.write_handle) {
        send_to_handle(s_main.write_handle, data, len, label);
    }
}

static void send_framed(uint8_t cmd, uint8_t a, uint8_t b, const char *label) {
    uint8_t packet[16];
    make_command(cmd, a, b, packet);
    send_to_ring(packet, sizeof(packet), label);
}

static void parse_notify(const uint8_t *data, size_t len) {
    printf("RX ");
    print_hex(data, len);
    printf("\n");
    if (len >= 10 && data[0] == 0xa1 && data[1] == 0x03) {
        int16_t raw_y = parse_i12(data[2], data[3]);
        int16_t raw_z = parse_i12(data[4], data[5]);
        int16_t raw_x = parse_i12(data[6], data[7]);
        ESP_LOGI(TAG, "IMU rssi=%d raw x=%d y=%d z=%d g x=%.3f y=%.3f z=%.3f",
                 s_target_rssi, raw_x, raw_y, raw_z, raw_x / 512.0f, raw_y / 512.0f, raw_z / 512.0f);
    } else if ((data[0] & 0x7f) == 0x03 && len >= 3) {
        ESP_LOGI(TAG, "battery=%u charging=%u", data[1], data[2]);
    }
}

static int disc_next_service(void);

static void ring_ready(void) {
    ESP_LOGI(TAG, "ring connected handles uart write=%u notify=%u cccd=%u main write=%u notify=%u cccd=%u",
             s_uart.write_handle, s_uart.notify_handle, s_uart.cccd_handle,
             s_main.write_handle, s_main.notify_handle, s_main.cccd_handle);
    log_heap("after ring connect");
    send_framed(0x03, 0, 0, "battery");
    send_framed(0x10, 0, 0, "identify");
    static const uint8_t raw_short[] = {0xa1, 0x04, 0x04};
    send_to_ring(raw_short, sizeof(raw_short), "raw-short");
    send_framed(0xa1, 0x04, 0x00, "raw-framed");
}

static int subscribe_cccd_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                             struct ble_gatt_attr *attr, void *arg) {
    (void)conn_handle;
    (void)attr;
    ring_service_t *svc = (ring_service_t *)arg;
    ESP_LOGI(TAG, "subscribe %s status=%d", svc == &s_uart ? "uart" : "main", error->status);
    return 0;
}

static void subscribe_service(ring_service_t *svc) {
    if (!svc->cccd_handle) {
        return;
    }
    uint8_t value[2] = {1, 0};
    int rc = ble_gattc_write_flat(s_conn_handle, svc->cccd_handle, value, sizeof(value),
                                  subscribe_cccd_cb, svc);
    if (rc != 0) {
        ESP_LOGW(TAG, "subscribe write failed rc=%d", rc);
    }
}

static int dsc_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                  uint16_t chr_val_handle, const struct ble_gatt_dsc *dsc, void *arg) {
    (void)conn_handle;
    (void)chr_val_handle;
    ring_service_t *svc = (ring_service_t *)arg;
    if (error->status == 0 && dsc) {
        if (ble_uuid_u16(&dsc->uuid.u) == BLE_GATT_DSC_CLT_CFG_UUID16) {
            svc->cccd_handle = dsc->handle;
            ESP_LOGI(TAG, "%s cccd=%u", svc == &s_uart ? "uart" : "main", dsc->handle);
        }
        return 0;
    }
    svc->desc_done = true;
    subscribe_service(svc);
    if (svc == &s_uart) {
        return disc_next_service();
    }
    ring_ready();
    return 0;
}

static int chr_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                  const struct ble_gatt_chr *chr, void *arg) {
    ring_service_t *svc = (ring_service_t *)arg;
    if (error->status == 0 && chr) {
        char uuid_str[BLE_UUID_STR_LEN];
        ble_uuid_to_str(&chr->uuid.u, uuid_str);
        ESP_LOGI(TAG, "%s chr %s def=%u val=%u props=0x%02x",
                 svc == &s_uart ? "uart" : "main", uuid_str, chr->def_handle, chr->val_handle, chr->properties);
        if (uuid_eq(&chr->uuid.u, svc->write_uuid)) {
            svc->write_handle = chr->val_handle;
        }
        if (uuid_eq(&chr->uuid.u, svc->notify_uuid)) {
            svc->notify_handle = chr->val_handle;
        }
        return 0;
    }
    svc->chars_done = true;
    if (svc->notify_handle && svc->notify_handle < svc->svc_end) {
        int rc = ble_gattc_disc_all_dscs(conn_handle, svc->notify_handle + 1, svc->svc_end, dsc_cb, svc);
        if (rc == 0) {
            return 0;
        }
        ESP_LOGW(TAG, "descriptor discovery failed rc=%d", rc);
    }
    if (svc == &s_uart) {
        return disc_next_service();
    }
    ring_ready();
    return 0;
}

static int svc_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                  const struct ble_gatt_svc *svc_desc, void *arg) {
    ring_service_t *svc = (ring_service_t *)arg;
    if (error->status == 0 && svc_desc) {
        svc->found = true;
        svc->svc_start = svc_desc->start_handle;
        svc->svc_end = svc_desc->end_handle;
        ESP_LOGI(TAG, "%s service start=%u end=%u", svc == &s_uart ? "uart" : "main",
                 svc->svc_start, svc->svc_end);
        int rc = ble_gattc_disc_all_chrs(conn_handle, svc->svc_start, svc->svc_end, chr_cb, svc);
        if (rc != 0) {
            ESP_LOGW(TAG, "characteristic discovery failed rc=%d", rc);
        }
        return 0;
    }
    ESP_LOGW(TAG, "%s service not found status=%d", svc == &s_uart ? "uart" : "main", error->status);
    if (svc == &s_uart) {
        return disc_next_service();
    }
    ring_ready();
    return 0;
}

static int disc_next_service(void) {
    int rc = 0;
    if (!s_uart.chars_done && !s_uart.found) {
        rc = ble_gattc_disc_svc_by_uuid(s_conn_handle, &s_uart.svc_uuid->u, svc_cb, &s_uart);
    } else if (!s_main.chars_done && !s_main.found) {
        rc = ble_gattc_disc_svc_by_uuid(s_conn_handle, &s_main.svc_uuid->u, svc_cb, &s_main);
    } else {
        ring_ready();
    }
    if (rc != 0) {
        ESP_LOGW(TAG, "service discovery request failed rc=%d", rc);
    }
    return 0;
}

static bool name_looks_like_ring(const struct ble_hs_adv_fields *fields) {
    const uint8_t *name = fields->name;
    uint8_t len = fields->name_len;
    if (!name || len == 0) {
        return false;
    }
    return (len >= 5 && memcmp(name, "COLMI", 5) == 0) ||
           (len >= 3 && memmem(name, len, "R02", 3) != NULL);
}

static bool adv_looks_like_ring(const struct ble_hs_adv_fields *fields) {
    return name_looks_like_ring(fields) ||
           adv_has_uuid16(fields, 0xfee7) ||
           adv_has_mfg_company(fields, 0x1234) ||
           adv_has_colmi_mfg_payload(fields) ||
           adv_has_uuid128(fields, &UUID_UART_SERVICE) ||
           adv_has_uuid128(fields, &UUID_MAIN_SERVICE);
}

static void start_scan(void);

static int gap_event(struct ble_gap_event *event, void *arg) {
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_DISC: {
        struct ble_hs_adv_fields fields;
        if (ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data) != 0) {
            return 0;
        }
        char addr[18];
        snprintf(addr, sizeof(addr), "%02x:%02x:%02x:%02x:%02x:%02x",
                 event->disc.addr.val[5], event->disc.addr.val[4], event->disc.addr.val[3],
                 event->disc.addr.val[2], event->disc.addr.val[1], event->disc.addr.val[0]);
        const bool ringish = adv_looks_like_ring(&fields);
        static uint32_t adv_count = 0;
        ++adv_count;
        const bool interesting_mfg = fields.mfg_data && (fields.mfg_data_len == 8 || fields.mfg_data_len == 10);
        if (ringish || interesting_mfg || (adv_count % 512u) == 0u) {
            ESP_LOGI(TAG, "%sadv %s rssi=%d type=%u name=%.*s uuid16=%u svc16=%u uuid128=%u sol128=%u mfg=%u svc128=%u seen=%u",
                     ringish ? "RING " : "", addr, event->disc.rssi, event->disc.event_type,
                     fields.name_len, fields.name ? (const char *)fields.name : "",
                     fields.num_uuids16, fields.svc_data_uuid16_len,
                     fields.num_uuids128, fields.sol_num_uuids128,
                     fields.mfg_data_len, fields.svc_data_uuid128_len,
                     (unsigned)adv_count);
            if (ringish) {
                print_adv_raw(event->disc.data, event->disc.length_data);
            } else if (interesting_mfg) {
                printf("MFGRAW ");
                print_hex(fields.mfg_data, fields.mfg_data_len);
                printf("\n");
            }
        }
        if (!s_connecting && ringish) {
            s_connecting = true;
            s_target_addr = event->disc.addr;
            s_target_rssi = event->disc.rssi;
            ESP_LOGI(TAG, "ring candidate %s rssi=%d name=%.*s", addr, event->disc.rssi,
                     fields.name_len, fields.name ? (const char *)fields.name : "");
            ble_gap_disc_cancel();
            int rc = ble_gap_connect(s_own_addr_type, &s_target_addr, 30000, NULL, gap_event, NULL);
            if (rc != 0) {
                ESP_LOGE(TAG, "connect start failed rc=%d", rc);
                s_connecting = false;
                start_scan();
            }
        }
        return 0;
    }
    case BLE_GAP_EVENT_CONNECT:
        s_connecting = false;
        if (event->connect.status == 0) {
            s_connected = true;
            s_conn_handle = event->connect.conn_handle;
            ESP_LOGI(TAG, "connected conn=%u", s_conn_handle);
            log_heap("after gap connect");
            memset(&s_uart, 0, sizeof(s_uart));
            memset(&s_main, 0, sizeof(s_main));
            s_uart.svc_uuid = &UUID_UART_SERVICE;
            s_uart.write_uuid = &UUID_UART_WRITE;
            s_uart.notify_uuid = &UUID_UART_NOTIFY;
            s_main.svc_uuid = &UUID_MAIN_SERVICE;
            s_main.write_uuid = &UUID_MAIN_WRITE;
            s_main.notify_uuid = &UUID_MAIN_NOTIFY;
            disc_next_service();
        } else {
            ESP_LOGW(TAG, "connect failed status=%d", event->connect.status);
            s_connected = false;
            s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            start_scan();
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGW(TAG, "disconnect reason=%d", event->disconnect.reason);
        s_connected = false;
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        start_scan();
        return 0;
    case BLE_GAP_EVENT_NOTIFY_RX: {
        const uint16_t len = OS_MBUF_PKTLEN(event->notify_rx.om);
        uint8_t buf[64];
        const uint16_t n = len < sizeof(buf) ? len : sizeof(buf);
        os_mbuf_copydata(event->notify_rx.om, 0, n, buf);
        parse_notify(buf, n);
        return 0;
    }
    case BLE_GAP_EVENT_DISC_COMPLETE:
        ESP_LOGI(TAG, "scan complete reason=%d", event->disc_complete.reason);
        if (!s_connected && !s_connecting) {
            s_scan_passive = !s_scan_passive;
            start_scan();
        }
        return 0;
    default:
        return 0;
    }
}

static void start_scan(void) {
    struct ble_gap_disc_params params = {
        .itvl = 0x20,
        .window = 0x20,
        .filter_policy = 0,
        .limited = 0,
        .passive = s_scan_passive ? 1 : 0,
        .filter_duplicates = 0,
    };
    int rc = ble_gap_disc(s_own_addr_type, 5000, &params, gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "scan start failed rc=%d", rc);
    } else {
        ESP_LOGI(TAG, "scanning for COLMI/R02 mode=%s", s_scan_passive ? "passive" : "active");
        log_heap("after scan start");
    }
}

static void on_sync(void) {
    int rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "id infer failed rc=%d", rc);
        return;
    }
    start_scan();
}

static void on_reset(int reason) {
    ESP_LOGW(TAG, "nimble reset reason=%d", reason);
}

static void host_task(void *param) {
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void app_main(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_LOGI(TAG, "ESP-IDF wand receiver boot");
    log_heap("boot");

    ESP_ERROR_CHECK(nimble_port_init());
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;

    nimble_port_freertos_init(host_task);
}
