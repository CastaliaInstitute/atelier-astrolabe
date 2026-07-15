#include "faculty175_cycle_arcs.h"

#include <math.h>
#include <stdbool.h>
#include <time.h>

#include "astrolabe_time.h"
#include "faculty175_board.h"

#define TAU_F 6.28318530718f
#define LUNAR_SYNODIC_SECONDS 2551443.0
#define SOLAR_CYCLE_SECONDS (365.2425 * 86400.0 * 11.0)
#define SOLAR_YEAR_SECONDS (365.2425 * 86400.0)

static uint16_t rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return faculty175_display_rgb888(r, g, b);
}

static double wrap01(double value)
{
    value = fmod(value, 1.0);
    return value < 0.0 ? value + 1.0 : value;
}

static time_t cycle_epoch(uint32_t anim_ms)
{
    if (astrolabe_time_valid()) {
        return astrolabe_time_now();
    }
    return (time_t)(ASTROLABE_TIME_VALID_MIN_EPOCH + (time_t)(anim_ms / 1000u));
}

float faculty175_cycle_lunar_phase(uint32_t anim_ms)
{
    const time_t epoch = cycle_epoch(anim_ms);
    const double known_new_moon = 947182440.0; /* 2000-01-06T18:14:00Z */
    return (float)wrap01(((double)epoch - known_new_moon) / LUNAR_SYNODIC_SECONDS);
}

float faculty175_cycle_solar_phase(uint32_t anim_ms)
{
    const time_t epoch = cycle_epoch(anim_ms);
    const double cycle25_minimum = 1577836800.0; /* 2020-01-01, near Solar Cycle 25 minimum */
    return (float)wrap01(((double)epoch - cycle25_minimum) / SOLAR_CYCLE_SECONDS);
}

float faculty175_cycle_solar_year_phase(uint32_t anim_ms)
{
    const time_t epoch = cycle_epoch(anim_ms);
    const double winter_solstice = 1703188800.0; /* 2023-12-21T15:27:00Z */
    return (float)wrap01(((double)epoch - winter_solstice) / SOLAR_YEAR_SECONDS);
}

const char *faculty175_cycle_lunar_label(float phase)
{
    if (phase < 0.0625f || phase >= 0.9375f) {
        return "NEW MOON";
    }
    if (phase < 0.1875f) {
        return "WAXING CRESCENT";
    }
    if (phase < 0.3125f) {
        return "FIRST QUARTER";
    }
    if (phase < 0.4375f) {
        return "WAXING GIBBOUS";
    }
    if (phase < 0.5625f) {
        return "FULL MOON";
    }
    if (phase < 0.6875f) {
        return "WANING GIBBOUS";
    }
    if (phase < 0.8125f) {
        return "LAST QUARTER";
    }
    return "WANING CRESCENT";
}

static void draw_arc_segment(int cx,
                             int cy,
                             int radius,
                             float start_phase,
                             float end_phase,
                             uint16_t color,
                             int width)
{
    if (radius <= 0 || width <= 0) {
        return;
    }
    float span = end_phase - start_phase;
    if (span < 0.0f) {
        span = 0.0f;
    } else if (span > 1.0f) {
        span = 1.0f;
    }

    const int steps = (int)fmaxf(16.0f, (float)radius * TAU_F * span);
    for (int i = 0; i <= steps; ++i) {
        const float p = start_phase + span * ((float)i / (float)steps);
        const float a = p * TAU_F - 1.57079632679f;
        const int x = cx + (int)lrintf(cosf(a) * (float)radius);
        const int y = cy + (int)lrintf(sinf(a) * (float)radius);
        if (width <= 1) {
            faculty175_display_draw_pixel(x, y, color);
        } else {
            faculty175_display_fill_circle(x, y, width / 2, color);
        }
    }
}

static void draw_cycle_ticks(int cx, int cy, int radius, int divisions, uint16_t major, uint16_t minor)
{
    if (divisions <= 0) {
        return;
    }
    for (int i = 0; i < divisions; ++i) {
        const bool cardinal = i == 0 || (divisions == 12 && (i % 3) == 0);
        const float a = ((float)i / (float)divisions) * TAU_F - 1.57079632679f;
        const int inner = cardinal ? radius - 14 : radius - 8;
        const int outer = cardinal ? radius + 2 : radius - 1;
        const int x0 = cx + (int)lrintf(cosf(a) * (float)inner);
        const int y0 = cy + (int)lrintf(sinf(a) * (float)inner);
        const int x1 = cx + (int)lrintf(cosf(a) * (float)outer);
        const int y1 = cy + (int)lrintf(sinf(a) * (float)outer);
        faculty175_display_draw_line(x0, y0, x1, y1, cardinal ? major : minor);
    }
}

static void draw_cycle_arc(int cx,
                           int cy,
                           int radius,
                           float phase,
                           int divisions,
                           uint16_t guide,
                           uint16_t progress,
                           uint16_t marker)
{
    draw_arc_segment(cx, cy, radius, 0.0f, 1.0f, guide, 1);
    draw_arc_segment(cx, cy, radius, 0.0f, fmaxf(0.015f, phase), progress, 3);
    draw_cycle_ticks(cx, cy, radius, divisions, marker, guide);

    const float a = phase * TAU_F - 1.57079632679f;
    const int mx = cx + (int)lrintf(cosf(a) * (float)radius);
    const int my = cy + (int)lrintf(sinf(a) * (float)radius);
    faculty175_display_fill_circle(mx, my, 5, marker);
    faculty175_display_draw_circle(mx, my, 8, guide);
}

void faculty175_cycle_draw_lunar_arc(int cx, int cy, int radius, uint32_t anim_ms)
{
    const float phase = faculty175_cycle_lunar_phase(anim_ms);
    draw_cycle_arc(cx,
                   cy,
                   radius,
                   phase,
                   8,
                   rgb(48, 54, 70),
                   rgb(170, 188, 218),
                   rgb(238, 232, 212));
}

void faculty175_cycle_draw_solar_arc(int cx, int cy, int radius, uint32_t anim_ms)
{
    const float phase = faculty175_cycle_solar_phase(anim_ms);
    draw_cycle_arc(cx,
                   cy,
                   radius,
                   phase,
                   11,
                   rgb(72, 42, 18),
                   rgb(255, 154, 38),
                   rgb(255, 224, 128));
}

void faculty175_cycle_draw_solar_year_arc(int cx, int cy, int radius, uint32_t anim_ms)
{
    const float phase = faculty175_cycle_solar_year_phase(anim_ms);
    draw_cycle_arc(cx,
                   cy,
                   radius,
                   phase,
                   12,
                   rgb(46, 56, 36),
                   rgb(128, 218, 102),
                   rgb(255, 236, 154));

    const float summer = 0.5f * TAU_F - 1.57079632679f;
    const int x0 = cx + (int)lrintf(cosf(summer) * (float)(radius - 14));
    const int y0 = cy + (int)lrintf(sinf(summer) * (float)(radius - 14));
    const int x1 = cx + (int)lrintf(cosf(summer) * (float)(radius + 2));
    const int y1 = cy + (int)lrintf(sinf(summer) * (float)(radius + 2));
    const uint16_t summer_col = rgb(255, 176, 76);
    faculty175_display_draw_line(x0, y0, x1, y1, summer_col);
}
