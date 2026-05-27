#pragma once

#include <cstddef>
#include <cstdint>

#include "pm_gesture.h"

void pm_face_hid_draw(void);
void pm_face_hid_on_enter(void);
void pm_face_hid_on_leave(void);
bool pm_face_hid_touch_tick(uint32_t now_ms);
bool pm_face_hid_on_gesture(PmGestureKind kind, int16_t x, int16_t y, char *banner, size_t banner_cap);
