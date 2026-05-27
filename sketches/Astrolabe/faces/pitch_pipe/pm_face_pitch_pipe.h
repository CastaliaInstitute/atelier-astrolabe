#pragma once

#include <cstdint>

void pm_face_pitch_pipe_draw(void);
void pm_face_pitch_pipe_on_enter(void);
void pm_face_pitch_pipe_on_leave(void);
bool pm_face_pitch_pipe_breath_tick(uint32_t now_ms);
bool pm_face_pitch_pipe_tap_at(int16_t x, int16_t y);
void pm_face_pitch_pipe_cycle(int delta);
bool pm_face_pitch_pipe_anim_tick(uint32_t now_ms);
void pm_face_pitch_pipe_stop(void);
const char *pm_face_pitch_pipe_note_label(void);
int pm_face_pitch_pipe_note_index(void);

