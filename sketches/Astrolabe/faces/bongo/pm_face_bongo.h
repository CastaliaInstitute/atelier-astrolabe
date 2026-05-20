#pragma once

#include <cstdint>

void pm_face_bongo_draw(void);
void pm_face_bongo_stop(void);
bool pm_face_bongo_play_at(int16_t x, int16_t y);
bool pm_face_bongo_anim_tick(uint32_t now_ms);
float pm_face_bongo_last_hz(void);
float pm_face_bongo_last_radius_norm(void);
