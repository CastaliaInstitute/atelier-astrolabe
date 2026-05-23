#pragma once

#include <stddef.h>

/** Delphi oracle face: Pythia bust + deliberately oblique voice prompt. */
void pm_face_pythia_draw(void);
void pm_face_pythia_draw_voice_screen(const char *status, float thinking_progress = -1.f, bool speaking = false);
bool pm_face_pythia_build_system_prompt(char *out, size_t cap);
