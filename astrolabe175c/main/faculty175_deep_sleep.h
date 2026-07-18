#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "faculty175_pmu.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool retained;
    bool completed;
    bool pending;
    uint32_t requested_sleep_s;
    uint32_t started_epoch_s;
    int start_battery_percent;
    uint16_t start_battery_mv;
    int wake_cause;
} faculty175_deep_sleep_status_t;

/** Arm a battery-only deep-sleep interval. Entry occurs after VBUS is removed. */
bool faculty175_deep_sleep_request(uint32_t sleep_s);
void faculty175_deep_sleep_cancel(void);
bool faculty175_deep_sleep_request_pending(void);
void faculty175_deep_sleep_status(faculty175_deep_sleep_status_t *out);

/** Consume an armed request and enter deep sleep. This function does not return. */
void faculty175_deep_sleep_enter(const faculty175_pmu_status_t *pmu);

#ifdef __cplusplus
}
#endif
