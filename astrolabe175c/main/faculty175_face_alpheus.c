#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "faculty175_board.h"
#include "faculty175_faces.h"
#include "faculty175_power_metrics.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static uint16_t alpheus_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return faculty175_display_rgb888(r, g, b);
}

static void alpheus_line(int x0, int y0, int x1, int y1, uint16_t color)
{
    faculty175_display_draw_line(x0, y0, x1, y1, color);
}

static void draw_power_gauge(int cx, int cy, float fraction, uint16_t active, uint16_t dim)
{
    const int segments = 24;
    const float start = -2.55f;
    const float span = 1.95f;
    if (fraction < 0.0f) fraction = 0.0f;
    if (fraction > 1.0f) fraction = 1.0f;
    for (int i = 0; i < segments; ++i) {
        const float angle = start + span * ((float)i / (float)(segments - 1));
        const int r0 = 178;
        const int r1 = (i % 4) == 0 ? 194 : 188;
        const int x0 = cx + (int)lrintf(cosf(angle) * (float)r0);
        const int y0 = cy + (int)lrintf(sinf(angle) * (float)r0);
        const int x1 = cx + (int)lrintf(cosf(angle) * (float)r1);
        const int y1 = cy + (int)lrintf(sinf(angle) * (float)r1);
        alpheus_line(x0, y0, x1, y1,
                     ((float)i / (float)segments) <= fraction ? active : dim);
    }
}

static void draw_shrimp(int cx, int cy, uint16_t outline, uint16_t shell, uint16_t blue, uint16_t amber)
{
    const int bx = cx - 30;
    const int by = cy + 4;
    faculty175_display_fill_circle(bx, by, 31, blue);
    faculty175_display_draw_circle(bx, by, 37, outline);
    faculty175_display_fill_circle(bx + 28, by - 14, 20, shell);
    faculty175_display_draw_circle(bx + 28, by - 14, 22, outline);

    /* Right-facing body, antennae, legs, and the larger claw above. */
    alpheus_line(bx - 32, by - 2, bx - 64, by - 18, shell);
    alpheus_line(bx - 30, by + 12, bx - 61, by + 28, shell);
    alpheus_line(bx - 16, by + 23, bx - 34, by + 47, amber);
    alpheus_line(bx + 2, by + 26, bx - 3, by + 51, amber);
    alpheus_line(bx + 18, by + 16, bx + 27, by + 42, amber);
    alpheus_line(bx + 22, by - 27, bx + 43, by - 59, amber);
    alpheus_line(bx + 43, by - 59, bx + 78, by - 72, amber);
    alpheus_line(bx + 78, by - 72, bx + 92, by - 61, amber);
    alpheus_line(bx + 78, by - 72, bx + 88, by - 86, amber);
    alpheus_line(bx + 39, by - 51, bx + 62, by - 38, shell);
    alpheus_line(bx + 62, by - 38, bx + 86, by - 42, shell);
    alpheus_line(bx + 38, by - 3, bx + 64, by + 10, shell);
    alpheus_line(bx + 64, by + 10, bx + 85, by + 5, shell);
    faculty175_display_fill_circle(bx + 40, by - 20, 3, amber);
}

void faculty175_face_alpheus_draw(uint32_t anim_ms)
{
    const int cx = FACULTY175_LCD_W / 2;
    const int cy = FACULTY175_LCD_H / 2;
    const float pulse = 0.5f + 0.5f * sinf((float)anim_ms * 0.004f);
    faculty175_power_metrics_t power = {};
    faculty175_power_metrics_status(&power);
    const float battery = power.pmu.battery_percent >= 0 && power.pmu.battery_percent <= 100
                              ? (float)power.pmu.battery_percent / 100.0f
                              : 0.35f;
    const uint16_t bg = alpheus_rgb(6, 5, 10);
    const uint16_t turquoise = alpheus_rgb(0, 210, 226);
    const uint16_t dim = alpheus_rgb(28, 76, 103);
    const uint16_t amber = alpheus_rgb(255, 172, 42);

    faculty175_display_fill_rgb565(bg);
    faculty175_display_draw_circle(cx, cy, 226, alpheus_rgb(42, 48, 56));
    faculty175_display_draw_circle(cx, cy, 214 + (int)(pulse * 5.0f), dim);
    faculty175_display_draw_circle(cx, cy, 196, alpheus_rgb(44, 30, 28));
    draw_power_gauge(cx, cy, battery, amber, dim);
    faculty175_display_draw_centered_text("ALPHEUS // CORE DIAL", 22, amber);
    faculty175_display_draw_centered_text("POWER", 56, dim);
    draw_shrimp(cx, cy, turquoise, alpheus_rgb(235, 62, 32), alpheus_rgb(18, 92, 175), amber);

    char status[24];
    snprintf(status, sizeof(status), "POWER %d%%", power.pmu.battery_percent);
    faculty175_display_draw_centered_text(status, FACULTY175_LCD_H - 34, turquoise);
    faculty175_display_flush();
}

bool faculty175_face_alpheus_action(uint32_t now_ms)
{
    (void)now_ms;
    return false;
}
