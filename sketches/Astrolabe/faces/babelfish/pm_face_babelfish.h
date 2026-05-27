#pragma once

#include <cstddef>

/** Babel Fish translator face: fish display + STT/LLM/TTS translation prompt. */
void pm_face_babelfish_draw(void);
void pm_face_babelfish_draw_voice_screen(const char *status, float thinking_progress = -1.f,
                                          bool speaking = false);
bool pm_face_babelfish_build_system_prompt(char *out, size_t cap);

