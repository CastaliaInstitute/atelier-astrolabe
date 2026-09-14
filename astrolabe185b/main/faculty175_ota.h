#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { bool active; bool network_ready; char last[160]; } faculty175_ota_status_t;
void faculty175_ota_get_status(faculty175_ota_status_t *out);
void faculty175_ota_set_network_ready(bool ready);
esp_err_t faculty175_ota_fetch_verified(const char *url, const char *sha256);
void faculty175_ota_init(void);
bool faculty175_ota_handle(const char *line);
void faculty175_ota_maybe_boot_product(void);
void faculty175_ota_maybe_start_recovery_request(void);
void faculty175_ota_start_auto_update_task(void);
bool faculty175_ota_active(void);

#ifdef __cplusplus
}
#endif
