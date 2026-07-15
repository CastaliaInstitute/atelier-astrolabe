#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    uint16_t heart_rate_bpm;
    uint16_t hrv_ms;
    uint8_t battery_percent;
    uint64_t updated_ms;
    bool heart_rate_valid;
    bool hrv_valid;
    bool battery_valid;
} faculty175_ring_vitals_t;

void faculty175_ring_update_vitals(const faculty175_ring_vitals_t *vitals);
bool faculty175_ring_latest_vitals(faculty175_ring_vitals_t *out);
esp_err_t faculty175_ring_handle_json(const char *json);
