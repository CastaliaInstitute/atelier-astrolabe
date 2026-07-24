#pragma once

#include <stdbool.h>
#include <stdint.h>

void faculty175_face_psych_state_draw(uint32_t anim_ms);
bool faculty175_face_psych_state_action(uint32_t seed_ms);
bool faculty175_face_psych_state_tap(int16_t x, int16_t y);
bool faculty175_face_psych_state_style_delta(int delta);
bool faculty175_face_psych_state_set_mood(const char *label, bool check_in);
const char *faculty175_face_psych_state_mood_label(void);
void faculty175_face_psych_state_mood_values(uint8_t *arousal, uint8_t *valence);
