#pragma once

#include <cstddef>
#include <cstdint>

/** Alethiometer face: 36 symbols, three question needles, one answer needle. */
void pm_face_alethiometer_draw(void);
void pm_face_alethiometer_draw_voice_screen(const char *status, float thinking_progress = -1.f);
bool pm_face_alethiometer_anim_tick(uint32_t now_ms);
void pm_face_alethiometer_seed_from_text(const char *question, const char *reply);
bool pm_face_alethiometer_build_system_prompt(char *out, size_t cap);
const char *pm_face_alethiometer_symbol_name(int idx);
const char *pm_face_alethiometer_needle_symbol_name(int needle);
