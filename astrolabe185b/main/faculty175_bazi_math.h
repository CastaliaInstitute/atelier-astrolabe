#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t stem[4];
    uint8_t branch[4];
} faculty175_bazi_chart_t;

/*
 * Compute the four pillars from local civil time and tropical solar longitude.
 * Solar longitude is supplied by the shared ephemeris so the month/year
 * transitions follow Li Chun and the other 11 jie boundaries instead of
 * Gregorian months.  The compact firmware ephemeris is not a replacement for
 * an almanac at a term boundary, so callers should treat a boundary minute as
 * approximate.
 */
bool faculty175_bazi_calculate(int year, int month, int day, int hour, int minute,
                               double tropical_sun_longitude,
                               faculty175_bazi_chart_t *out);
