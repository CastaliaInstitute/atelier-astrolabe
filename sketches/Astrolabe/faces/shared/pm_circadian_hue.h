#pragma once

#include <cstdint>
#include <ctime>

/** Circadian palette from Hue Daywheel design (hour-of-day → RGB). */
uint16_t pm_circadian_color565_at_hour(float hour_local);
uint16_t pm_circadian_color565_at_unix(time_t unix_sec);

/** Poetic label for the current time-feeling (e.g. "Gold Hour"). */
const char *pm_circadian_hue_name_at_hour(float hour_local);
const char *pm_circadian_hue_name_now(void);
