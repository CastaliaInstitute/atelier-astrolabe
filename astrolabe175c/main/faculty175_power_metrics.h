#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "faculty175_pmu.h"

typedef enum {
    FACULTY175_POWER_AWAKE = 0,
    FACULTY175_POWER_BREATHING,
    FACULTY175_POWER_DIMMED,
    FACULTY175_POWER_ASLEEP,
} faculty175_power_mode_t;

typedef struct {
    faculty175_pmu_status_t pmu;
    faculty175_power_mode_t mode;
    bool wifi_active;
    uint32_t uptime_ms;
    uint32_t awake_ms;
    uint32_t breathing_ms;
    uint32_t dimmed_ms;
    uint32_t asleep_ms;
    uint32_t discharge_elapsed_ms;
    int discharge_drop_percent;
    float discharge_percent_per_hour;
    float remaining_hours;
    bool estimate_valid;
    float stream_hz;
} faculty175_power_metrics_t;

void faculty175_power_metrics_update(uint32_t now_ms,
                                     const faculty175_pmu_status_t *pmu,
                                     faculty175_power_mode_t mode,
                                     bool wifi_active);
void faculty175_power_metrics_status(faculty175_power_metrics_t *out);
bool faculty175_power_metrics_stream_set(float hz);
void faculty175_power_metrics_stream_maybe_emit(uint32_t now_ms);
const char *faculty175_power_mode_name(faculty175_power_mode_t mode);
