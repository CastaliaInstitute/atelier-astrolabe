#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "faculty175_pmu.h"

/* Forty-minute samples retain 170 hours, slightly longer than the seven-day
 * qualification ceiling, without waking the HTTP server during battery-only
 * runs. 255 is the largest ring representable by the persisted uint8 indices. */
#define FACULTY175_POWER_HISTORY_CAPACITY 255

typedef struct {
    uint32_t epoch_s;
    uint16_t battery_mv;
    uint8_t battery_percent;
    uint8_t flags;
    uint8_t mode;
    uint8_t scenario;
} faculty175_power_history_sample_t;

typedef struct {
    bool valid;
    uint32_t elapsed_s;
    int drop_percent;
    float percent_per_hour;
    float remaining_hours;
} faculty175_power_history_estimate_t;

enum {
    FACULTY175_POWER_HISTORY_BATTERY_PRESENT = 1u << 0,
    FACULTY175_POWER_HISTORY_VBUS = 1u << 1,
    FACULTY175_POWER_HISTORY_CHARGING = 1u << 2,
    FACULTY175_POWER_HISTORY_DISCHARGING = 1u << 3,
    FACULTY175_POWER_HISTORY_WIFI = 1u << 4,
    FACULTY175_POWER_HISTORY_BLE = 1u << 5,
};

/** Persist a periodic or state-transition sample when wall-clock time is valid. */
void faculty175_power_history_maybe_record(const faculty175_pmu_status_t *pmu,
                                           uint8_t mode,
                                           uint8_t scenario,
                                           bool wifi_active,
                                           bool ble_active);

/** Copy retained samples oldest-first. */
size_t faculty175_power_history_load(faculty175_power_history_sample_t *out, size_t capacity);

/** Estimate the current continuous battery discharge segment from retained samples. */
bool faculty175_power_history_estimate(const faculty175_pmu_status_t *pmu,
                                       faculty175_power_history_estimate_t *out);
