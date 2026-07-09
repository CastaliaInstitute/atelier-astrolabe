#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_wifi_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FACULTY175_WIFI_INCIDENT_MAX 32
#define FACULTY175_WIFI_INCIDENT_TEXT_MAX 48

typedef struct {
    uint32_t seq;
    uint32_t uptime_ms;
    char type[20];
    char ssid[33];
    uint8_t bssid[6];
    int reason;
    int rssi;
    uint8_t channel;
    wifi_auth_mode_t authmode;
    char detail[FACULTY175_WIFI_INCIDENT_TEXT_MAX];
} faculty175_wifi_incident_t;

void faculty175_wifi_monitor_record_connected(const wifi_ap_record_t *ap);
void faculty175_wifi_monitor_record_disconnected(const char *ssid,
                                                 const uint8_t bssid[6],
                                                 int reason,
                                                 int rssi);
void faculty175_wifi_monitor_record_ap_client(bool connected, int aid);
void faculty175_wifi_monitor_record_scan(uint16_t count);
void faculty175_wifi_monitor_record_note(const char *type, const char *detail);
size_t faculty175_wifi_monitor_copy(faculty175_wifi_incident_t *out, size_t cap);
size_t faculty175_wifi_monitor_count(void);
bool faculty175_wifi_monitor_get_newest(size_t offset, faculty175_wifi_incident_t *out);
void faculty175_wifi_monitor_clear(void);

#ifdef __cplusplus
}
#endif

