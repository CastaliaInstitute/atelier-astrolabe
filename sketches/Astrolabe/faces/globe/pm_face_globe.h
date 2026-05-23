#pragma once

#include <ctime>

void pm_face_globe_draw(const struct tm *local, bool valid_time);
bool pm_face_globe_anim_tick(uint32_t now_ms);
