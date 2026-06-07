#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "esp_netif_ip_addr.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FACULTY175_WIFI_SSID_MAX 32
#define FACULTY175_WIFI_PASS_MAX 64
#define FACULTY175_WIFI_URL_MAX 64
#define FACULTY175_WIFI_QR_MAX 160
#define FACULTY175_WIFI_KNOWN_MAX 6

typedef struct {
    char ssid[FACULTY175_WIFI_SSID_MAX + 1];
    char pass[FACULTY175_WIFI_PASS_MAX + 1];
} faculty175_wifi_known_t;

esp_err_t faculty175_wifi_settings_load(char *ssid, size_t ssid_cap, char *pass, size_t pass_cap);
esp_err_t faculty175_wifi_settings_save(const char *ssid, const char *pass);
size_t faculty175_wifi_settings_load_known(faculty175_wifi_known_t *out, size_t cap);
esp_err_t faculty175_wifi_settings_add_known(const char *ssid, const char *pass, bool make_primary);
esp_err_t faculty175_wifi_settings_remove_known(const char *ssid);
esp_err_t faculty175_wifi_settings_clear_known(void);
bool faculty175_wifi_settings_travel_router_enabled(void);
esp_err_t faculty175_wifi_settings_set_travel_router_enabled(bool enabled);

void faculty175_wifi_settings_set_sta(const char *ssid, const esp_ip4_addr_t *ip);
void faculty175_wifi_settings_set_ap(const char *ssid, const char *pass, const esp_ip4_addr_t *ip);
void faculty175_wifi_settings_set_router_upstream(const char *ssid, const esp_ip4_addr_t *ip);
void faculty175_wifi_settings_set_ap_client_count(unsigned count);
void faculty175_wifi_settings_clear_runtime(void);
void faculty175_wifi_settings_set_scan_suppressed(bool suppressed);

bool faculty175_wifi_settings_ap_active(void);
bool faculty175_wifi_settings_ap_client_connected(void);
bool faculty175_wifi_settings_scan_suppressed(void);
const char *faculty175_wifi_settings_ssid(void);
const char *faculty175_wifi_settings_upstream_ssid(void);
const char *faculty175_wifi_settings_url(void);
const char *faculty175_wifi_settings_qr_payload(void);
const char *faculty175_wifi_settings_ap_qr_payload(void);
const char *faculty175_wifi_settings_page_qr_payload(void);
const char *faculty175_wifi_settings_status(void);

#ifdef __cplusplus
}
#endif
