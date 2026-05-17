#pragma once

#include <cstddef>

struct tm;

void pm_face_synastry_draw(const struct tm *tm_local, bool valid_local);
void pm_face_synastry_draw_voice_screen(const char *status, float thinking_progress = -1.f);

bool pm_face_synastry_cycle_target(int delta);
bool pm_face_synastry_build_voice_message(char *buf, size_t cap);
bool pm_face_synastry_build_system_prompt(char *voice_msg, size_t voice_cap, char *sys_out, size_t sys_cap);
