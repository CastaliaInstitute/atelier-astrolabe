#pragma once

#include <cstdint>

void pm_face_tuning_on_enter(void);
void pm_face_tuning_on_leave(void);
bool pm_face_tuning_tick(uint32_t now_ms);
void pm_face_tuning_draw(void);
const char *pm_face_tuning_note_label(void);
