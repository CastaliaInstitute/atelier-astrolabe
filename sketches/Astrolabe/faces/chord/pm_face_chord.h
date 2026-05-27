#pragma once

#include <cstdint>

void pm_face_chord_draw(void);
void pm_face_chord_stop(void);
bool pm_face_chord_play_at(int16_t x, int16_t y);
bool pm_face_chord_anim_tick(uint32_t now_ms);
const char *pm_face_chord_label(void);
