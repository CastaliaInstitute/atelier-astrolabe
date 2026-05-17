#pragma once

#include <cstddef>
#include <cstdint>

struct tm;

const char *pm_face_moon_phase_name(double el_deg);
void pm_face_moon_draw(const struct tm *tm_local, bool valid_local);
/** Moon + rainbow; optional status banner and/or thinking ring (0..1, or <0 for none). */
void pm_face_moon_draw_voice_screen(const char *status, float thinking_progress);
bool pm_face_moon_build_fortune_message(char *buf, size_t cap);
bool pm_face_moon_build_fortune_system_prompt(char *out, size_t cap);
bool pm_face_moon_build_system_prompt(char *out, size_t cap);

