#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

void faculty175_solar_image_request(bool force);
bool faculty175_solar_image_draw_cached(void);
bool faculty175_solar_image_copy_cached(uint16_t *out, size_t pixel_count, time_t *cached_epoch_out);
bool faculty175_solar_image_busy(void);
bool faculty175_solar_image_has_cached(void);
bool faculty175_solar_image_action(uint32_t seed_ms);
const char *faculty175_solar_image_error(void);
const char *faculty175_solar_image_channel(void);
time_t faculty175_solar_image_cached_epoch(void);

#ifdef __cplusplus
}
#endif
