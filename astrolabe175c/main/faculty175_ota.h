#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

void faculty175_ota_init(void);
bool faculty175_ota_handle(const char *line);
void faculty175_ota_maybe_boot_product(void);
void faculty175_ota_maybe_start_recovery_request(void);
void faculty175_ota_start_auto_update_task(void);
void faculty175_ota_set_auto_paused(bool paused);
bool faculty175_ota_active(void);
/** Start an OTA from a signed manifest. Empty URL uses the integration channel. */
esp_err_t faculty175_ota_start_manifest(const char *manifest_url);
/** Snapshot asynchronous OTA state for the Wi-Fi API. */
void faculty175_ota_status(char *state, size_t state_cap, char *last, size_t last_cap);

#ifdef __cplusplus
}
#endif
