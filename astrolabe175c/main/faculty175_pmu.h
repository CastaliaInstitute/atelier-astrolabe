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

/** Initialize the Waveshare AXP2101 and board power rails. */
esp_err_t faculty175_pmu_init(void);

/** Latest AXP2101 battery/USB snapshot; returns false when PMU init failed. */
bool faculty175_pmu_status(faculty175_pmu_status_t *out);

/** True once when the AXP2101 PEKEY reports a long press. */
bool faculty175_pmu_pekey_long_press(void);

/** Disable only confirmed-unused PMU outputs before ESP32 deep sleep. */
void faculty175_pmu_prepare_deep_sleep(void);

#ifdef __cplusplus
}
#endif
