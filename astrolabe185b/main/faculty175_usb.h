#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t faculty175_usb_init(void);
bool faculty175_usb_storage_ready(void);
bool faculty175_usb_storage_mounted(void);
const char *faculty175_usb_storage_base_path(void);
bool faculty175_usb_resolve_asset_path(const char *sd_relative, const char *fallback_path, char *out, size_t cap);
bool faculty175_usb_sd_present(void);
uint64_t faculty175_usb_sd_capacity(void);
void faculty175_usb_set_face_active(bool active);
esp_err_t faculty175_usb_enter_bootloader(void);

#ifdef __cplusplus
}
#endif
