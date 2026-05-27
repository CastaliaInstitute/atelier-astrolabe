#pragma once

#include <cstdint>

void pm_face_drone_draw(void);
void pm_face_drone_stop(void);
bool pm_face_drone_toggle_at(int16_t x, int16_t y);
void pm_face_drone_cycle_root(int delta);
bool pm_face_drone_anim_tick(uint32_t now_ms);
const char *pm_face_drone_label(void);
