#include "faculty175_relationship_weather.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "astrolabe_time.h"

/* Zero means "follow today"; otherwise this stores a timezone-neutral civil
 * day ordinal. A 32-bit value is atomic on ESP32-S3, so BLE selection and the
 * display task cannot observe a torn 64-bit epoch. */
static volatile int32_t s_selected_day;

static double norm360(double value)
{
    value = fmod(value, 360.0);
    return value < 0.0 ? value + 360.0 : value;
}

static double separation(double a, double b)
{
    double distance = fabs(norm360(a) - norm360(b));
    return distance > 180.0 ? 360.0 - distance : distance;
}

/* Howard Hinnant's proleptic Gregorian civil-date transform. */
static int32_t days_from_civil(int year, unsigned month, unsigned day)
{
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = (unsigned)(year - era * 400);
    const unsigned month_prime = month > 2 ? month - 3u : month + 9u;
    const unsigned doy =
        (153u * month_prime + 2u) / 5u + day - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return (int32_t)(era * 146097 + (int)doe - 719468);
}

static bool leap_year(int year)
{
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

static bool parse_date(const char *date,
                       int *year_out,
                       unsigned *month_out,
                       unsigned *day_out)
{
    int year = 0;
    unsigned month = 0;
    unsigned day = 0;
    char tail = '\0';
    if (date == NULL ||
        sscanf(date, "%4d-%2u-%2u%c", &year, &month, &day, &tail) != 3 ||
        year < 1900 || year > 2100 || month < 1 || month > 12) {
        return false;
    }
    static const uint8_t month_days[] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31,
    };
    unsigned max_day = month_days[month - 1];
    if (month == 2 && leap_year(year)) {
        max_day = 29;
    }
    if (day < 1 || day > max_day) {
        return false;
    }
    if (year_out != NULL) *year_out = year;
    if (month_out != NULL) *month_out = month;
    if (day_out != NULL) *day_out = day;
    return true;
}

static int32_t today_day(void)
{
    struct tm local = {};
    if (astrolabe_time_valid()) {
        astrolabe_time_local(&local);
    } else {
        const time_t fallback = time(NULL);
        localtime_r(&fallback, &local);
    }
    return days_from_civil(local.tm_year + 1900,
                           (unsigned)local.tm_mon + 1u,
                           (unsigned)local.tm_mday);
}

static void format_day(int32_t day, char out[FACULTY175_RELATIONSHIP_DATE_LEN])
{
    const time_t epoch = (time_t)day * 86400 + 43200;
    struct tm utc = {};
    gmtime_r(&epoch, &utc);
    snprintf(out,
             FACULTY175_RELATIONSHIP_DATE_LEN,
             "%04d-%02d-%02d",
             utc.tm_year + 1900,
             utc.tm_mon + 1,
             utc.tm_mday);
}

faculty175_relationship_condition_t faculty175_relationship_weather_at(
    const faculty175_chart_positions_t *primary,
    const faculty175_chart_positions_t *target,
    time_t epoch)
{
    if (primary == NULL || target == NULL || !primary->ok || !target->ok ||
        epoch <= 0) {
        return FACULTY175_RELATIONSHIP_CHANGEABLE;
    }
    faculty175_chart_positions_t transit = {};
    if (!faculty175_charts_positions_at(epoch, &transit)) {
        return FACULTY175_RELATIONSHIP_CHANGEABLE;
    }
    float score = 0.0f;
    static const int aspects[] = {0, 60, 90, 120, 180};
    static const float tone[] = {0.35f, 0.65f, -0.82f, 1.0f, -0.62f};
    for (int body = 0; body < FACULTY175_CHART_BODY_COUNT; ++body) {
        for (int person = 0; person < 2; ++person) {
            const faculty175_chart_positions_t *natal =
                person == 0 ? primary : target;
            for (int natal_body = 0;
                 natal_body < FACULTY175_CHART_BODY_COUNT;
                 ++natal_body) {
                const double sep =
                    separation(transit.lon[body], natal->lon[natal_body]);
                for (size_t ai = 0;
                     ai < sizeof(aspects) / sizeof(aspects[0]);
                     ++ai) {
                    const double orb = fabs(sep - aspects[ai]);
                    if (orb <= 5.5) {
                        const float exact = 1.0f - (float)(orb / 5.5);
                        const float personal =
                            (body < 2 || natal_body < 2) ? 1.3f : 0.72f;
                        score += tone[ai] * exact * personal;
                        break;
                    }
                }
            }
        }
    }
    if (score >= 3.0f) return FACULTY175_RELATIONSHIP_OPEN;
    if (score >= 0.8f) return FACULTY175_RELATIONSHIP_EASY;
    if (score > -1.1f) return FACULTY175_RELATIONSHIP_CHANGEABLE;
    if (score > -3.2f) return FACULTY175_RELATIONSHIP_TENDER;
    return FACULTY175_RELATIONSHIP_INTENSE;
}

