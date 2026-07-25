#include "faculty175_bazi_math.h"

#include <math.h>
#include <stddef.h>

static int positive_mod(int value, int modulo)
{
    const int result = value % modulo;
    return result < 0 ? result + modulo : result;
}

static int day_index(int year, int month, int day)
{
    if (month < 3) {
        --year;
        month += 12;
    }
    return 365 * year + year / 4 - year / 100 + year / 400 +
           (153 * (month - 3) + 2) / 5 + day;
}

bool faculty175_bazi_calculate(int year, int month, int day, int hour, int minute,
                               double tropical_sun_longitude,
                               faculty175_bazi_chart_t *out)
{
    if (out == NULL || year < 1 || month < 1 || month > 12 || day < 1 ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
        !isfinite(tropical_sun_longitude)) {
        return false;
    }

    const double sun = fmod(tropical_sun_longitude, 360.0) < 0.0
                           ? fmod(tropical_sun_longitude, 360.0) + 360.0
                           : fmod(tropical_sun_longitude, 360.0);
    const bool after_li_chun = sun >= 315.0;
    const int solar_year = year - (after_li_chun ? 0 : 1);
    const int year_stem = positive_mod(solar_year - 4, 10);
    const int year_branch = positive_mod(solar_year - 4, 12);

    /* 315° is Li Chun / Tiger month; every 30° advances one branch. */
    const int month_branch = positive_mod(2 + (int)floor(fmod(sun - 315.0 + 360.0, 360.0) / 30.0), 12);
    const int tiger_stem = positive_mod(2 + 2 * (year_stem % 5), 10);
    const int month_stem = positive_mod(tiger_stem + positive_mod(month_branch - 2, 12), 10);

    /* 2000-01-01 is the 戊午 (4,6) day; this fixes the cycle phase. */
    const int ordinal = day_index(year, month, day);
    const int day_stem = positive_mod(ordinal + 8, 10);
    const int day_branch = positive_mod(ordinal + 8, 12);

    /* Zi hour is 23:00–00:59; the hour stem follows the day stem group. */
    const int minutes = hour * 60 + minute;
    const int hour_branch = positive_mod((minutes + 60) / 120, 12);
    const int hour_stem = positive_mod((day_stem % 5) * 2 + hour_branch, 10);

    out->stem[0] = (uint8_t)year_stem;
    out->stem[1] = (uint8_t)month_stem;
    out->stem[2] = (uint8_t)day_stem;
    out->stem[3] = (uint8_t)hour_stem;
    out->branch[0] = (uint8_t)year_branch;
    out->branch[1] = (uint8_t)month_branch;
    out->branch[2] = (uint8_t)day_branch;
    out->branch[3] = (uint8_t)hour_branch;
    return true;
}
