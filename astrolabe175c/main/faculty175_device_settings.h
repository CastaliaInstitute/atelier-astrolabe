#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool valid;
    double lat_deg;
    double lon_deg;
    int64_t updated_epoch;
    char source[32];
} faculty175_location_settings_t;

esp_err_t faculty175_location_settings_load(faculty175_location_settings_t *out);
esp_err_t faculty175_location_settings_save(double lat_deg, double lon_deg, const char *source);
bool faculty175_location_settings_valid(double lat_deg, double lon_deg);

#ifdef __cplusplus
}
#endif
