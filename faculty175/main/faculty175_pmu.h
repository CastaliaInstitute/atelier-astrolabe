#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool present;
    bool battery_present;
    bool vbus_in;
    bool charging;
    bool discharging;
    int battery_percent;
    uint16_t battery_mv;
} faculty175_pmu_status_t;

#ifdef __cplusplus
extern "C" {
#endif

/** Enable Waveshare AXP2101 rails (MIC BLDO2, display BLDO1, core DC3, etc.). */
esp_err_t faculty175_pmu_init(void);

/** Latest AXP2101 battery/USB snapshot; returns false when PMU init failed. */
bool faculty175_pmu_status(faculty175_pmu_status_t *out);

/** True once when the AXP2101 PEKEY reports a long press. */
bool faculty175_pmu_pekey_long_press(void);

#ifdef __cplusplus
}
#endif
