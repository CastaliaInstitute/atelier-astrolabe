#pragma once

#include <cstddef>
#include <cstdint>

void pm_face_biometrics_on_enter(void);
void pm_face_biometrics_on_leave(void);
bool pm_face_biometrics_anim_tick(uint32_t now_ms);
void pm_face_biometrics_draw(void);
void pm_face_biometrics_format_prompt_state(char *out, size_t cap);
void pm_face_biometrics_pause_ble_for_voice(void);
