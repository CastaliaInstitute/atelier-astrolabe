#include "faculty175_power_metrics.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"

#define POWER_ESTIMATE_MIN_MS (15u * 60u * 1000u)
#define POWER_STREAM_MAX_HZ 5.0f

static portMUX_TYPE s_power_metrics_mux = portMUX_INITIALIZER_UNLOCKED;
static faculty175_power_metrics_t s_metrics = {
    .stream_hz = 0.2f,
};
static uint32_t s_started_ms;
static uint32_t s_last_update_ms;
static uint32_t s_last_stream_ms;
static uint32_t s_discharge_started_ms;
static int s_discharge_started_percent = -1;

const char *faculty175_power_mode_name(faculty175_power_mode_t mode)
{
    switch (mode) {
        case FACULTY175_POWER_BREATHING: return "breathing";
        case FACULTY175_POWER_DIMMED: return "dimmed";
        case FACULTY175_POWER_ASLEEP: return "asleep";
        case FACULTY175_POWER_AWAKE:
        default: return "awake";
    }
}

void faculty175_power_metrics_update(uint32_t now_ms,
                                     const faculty175_pmu_status_t *pmu,
                                     faculty175_power_mode_t mode,
                                     bool wifi_active)
{
    portENTER_CRITICAL(&s_power_metrics_mux);
    if (s_started_ms == 0u) {
        s_started_ms = now_ms;
        s_last_update_ms = now_ms;
    }
    const uint32_t dt_ms = now_ms - s_last_update_ms;
    s_last_update_ms = now_ms;
    s_metrics.uptime_ms = now_ms - s_started_ms;
    s_metrics.mode = mode;
    s_metrics.wifi_active = wifi_active;
    if (pmu != NULL) {
        s_metrics.pmu = *pmu;
    }
    switch (mode) {
        case FACULTY175_POWER_BREATHING: s_metrics.breathing_ms += dt_ms; break;
        case FACULTY175_POWER_DIMMED: s_metrics.dimmed_ms += dt_ms; break;
        case FACULTY175_POWER_ASLEEP: s_metrics.asleep_ms += dt_ms; break;
        case FACULTY175_POWER_AWAKE:
        default: s_metrics.awake_ms += dt_ms; break;
    }

    const bool on_battery = s_metrics.pmu.present && s_metrics.pmu.battery_present &&
                            !s_metrics.pmu.vbus_in && !s_metrics.pmu.charging;
    const int percent = s_metrics.pmu.battery_percent;
    if (!on_battery || percent < 0 || percent > 100) {
        s_discharge_started_ms = 0u;
        s_discharge_started_percent = -1;
        s_metrics.discharge_elapsed_ms = 0u;
        s_metrics.discharge_drop_percent = 0;
        s_metrics.discharge_percent_per_hour = 0.0f;
        s_metrics.remaining_hours = 0.0f;
        s_metrics.estimate_valid = false;
    } else {
        if (s_discharge_started_ms == 0u || s_discharge_started_percent < percent) {
            s_discharge_started_ms = now_ms;
            s_discharge_started_percent = percent;
        }
        s_metrics.discharge_elapsed_ms = now_ms - s_discharge_started_ms;
        s_metrics.discharge_drop_percent = s_discharge_started_percent - percent;
        s_metrics.estimate_valid = s_metrics.discharge_elapsed_ms >= POWER_ESTIMATE_MIN_MS &&
                                   s_metrics.discharge_drop_percent >= 1;
        if (s_metrics.estimate_valid) {
            s_metrics.discharge_percent_per_hour =
                (float)s_metrics.discharge_drop_percent * 3600000.0f /
                (float)s_metrics.discharge_elapsed_ms;
            s_metrics.remaining_hours = s_metrics.discharge_percent_per_hour > 0.0f
                                            ? (float)percent / s_metrics.discharge_percent_per_hour
                                            : 0.0f;
        }
    }
    portEXIT_CRITICAL(&s_power_metrics_mux);
}

void faculty175_power_metrics_status(faculty175_power_metrics_t *out)
{
    if (out == NULL) {
        return;
    }
    portENTER_CRITICAL(&s_power_metrics_mux);
    *out = s_metrics;
    portEXIT_CRITICAL(&s_power_metrics_mux);
}

bool faculty175_power_metrics_stream_set(float hz)
{
    if (hz < 0.0f || hz > POWER_STREAM_MAX_HZ) {
        return false;
    }
    portENTER_CRITICAL(&s_power_metrics_mux);
    s_metrics.stream_hz = hz;
    s_last_stream_ms = 0u;
    portEXIT_CRITICAL(&s_power_metrics_mux);
    return true;
}

void faculty175_power_metrics_stream_maybe_emit(uint32_t now_ms)
{
    faculty175_power_metrics_t status = {0};
    uint32_t interval_ms = 0u;
    bool emit = false;
    portENTER_CRITICAL(&s_power_metrics_mux);
    if (s_metrics.stream_hz > 0.0f) {
        interval_ms = (uint32_t)(1000.0f / s_metrics.stream_hz);
        if (interval_ms == 0u) {
            interval_ms = 1u;
        }
        if (s_last_stream_ms == 0u || now_ms - s_last_stream_ms >= interval_ms) {
            s_last_stream_ms = now_ms;
            status = s_metrics;
            emit = true;
        }
    }
    portEXIT_CRITICAL(&s_power_metrics_mux);
    if (!emit) {
        return;
    }

    const bool on_battery = status.pmu.present && status.pmu.battery_present &&
                            !status.pmu.vbus_in && !status.pmu.charging;
    printf("power_csv,%lu,%s,%s,%d,%u,%d,%d,%d,%lu,%lu,%lu,%lu,%lu,%d,%lu,%.3f,%.2f,%d\n",
           (unsigned long)now_ms,
           faculty175_power_mode_name(status.mode),
           on_battery ? "battery" : "usb",
           status.pmu.battery_percent,
           (unsigned)status.pmu.battery_mv,
           status.pmu.charging ? 1 : 0,
           status.pmu.discharging ? 1 : 0,
           status.wifi_active ? 1 : 0,
           (unsigned long)(status.uptime_ms / 1000u),
           (unsigned long)(status.awake_ms / 1000u),
           (unsigned long)(status.breathing_ms / 1000u),
           (unsigned long)(status.dimmed_ms / 1000u),
           (unsigned long)(status.asleep_ms / 1000u),
           status.discharge_drop_percent,
           (unsigned long)(status.discharge_elapsed_ms / 1000u),
           (double)status.discharge_percent_per_hour,
           (double)status.remaining_hours,
           status.estimate_valid ? 1 : 0);
    fflush(stdout);
}