const char *faculty175_relationship_weather_name(
    faculty175_relationship_condition_t condition)
{
    static const char *const names[] = {
        "OPEN", "EASY", "CHANGEABLE", "TENDER", "INTENSE",
    };
    return condition >= 0 &&
            condition < FACULTY175_RELATIONSHIP_CONDITION_COUNT
        ? names[condition]
        : names[FACULTY175_RELATIONSHIP_CHANGEABLE];
}

const char *faculty175_relationship_weather_guidance(
    faculty175_relationship_condition_t condition)
{
    static const char *const guidance[] = {
        "Make the plan together",
        "Share the warmth; say the kind thing",
        "Stay curious and check assumptions",
        "Slow down; make room for feelings",
        "Protect the bond; pause before reacting",
    };
    return condition >= 0 &&
            condition < FACULTY175_RELATIONSHIP_CONDITION_COUNT
        ? guidance[condition]
        : guidance[FACULTY175_RELATIONSHIP_CHANGEABLE];
}

const char *faculty175_relationship_weather_symbol(
    faculty175_relationship_condition_t condition)
{
    static const char *const symbols[] = {"*", "o", "~", ":", "!"};
    return condition >= 0 &&
            condition < FACULTY175_RELATIONSHIP_CONDITION_COUNT
        ? symbols[condition]
        : symbols[FACULTY175_RELATIONSHIP_CHANGEABLE];
}

uint32_t faculty175_relationship_weather_color(
    faculty175_relationship_condition_t condition)
{
    static const uint32_t colors[] = {
        0xffcc58, 0xa9d9ff, 0x9da9bc, 0x5d91c9, 0x9a72d6,
    };
    return condition >= 0 &&
            condition < FACULTY175_RELATIONSHIP_CONDITION_COUNT
        ? colors[condition]
        : colors[FACULTY175_RELATIONSHIP_CHANGEABLE];
}

esp_err_t faculty175_relationship_weather_select_date(const char *date)
{
    if (date == NULL || date[0] == '\0') {
        faculty175_relationship_weather_select_today();
        return ESP_OK;
    }
    int year = 0;
    unsigned month = 0;
    unsigned day = 0;
    if (!parse_date(date, &year, &month, &day)) {
        return ESP_ERR_INVALID_ARG;
    }
    const int32_t selected = days_from_civil(year, month, day);
    const int32_t offset = selected - today_day();
    if (offset < -3650 || offset > 3650) {
        return ESP_ERR_INVALID_ARG;
    }
    s_selected_day = selected;
    return ESP_OK;
}

void faculty175_relationship_weather_select_today(void)
{
    s_selected_day = 0;
}

bool faculty175_relationship_weather_snapshot(
    faculty175_relationship_weather_snapshot_t *out)
{
    if (out == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    faculty175_charts_ensure_family_seed();
    faculty175_birth_chart_t primary = {};
    faculty175_birth_chart_t target = {};
    faculty175_chart_positions_t primary_pos = {};
    faculty175_chart_positions_t target_pos = {};
    if (!faculty175_charts_primary(&primary) ||
        !faculty175_charts_active(&target) ||
        !faculty175_charts_birth_positions(&primary, &primary_pos) ||
        !faculty175_charts_birth_positions(&target, &target_pos)) {
        return false;
    }
    const int32_t today = today_day();
    const int32_t selected = s_selected_day != 0 ? s_selected_day : today;
    const time_t epoch = (time_t)selected * 86400 + 43200;
    out->available = true;
    snprintf(out->primary_name, sizeof(out->primary_name), "%s", primary.name);
    snprintf(out->target_name, sizeof(out->target_name), "%s", target.name);
    format_day(selected, out->selected_date);
    out->active_slot = faculty175_charts_active_slot();
    out->offset_days = (int)(selected - today);
    out->selected_epoch = epoch;
    for (int day = 0; day < FACULTY175_RELATIONSHIP_ARC_DAYS; ++day) {
        out->arc[day] = faculty175_relationship_weather_at(
            &primary_pos,
            &target_pos,
            epoch + (time_t)day * 86400);
    }
    return true;
}
