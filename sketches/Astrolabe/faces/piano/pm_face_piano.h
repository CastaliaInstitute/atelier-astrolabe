#pragma once

#include <cstdint>

void pm_face_piano_draw(void);
void pm_face_piano_stop(void);
bool pm_face_piano_play_at(int16_t x, int16_t y);
bool pm_face_piano_anim_tick(uint32_t now_ms);
const char *pm_face_piano_note_label(void);
int pm_face_piano_note_index(void);
