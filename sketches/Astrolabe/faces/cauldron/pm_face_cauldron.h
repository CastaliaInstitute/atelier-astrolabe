#pragma once

#include <Arduino.h>

void pm_face_cauldron_draw(void);
bool pm_face_cauldron_anim_tick(uint32_t now_ms);
bool pm_face_cauldron_touch_tick(uint32_t now_ms);
void pm_face_cauldron_on_enter(void);
void pm_face_cauldron_on_leave(void);
