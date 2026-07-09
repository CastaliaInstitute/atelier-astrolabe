#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

bool faculty175_pocketwatch_background_enabled(void);
esp_err_t faculty175_pocketwatch_background_set_enabled(bool enabled);
bool faculty175_pocketwatch_draw_background(void);
const uint16_t *faculty175_pocketwatch_background_pixels(void);
bool faculty175_pocketwatch_handle(const char *line);
int faculty175_pocketwatch_hour_at(int16_t x, int16_t y);
bool faculty175_pocketwatch_profile_tap(int16_t x, int16_t y, uint32_t now_ms, char *out_slug, size_t out_cap);

#ifdef __cplusplus
}
#endif
