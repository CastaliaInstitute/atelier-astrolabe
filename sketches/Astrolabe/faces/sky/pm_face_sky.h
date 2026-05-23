#pragma once

#include <ctime>
#include <stdint.h>

void pm_face_sky_draw(const struct tm *local, bool valid_time);
bool pm_face_sky_anim_tick(uint32_t now_ms);
bool pm_face_sky_touch_tick(uint32_t now_ms);
void pm_face_sky_on_leave(void);
