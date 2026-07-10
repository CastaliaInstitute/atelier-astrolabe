#pragma once

#include <stdbool.h>

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

#ifdef __cplusplus
}
#endif
