#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool active;
    bool auto_started;
    bool auto_paused;
    bool test_locked;
    bool network_ready;
    bool heap_ready;
    uint32_t auto_interval_s;
    uint32_t last_poll_uptime_ms;
    char last[160];
} faculty175_ota_status_t;

void faculty175_ota_init(void);
bool faculty175_ota_handle(const char *line);
void faculty175_ota_maybe_boot_product(void);
void faculty175_ota_maybe_start_recovery_request(void);
void faculty175_ota_start_auto_update_task(void);
void faculty175_ota_set_auto_paused(bool paused);
void faculty175_ota_set_network_ready(bool ready);
bool faculty175_ota_active(void);
void faculty175_ota_get_status(faculty175_ota_status_t *out);

#ifdef __cplusplus
}
#endif
