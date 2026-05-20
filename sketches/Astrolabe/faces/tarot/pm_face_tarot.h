#pragma once

#include <cstddef>
#include <ctime>

/** Major Arcana tarot face. Daily card by default; swipe up/down browses the deck. */
void pm_face_tarot_draw(const struct tm *tm_local, bool valid_local);
bool pm_face_tarot_cycle(int delta);
void pm_face_tarot_reset_daily(void);
int pm_face_tarot_index(const struct tm *tm_local, bool valid_local);
const char *pm_face_tarot_title(int idx);
const char *pm_face_tarot_manifest_url(void);
