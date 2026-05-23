#pragma once

#include <ctime>

/** 36-card Lenormand face using the bundled monochrome Noto Emoji glyph set. */
void pm_face_lenormand_draw(const struct tm *tm_local, bool valid_local);
bool pm_face_lenormand_cycle(int delta);
void pm_face_lenormand_reset_daily(void);
int pm_face_lenormand_index(const struct tm *tm_local, bool valid_local);
const char *pm_face_lenormand_title(int idx);
const char *pm_face_lenormand_keyword(int idx);
