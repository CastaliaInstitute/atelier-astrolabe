#pragma once

#include <cstdint>
#include <ctime>

/** Circadian hue (degrees) from local seconds- or hour-of-day. */
float pm_circadian_hue_from_seconds(float seconds_of_day);
float pm_circadian_hue_from_hour(float hour_local);

/** Face fill / daywheel tint: hsl(hue, 45%, 8%). */
uint16_t pm_circadian_color565_at_hour(float hour_local);
uint16_t pm_circadian_color565_at_unix(time_t unix_sec);

/** Accent / labels: hsl(hue, 90%, 62%). */
uint16_t pm_circadian_accent565_at_hour(float hour_local);

/** Daily ritual band name (Nocturne … Oracle). */
const char *pm_circadian_hue_name_at_hour(float hour_local);
const char *pm_circadian_hue_name_now(void);
