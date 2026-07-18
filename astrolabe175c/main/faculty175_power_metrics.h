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

typedef enum {
    FACULTY175_POWER_SCENARIO_NORMAL = 0,
    FACULTY175_POWER_SCENARIO_FULL_WIFI,
    FACULTY175_POWER_SCENARIO_FULL_OFFLINE,
    FACULTY175_POWER_SCENARIO_DIM_WIFI,
    FACULTY175_POWER_SCENARIO_DIM_OFFLINE,
    FACULTY175_POWER_SCENARIO_OFF_WIFI,
    FACULTY175_POWER_SCENARIO_SLEEP_OFFLINE,
} faculty175_power_scenario_t;

typedef struct {
    faculty175_pmu_status_t pmu;
    faculty175_power_mode_t mode;
    bool wifi_active;
    bool ble_active;
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
    faculty175_power_scenario_t scenario;
    uint32_t scenario_elapsed_ms;
    uint32_t scenario_remaining_ms;
    uint32_t scenario_awake_ms;
    uint32_t scenario_breathing_ms;
    uint32_t scenario_dimmed_ms;
    uint32_t scenario_asleep_ms;
    uint32_t scenario_battery_ms;
    uint32_t scenario_wifi_ms;
    uint32_t scenario_ble_ms;
} faculty175_power_metrics_t;

void faculty175_power_metrics_update(uint32_t now_ms,
                                     const faculty175_pmu_status_t *pmu,
                                     faculty175_power_mode_t mode,
                                     bool wifi_active,
                                     bool ble_active);
void faculty175_power_metrics_status(faculty175_power_metrics_t *out);
bool faculty175_power_metrics_stream_set(float hz);
void faculty175_power_metrics_stream_maybe_emit(uint32_t now_ms);
const char *faculty175_power_mode_name(faculty175_power_mode_t mode);
const char *faculty175_power_scenario_name(faculty175_power_scenario_t scenario);
bool faculty175_power_scenario_parse(const char *name, faculty175_power_scenario_t *out);
bool faculty175_power_scenario_set(faculty175_power_scenario_t scenario,
                                   uint32_t now_ms,
                                   uint32_t duration_ms);
faculty175_power_scenario_t faculty175_power_scenario_get(uint32_t now_ms);
bool faculty175_power_scenario_wifi_enabled(faculty175_power_scenario_t scenario);
faculty175_power_mode_t faculty175_power_scenario_mode(faculty175_power_scenario_t scenario);
