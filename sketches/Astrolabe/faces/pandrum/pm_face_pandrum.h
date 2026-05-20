#pragma once

#include <cstdint>

void pm_face_pandrum_draw(void);
void pm_face_pandrum_stop(void);
bool pm_face_pandrum_play_at(int16_t x, int16_t y);
bool pm_face_pandrum_anim_tick(uint32_t now_ms);
const char *pm_face_pandrum_note_label(void);
int pm_face_pandrum_note_index(void);
float pm_face_pandrum_last_hz(void);
