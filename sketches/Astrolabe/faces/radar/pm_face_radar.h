#pragma once

#include <cstdint>

struct tm;

void pm_face_radar_draw(const struct tm *tm, bool valid);
void pm_face_radar_on_enter(void);
void pm_face_radar_on_leave(void);
void pm_face_radar_tick(uint32_t now_ms);
