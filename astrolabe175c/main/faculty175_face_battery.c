#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include "faculty175_board.h"
#include "faculty175_power_history.h"
#include "faculty175_power_metrics.h"

#define TAU_F 6.28318530718f

static uint16_t rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return faculty175_display_rgb888(r, g, b);
}

static float clamp_fraction(float value)
{
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

static void draw_arc(int cx, int cy, int radius, float fraction, int width, uint16_t color)
{
    fraction = clamp_fraction(fraction);
    const int steps = (int)fmaxf(1.0f, (float)radius * TAU_F * fraction / 2.0f);
    for (int i = 0; i <= steps; ++i) {
        const float angle = -1.57079632679f + ((float)i / (float)steps) * fraction * TAU_F;
        const int x = cx + (int)lrintf(cosf(angle) * (float)radius);
        const int y = cy + (int)lrintf(sinf(angle) * (float)radius);
        faculty175_display_fill_circle(x, y, width / 2, color);
    }
}

static void draw_hour_ticks(int cx, int cy, int radius, uint16_t minor, uint16_t major)
{
    for (int hour = 0; hour < 24; ++hour) {
        const float angle = -1.57079632679f + (float)hour * TAU_F / 24.0f;
        const bool cardinal = (hour % 6) == 0;
        const int inner = radius - (cardinal ? 10 : 5);
        const int outer = radius + (cardinal ? 6 : 3);
        faculty175_display_draw_line(cx + (int)lrintf(cosf(angle) * inner),
                                     cy + (int)lrintf(sinf(angle) * inner),
                                     cx + (int)lrintf(cosf(angle) * outer),
                                     cy + (int)lrintf(sinf(angle) * outer),
                                     cardinal ? major : minor);
    }
}

static void draw_battery_icon(int cx, int cy, int percent, uint16_t guide, uint16_t fill)
{
    const int x = cx - 39;
    const int y = cy - 18;
    const int w = 72;
    const int h = 36;
    faculty175_display_draw_line(x, y, x + w, y, guide);
    faculty175_display_draw_line(x, y + h, x + w, y + h, guide);
    faculty175_display_draw_line(x, y, x, y + h, guide);
    faculty175_display_draw_line(x + w, y, x + w, y + h, guide);
    faculty175_display_fill_rect(x + w + 3, y + 10, 7, h - 20, guide);
    const int inner_w = (w - 8) * percent / 100;
    if (inner_w > 0) {
        faculty175_display_fill_rect(x + 4, y + 4, inner_w, h - 8, fill);
    }
}

void faculty175_face_battery_draw(uint32_t anim_ms)
{
    (void)anim_ms;
    const int cx = FACULTY175_LCD_W / 2;
    const int cy = FACULTY175_LCD_H / 2;
    faculty175_power_metrics_t metrics = {};
    faculty175_power_metrics_status(&metrics);
    const faculty175_pmu_status_t *pmu = &metrics.pmu;
    const bool on_battery = pmu->present && pmu->battery_present && !pmu->vbus_in && !pmu->charging;
    const int percent = pmu->battery_percent >= 0 && pmu->battery_percent <= 100
                            ? pmu->battery_percent
                            : 0;
    bool estimate_valid = metrics.estimate_valid;
    float remaining_hours = metrics.remaining_hours;
    float percent_per_hour = metrics.discharge_percent_per_hour;
    faculty175_power_history_estimate_t retained = {};
    if (faculty175_power_history_estimate(pmu, &retained) &&
        (!estimate_valid || retained.elapsed_s * 1000u > metrics.discharge_elapsed_ms)) {
        estimate_valid = true;
        remaining_hours = retained.remaining_hours;
        percent_per_hour = retained.percent_per_hour;
    }

    const uint16_t bg = rgb(5, 9, 13);
    const uint16_t guide = rgb(24, 48, 56);
    const uint16_t dim = rgb(54, 92, 100);
    const uint16_t charge = percent > 50 ? rgb(104, 242, 170)
                              : (percent > 20 ? rgb(255, 205, 92) : rgb(255, 92, 104));
    const uint16_t time_color = rgb(98, 215, 255);
    faculty175_display_fill_rgb565(bg);

    faculty175_display_draw_circle(cx, cy, 220, guide);
    faculty175_display_draw_circle(cx, cy, 208, guide);
    draw_arc(cx, cy, 214, (float)percent / 100.0f, 11, charge);

    static const int eta_radii[] = {174, 156, 138};
    for (size_t i = 0; i < sizeof(eta_radii) / sizeof(eta_radii[0]); ++i) {
        faculty175_display_draw_circle(cx, cy, eta_radii[i], guide);
    }
    draw_hour_ticks(cx, cy, 178, dim, time_color);
    if (estimate_valid && on_battery) {
        for (size_t i = 0; i < sizeof(eta_radii) / sizeof(eta_radii[0]); ++i) {
            draw_arc(cx,
                     cy,
                     eta_radii[i],
                     (remaining_hours - (float)i * 24.0f) / 24.0f,
                     6,
                     i == 0 ? time_color : (i == 1 ? rgb(70, 174, 210) : rgb(54, 128, 160)));
        }
    }

    faculty175_display_fill_circle(cx, cy, 113, bg);
    faculty175_display_draw_circle(cx, cy, 113, dim);
    faculty175_display_draw_circle(cx, cy, 108, guide);
    char line[32];
    draw_battery_icon(cx, cy - 39, percent, dim, charge);
    snprintf(line, sizeof(line), "%d%%", percent);
    faculty175_display_draw_centered_text(line, cy - 8, charge);

    if (!pmu->present) {
        faculty175_display_draw_centered_text("PMU OFFLINE", cy + 21, rgb(255, 92, 104));
    } else if (!on_battery) {
        faculty175_display_draw_centered_text(pmu->charging ? "CHARGING" : "USB POWER", cy + 21, time_color);
        snprintf(line, sizeof(line), "%.3fV", (double)pmu->battery_mv / 1000.0);
        faculty175_display_draw_centered_text(line, cy + 48, dim);
    } else if (estimate_valid) {
        if (remaining_hours >= 48.0f) {
            snprintf(line,
                     sizeof(line),
                     "%uD %uH",
                     (unsigned)(remaining_hours / 24.0f),
                     (unsigned)remaining_hours % 24u);
        } else if (remaining_hours >= 1.0f) {
            snprintf(line, sizeof(line), "%.1fH", (double)remaining_hours);
        } else {
            snprintf(line, sizeof(line), "%uMIN", (unsigned)lrintf(remaining_hours * 60.0f));
        }
        faculty175_display_draw_centered_text(line, cy + 21, time_color);
        snprintf(line, sizeof(line), "%.2f%%/H", (double)percent_per_hour);
        faculty175_display_draw_centered_text(line, cy + 48, dim);
    } else {
        faculty175_display_draw_centered_text("CALCULATING", cy + 21, time_color);
        faculty175_display_draw_centered_text("15MIN + 1% DROP", cy + 48, dim);
    }
    faculty175_display_flush();
}
