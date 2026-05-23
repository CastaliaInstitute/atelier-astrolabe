#pragma once

#include <ctime>

/** Daily geomantic figure face; swipe browses the 16 figures, tap returns daily. */
void pm_face_geomancy_draw(const struct tm *tm_local, bool valid_local);
int pm_face_geomancy_index(const struct tm *tm_local, bool valid_local);
bool pm_face_geomancy_cycle(int delta);
void pm_face_geomancy_cast_entropy(int16_t touch_x, int16_t touch_y);
void pm_face_geomancy_reset_daily(void);
const char *pm_face_geomancy_title(int idx);
const char *pm_face_geomancy_keyword(int idx);
