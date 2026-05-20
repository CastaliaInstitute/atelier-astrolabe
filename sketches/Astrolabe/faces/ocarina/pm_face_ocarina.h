#pragma once

#include <cstdint>

void pm_face_ocarina_draw(void);
void pm_face_ocarina_stop(void);
bool pm_face_ocarina_play_at(int16_t x, int16_t y);
void pm_face_ocarina_cycle_key(int delta);
bool pm_face_ocarina_anim_tick(uint32_t now_ms);
const char *pm_face_ocarina_key_label(void);
int pm_face_ocarina_note_index(void);
