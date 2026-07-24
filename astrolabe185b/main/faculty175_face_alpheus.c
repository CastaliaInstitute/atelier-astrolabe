#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "faculty175_board.h"
#include "faculty175_faces.h"
#include "faculty175_pmu.h"

static uint16_t rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return faculty175_display_rgb888(r, g, b);
}

static void line(int x0, int y0, int x1, int y1, uint16_t color)
{
    faculty175_display_draw_line(x0, y0, x1, y1, color);
}

static void draw_gauge(int cx, int cy, float fraction, uint16_t active, uint16_t dim)
{
    const int segments = 20;
    const float start = -2.55f;
    const float span = 1.95f;
    if (fraction < 0.0f) fraction = 0.0f;
    if (fraction > 1.0f) fraction = 1.0f;
    for (int i = 0; i < segments; ++i) {
        const float a = start + span * ((float)i / (float)(segments - 1));
        const int r0 = 142;
        const int r1 = (i % 4) == 0 ? 158 : 153;
        line(cx + (int)lrintf(cosf(a) * r0), cy + (int)lrintf(sinf(a) * r0),
             cx + (int)lrintf(cosf(a) * r1), cy + (int)lrintf(sinf(a) * r1),
             ((float)i / (float)segments) <= fraction ? active : dim);
    }
}

static void draw_shrimp(int cx, int cy, uint16_t outline, uint16_t shell, uint16_t blue, uint16_t amber)
{
    const int bx = cx - 23;
    const int by = cy + 5;
    faculty175_display_fill_circle(bx, by, 27, blue);
    faculty175_display_draw_circle(bx, by, 32, outline);
    faculty175_display_fill_circle(bx + 25, by - 13, 18, shell);
    faculty175_display_draw_circle(bx + 25, by - 13, 20, outline);
    line(bx - 30, by - 2, bx - 57, by - 16, shell);
    line(bx - 28, by + 12, bx - 54, by + 25, shell);
    line(bx - 10, by + 21, bx - 25, by + 42, amber);
    line(bx + 5, by + 23, bx + 1, by + 46, amber);
    line(bx + 21, by + 14, bx + 30, by + 38, amber);
    line(bx + 21, by - 24, bx + 40, by - 51, amber);
    line(bx + 40, by - 51, bx + 68, by - 61, amber);
    line(bx + 68, by - 61, bx + 80, by - 51, amber);
    line(bx + 68, by - 61, bx + 77, by - 73, amber);
    line(bx + 38, by - 43, bx + 56, by - 34, shell);
    line(bx + 56, by - 34, bx + 77, by - 38, shell);
    line(bx + 36, by - 2, bx + 58, by + 9, shell);
    line(bx + 58, by + 9, bx + 76, by + 5, shell);
    faculty175_display_fill_circle(bx + 35, by - 18, 3, amber);
}

void faculty175_face_alpheus_draw(uint32_t anim_ms)
{
    const int cx = FACULTY175_LCD_W / 2;
    const int cy = FACULTY175_LCD_H / 2;
    faculty175_pmu_status_t power = {.battery_percent = -1};
    (void)faculty175_pmu_status(&power);
    const float battery = power.battery_percent >= 0 && power.battery_percent <= 100
                              ? (float)power.battery_percent / 100.0f : 0.35f;
    const uint16_t turquoise = rgb(0, 210, 226);
    const uint16_t dim = rgb(28, 76, 103);
    const uint16_t amber = rgb(255, 172, 42);
    const float pulse = 0.5f + 0.5f * sinf((float)anim_ms * 0.004f);
    faculty175_display_fill_rgb565(rgb(6, 5, 10));
    faculty175_display_draw_circle(cx, cy, 170, rgb(42, 48, 56));
    faculty175_display_draw_circle(cx, cy, 160 + (int)(pulse * 4.0f), dim);
    draw_gauge(cx, cy, battery, amber, dim);
    faculty175_display_draw_centered_text("ALPHEUS // CORE DIAL", 18, amber);
    draw_shrimp(cx, cy, turquoise, rgb(235, 62, 32), rgb(18, 92, 175), amber);
    char status[24];
    snprintf(status, sizeof(status), "POWER %d%%", power.battery_percent);
    faculty175_display_draw_centered_text(status, FACULTY175_LCD_H - 24, turquoise);
    faculty175_display_flush();
}

bool faculty175_face_alpheus_action(uint32_t now_ms)
{
    (void)now_ms;
    return false;
}
