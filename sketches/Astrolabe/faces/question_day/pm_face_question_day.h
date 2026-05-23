#pragma once

#include <cstddef>
#include <cstdint>

#include "pm_faculty.h"

void pm_face_question_day_draw(void);
void pm_face_question_day_draw_recording(float progress, uint32_t elapsed_ms);
void pm_face_question_day_draw_voice_screen(const char *status, float thinking_progress = -1.f);

const char *pm_face_question_day_current(void);
bool pm_face_question_day_faculty(PmFacultyProfile *out);
bool pm_face_question_day_set_current(const char *question);
bool pm_face_question_day_build_prompt(char *msg, size_t msg_cap, char *sys, size_t sys_cap);
bool pm_face_question_day_build_answer_system_prompt(char *sys, size_t sys_cap);
