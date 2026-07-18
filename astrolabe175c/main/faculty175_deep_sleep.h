#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "faculty175_pmu.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FACULTY175_DEEP_SLEEP_PREP_AUDIO   (1u << 0)
#define FACULTY175_DEEP_SLEEP_PREP_DISPLAY (1u << 1)
#define FACULTY175_DEEP_SLEEP_PREP_PMU     (1u << 2)
#define FACULTY175_DEEP_SLEEP_PREP_ALL     (FACULTY175_DEEP_SLEEP_PREP_AUDIO | \
                                            FACULTY175_DEEP_SLEEP_PREP_DISPLAY | \
                                            FACULTY175_DEEP_SLEEP_PREP_PMU)

typedef struct {
    bool retained;
    bool completed;
    bool pending;
    uint32_t requested_sleep_s;
    uint32_t started_epoch_s;
    int start_battery_percent;
    uint16_t start_battery_mv;
    uint16_t prepare_flags;
    int wake_cause;
} faculty175_deep_sleep_status_t;

/** Arm a battery-only deep-sleep interval. Entry occurs after VBUS is removed. */
bool faculty175_deep_sleep_request(uint32_t sleep_s);
void faculty175_deep_sleep_cancel(void);
bool faculty175_deep_sleep_request_pending(void);
void faculty175_deep_sleep_status(faculty175_deep_sleep_status_t *out);

/**
 * Consume an armed request and enter deep sleep.
 *
 * Returns false only when the request or power preconditions are rejected
 * before any board subsystem is quiesced. Once shutdown begins, the function
 * either enters deep sleep or restarts on a preparation failure.
 */
bool faculty175_deep_sleep_enter(const faculty175_pmu_status_t *pmu);

#ifdef __cplusplus
}
#endif
