#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

float faculty175_cycle_lunar_phase(uint32_t anim_ms);
float faculty175_cycle_solar_phase(uint32_t anim_ms);
float faculty175_cycle_solar_year_phase(uint32_t anim_ms);
const char *faculty175_cycle_lunar_label(float phase);
void faculty175_cycle_draw_lunar_arc(int cx, int cy, int radius, uint32_t anim_ms);
void faculty175_cycle_draw_solar_arc(int cx, int cy, int radius, uint32_t anim_ms);
void faculty175_cycle_draw_solar_year_arc(int cx, int cy, int radius, uint32_t anim_ms);

#ifdef __cplusplus
}
#endif
