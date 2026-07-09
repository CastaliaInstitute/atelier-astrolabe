#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_wifi_types.h"

#include "faculty175_faces.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FACULTY175_WIFI_LAB_AP_MAX 16
#define FACULTY175_WIFI_LAB_STATUS_MAX 64
#define FACULTY175_WIFI_LAB_DETAIL_MAX 96
#define FACULTY175_WIFI_LAB_CRED_MAX 80
#define FACULTY175_WIFI_LAB_PCAP_PATH "/bust_cache/handshake.pcap"

typedef struct {
    char ssid[33];
    uint8_t bssid[6];
    int rssi;
    uint8_t channel;
    wifi_auth_mode_t authmode;
} faculty175_wifi_lab_ap_t;

typedef struct {
    bool active;
    bool busy;
    char status[FACULTY175_WIFI_LAB_STATUS_MAX];
    char detail[FACULTY175_WIFI_LAB_DETAIL_MAX];
    uint16_t ap_count;
    uint8_t selected;
    faculty175_wifi_lab_ap_t aps[FACULTY175_WIFI_LAB_AP_MAX];
    char target_ssid[33];
    uint8_t target_bssid[6];
    uint8_t target_channel;
    uint32_t deauth_sent;
    uint32_t handshake_count;
    uint32_t capture_count;
    char last_cred[FACULTY175_WIFI_LAB_CRED_MAX];
    uint32_t pcap_frames;
    uint32_t pcap_bytes;
    char pcap_path[64];
} faculty175_wifi_lab_state_t;

bool faculty175_wifi_lab_is_face(faculty175_face_id_t id);
void faculty175_wifi_lab_on_enter(faculty175_face_id_t id);
void faculty175_wifi_lab_on_leave(faculty175_face_id_t id);
void faculty175_wifi_lab_tick(faculty175_face_id_t id, uint32_t now_ms);
bool faculty175_wifi_lab_tap(faculty175_face_id_t id);
bool faculty175_wifi_lab_cycle_target(faculty175_face_id_t id, int delta);
void faculty175_wifi_lab_get_state(faculty175_wifi_lab_state_t *out);
void faculty175_wifi_lab_record_capture(const char *ssid, const char *pass);
esp_err_t faculty175_wifi_lab_scan(void);
esp_err_t faculty175_wifi_lab_set_target_index(uint8_t index);
esp_err_t faculty175_wifi_lab_export_pcap(void);

#ifdef __cplusplus
}
#endif
