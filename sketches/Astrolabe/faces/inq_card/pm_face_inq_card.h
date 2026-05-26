#pragma once

#include <ctime>

/** iNQ Card of the Day from cards.castalia.institute. */
void pm_face_inq_card_draw(const struct tm *tm_local, bool valid_local);
const char *pm_face_inq_card_title(void);
const char *pm_face_inq_card_date(void);
