#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "wear_levelling.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t faculty175_storage_init(void);
bool faculty175_storage_ready(void);
const char *faculty175_storage_base_path(void);
const char *faculty175_storage_media_base_path(void);
wl_handle_t faculty175_storage_wl_handle(void);
bool faculty175_storage_resolve_path(const char *relative_path, char *out, size_t cap);
bool faculty175_storage_media_resolve_path(const char *relative_path, char *out, size_t cap);

#ifdef __cplusplus
}
#endif
