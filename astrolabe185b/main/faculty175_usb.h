#pragma once

#include <stdbool.h>
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

#ifdef __cplusplus
}
#endif
