#pragma once

#include <cstdint>

void pm_face_ocarina_draw(void);
void pm_face_ocarina_on_enter(void);
void pm_face_ocarina_on_leave(void);
void pm_face_ocarina_stop(void);
bool pm_face_ocarina_play_at(int16_t x, int16_t y);
bool pm_face_ocarina_touch_tick(int16_t x, int16_t y, bool down, uint32_t now_ms);
bool pm_face_ocarina_breath_tick(uint32_t now_ms);
void pm_face_ocarina_cycle_key(int delta);
bool pm_face_ocarina_anim_tick(uint32_t now_ms);
const char *pm_face_ocarina_key_label(void);
int pm_face_ocarina_note_index(void);
int pm_face_ocarina_selected_index(void);
