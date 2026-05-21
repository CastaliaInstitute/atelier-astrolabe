#pragma once

#include <cstddef>
#include <ctime>

/** Three-rune past / present / future fortune face. Tap casts a fresh spread and speaks it. */
void pm_face_runes_draw(const struct tm *tm_local, bool valid_local);
void pm_face_runes_draw_voice_screen(const char *status, float thinking_progress = -1.f);
void pm_face_runes_cast(const struct tm *tm_local, bool valid_local);
bool pm_face_runes_build_fortune_message(char *out, size_t cap);
bool pm_face_runes_build_system_prompt(char *out, size_t cap);
const char *pm_face_runes_slot_label(int slot);
const char *pm_face_runes_name(int slot);
const char *pm_face_runes_keyword(int slot);
