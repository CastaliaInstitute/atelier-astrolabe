#pragma once

#include <cstdint>

void pm_face_level_draw(void);
bool pm_face_level_anim_tick(uint32_t now_ms);
const char *pm_face_level_guidance(void);
bool pm_face_level_offset(float *x, float *y);
