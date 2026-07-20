#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "faculty175_board.h"
#include "faculty175_family.h"

#define TAU_F 6.28318530718f

static uint16_t rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return faculty175_display_rgb888(r, g, b);
}

static int clamp_i(int v, int lo, int hi)
{
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

static void centered(const char *text, int y, uint16_t color)
{
    faculty175_display_draw_centered_text(text, y, color);
}

static uint16_t load_color(uint8_t score)
{
    if (score >= 76) {
        return rgb(255, 116, 106);
    }
    if (score >= 58) {
        return rgb(255, 196, 104);
    }
    return rgb(116, 224, 152);
}

static void draw_arc_fraction(int cx, int cy, int radius, float fraction, uint16_t color, int width)
{
    if (fraction < 0.0f) {
        fraction = 0.0f;
    } else if (fraction > 1.0f) {
        fraction = 1.0f;
    }
    const int steps = (int)fmaxf(12.0f, (float)radius * TAU_F * fraction);
    for (int i = 0; i <= steps; ++i) {
        const float p = (float)i / (float)steps * fraction;
        const float a = p * TAU_F - 1.57079632679f;
        const int x = cx + (int)lrintf(cosf(a) * (float)radius);
        const int y = cy + (int)lrintf(sinf(a) * (float)radius);
        faculty175_display_fill_circle(x, y, width / 2, color);
    }
}

static void draw_metric_arc(int cx, int cy, int radius, uint8_t value, uint16_t guide, uint16_t color)
{
    faculty175_display_draw_circle(cx, cy, radius, guide);
    draw_arc_fraction(cx, cy, radius, (float)value / 100.0f, color, 4);
}

static void draw_sample_ticks(int cx, int cy, int radius, const faculty175_family_wellness_t *partner)
{
    const uint16_t dim = rgb(50, 58, 74);
    const uint16_t high = rgb(255, 116, 106);
    const uint16_t low_hrv = rgb(142, 184, 255);
    const int total = partner->day_sample_count > 0 ? partner->day_sample_count : 24;
    const int ticks = total > 48 ? 48 : total;
    const int high_ticks = partner->day_sample_count > 0
                               ? (int)((uint32_t)partner->day_high_stress_samples * (uint32_t)ticks / partner->day_sample_count)
                               : 0;
    const int low_ticks = partner->day_sample_count > 0
                              ? (int)((uint32_t)partner->day_low_hrv_samples * (uint32_t)ticks / partner->day_sample_count)
                              : 0;
    for (int i = 0; i < ticks; ++i) {
        const float a = ((float)i / (float)ticks) * TAU_F - 1.57079632679f;
        const int x = cx + (int)lrintf(cosf(a) * (float)radius);
        const int y = cy + (int)lrintf(sinf(a) * (float)radius);
        const uint16_t col = i < high_ticks ? high : (i >= ticks - low_ticks ? low_hrv : dim);
        faculty175_display_fill_circle(x, y, i < high_ticks ? 2 : 1, col);
    }
}

void faculty175_face_partner_wellness_draw(uint32_t anim_ms)
{
    (void)anim_ms;
    const int cx = FACULTY175_LCD_W / 2;
    const int cy = FACULTY175_LCD_H / 2 + 2;
    faculty175_family_wellness_t partner = {};

    faculty175_display_fill_rgb565(rgb(4, 7, 12));
    faculty175_display_draw_circle(cx, cy, 222, rgb(22, 28, 42));
    faculty175_display_draw_circle(cx, cy, 204, rgb(18, 40, 54));

    if (!faculty175_family_primary_partner(&partner)) {
        faculty175_display_draw_circle(cx, cy, 156, rgb(42, 48, 64));
        centered("PARTNER RING", 166, rgb(128, 140, 164));
        centered("waiting for paired packets", 196, rgb(168, 178, 198));
        centered("ESP-NOW family mesh", 224, rgb(96, 108, 132));
        faculty175_display_flush();
        return;
    }

    const uint8_t now_load = faculty175_family_load_score(&partner);
    const uint8_t day_load = partner.day_sample_count > 0 ? partner.day_load_avg : now_load;
    const uint16_t accent = load_color(now_load);
    const uint16_t day_col = load_color(day_load);
    const uint16_t guide = rgb(36, 44, 62);

    draw_metric_arc(cx, cy, 198, day_load, guide, day_col);
    draw_metric_arc(cx, cy, 170, now_load, rgb(30, 38, 52), accent);
    const uint8_t hrv_inverse = partner.hrv_ms >= 80 ? 0 : (uint8_t)clamp_i(100 - (int)partner.hrv_ms, 0, 100);
    draw_metric_arc(cx, cy, 142, hrv_inverse, rgb(28, 36, 48), rgb(128, 176, 255));
    const uint16_t debt = faculty175_family_sleep_debt_min(&partner);
    const uint8_t debt_score = debt >= 180 ? 100 : (uint8_t)((uint32_t)debt * 100u / 180u);
    draw_metric_arc(cx, cy, 116, debt_score, rgb(30, 36, 44), rgb(198, 150, 255));
    draw_sample_ticks(cx, cy, 214, &partner);

    char line[96];
    centered(partner.subject_name, 128, rgb(218, 226, 240));
    centered(faculty175_family_guidance_cue(&partner), 154, accent);

    snprintf(line, sizeof(line), "DAY %u AVG  %u PEAK", partner.day_stress_avg, partner.day_stress_peak);
    centered(line, 194, day_col);
    snprintf(line, sizeof(line), "HIGH %u/%u  LOW HRV %u",
             partner.day_high_stress_samples,
             partner.day_sample_count,
             partner.day_low_hrv_samples);
    centered(line, 222, rgb(188, 202, 224));
    snprintf(line, sizeof(line), "NOW %u%+d  HRV %u  SPO2 %u",
             partner.stress,
             partner.stress_trend_30m,
             partner.hrv_ms,
             partner.spo2_percent);
    centered(line, 252, rgb(208, 216, 232));
    snprintf(line, sizeof(line), "SLEEP %uh%02u  DEBT %u  AGE %lus",
             partner.sleep_total_min / 60,
             partner.sleep_total_min % 60,
             debt,
             (unsigned long)(partner.age_ms / 1000u));
    centered(line, 280, rgb(158, 172, 198));

    faculty175_display_flush();
}
