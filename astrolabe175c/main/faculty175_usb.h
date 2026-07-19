#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "esp_netif_ip_addr.h"
#include "esp_system.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t faculty175_usb_init(void);
esp_err_t faculty175_usb_set_screen_face_active(bool active);
esp_err_t faculty175_usb_tether_start(void);
bool faculty175_usb_screen_profile_active(void);
bool faculty175_usb_tether_mode_active(void);
bool faculty175_usb_tether_ready(void);
const esp_ip4_addr_t *faculty175_usb_tether_ip(void);
const char *faculty175_usb_tether_last_stage(void);
esp_err_t faculty175_usb_tether_last_error(void);
uint32_t faculty175_usb_tether_attempt_count(void);
uint32_t faculty175_usb_tether_last_uptime_ms(void);
esp_reset_reason_t faculty175_usb_tether_boot_reset_reason(void);
bool faculty175_usb_auto_tether_enabled(void);
esp_err_t faculty175_usb_auto_tether_set_enabled(bool enabled);
esp_err_t faculty175_usb_storage_claim(void);
bool faculty175_usb_storage_ready(void);
bool faculty175_usb_storage_mounted(void);
const char *faculty175_usb_storage_base_path(void);
bool faculty175_usb_storage_resolve_path(const char *relative_path, char *out, size_t cap);

#ifdef __cplusplus
}
#endif
