#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t faculty175_usb_init(void);
esp_err_t faculty175_usb_set_screen_face_active(bool active);
bool faculty175_usb_screen_profile_active(void);
esp_err_t faculty175_usb_storage_claim(void);
bool faculty175_usb_storage_ready(void);
bool faculty175_usb_storage_mounted(void);
const char *faculty175_usb_storage_base_path(void);
bool faculty175_usb_storage_resolve_path(const char *relative_path, char *out, size_t cap);

#ifdef __cplusplus
}
#endif
