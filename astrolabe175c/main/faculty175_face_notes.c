#include "faculty175_face_notes.h"

#include <math.h>
#include <stdio.h>

#include "faculty175_board.h"

static uint16_t c(uint8_t r, uint8_t g, uint8_t b)
{
    return faculty175_display_rgb888(r, g, b);
}

static void draw_thick_circle(int cx, int cy, int r, uint16_t color)
{
    faculty175_display_draw_circle(cx, cy, r, color);
    faculty175_display_draw_circle(cx, cy, r - 1, color);
}

static void draw_notebook_icon(int cx, int cy)
{
    const uint16_t paper = c(238, 236, 226);
    const uint16_t edge = c(116, 210, 190);
    const uint16_t gold = c(230, 190, 116);
    const uint16_t rule = c(80, 94, 104);

    faculty175_display_fill_rect(cx - 24, cy - 42, 48, 84, paper);
    faculty175_display_fill_rect(cx - 16, cy - 54, 32, 18, gold);
    faculty175_display_draw_line(cx - 24, cy - 42, cx + 24, cy - 42, edge);
    faculty175_display_draw_line(cx + 24, cy - 42, cx + 24, cy + 42, edge);
    faculty175_display_draw_line(cx + 24, cy + 42, cx - 24, cy + 42, edge);
    faculty175_display_draw_line(cx - 24, cy + 42, cx - 24, cy - 42, edge);
    for (int y = cy - 22; y <= cy + 24; y += 15) {
        faculty175_display_draw_line(cx - 12, y, cx + 16, y, rule);
    }
}

static void draw_orbit_ticks(int cx, int cy, int r, uint32_t anim_ms, uint16_t color)
{
    const float spin = (float)(anim_ms % 6000u) / 6000.0f * 6.2831853f;
    for (int i = 0; i < 24; ++i) {
        const float a = spin + ((float)i / 24.0f) * 6.2831853f;
        const int x0 = cx + (int)lrintf(cosf(a) * (float)(r - 3));
        const int y0 = cy + (int)lrintf(sinf(a) * (float)(r - 3));
        const int x1 = cx + (int)lrintf(cosf(a) * (float)(r + 3));
        const int y1 = cy + (int)lrintf(sinf(a) * (float)(r + 3));
        if ((i % 3) == 0) {
            faculty175_display_draw_line(x0, y0, x1, y1, color);
        }
    }
}

void faculty175_face_notes_draw(uint32_t anim_ms)
{
    const uint16_t bg = c(8, 10, 16);
    const uint16_t panel = c(20, 22, 30);
    const uint16_t ink = c(238, 236, 226);
    const uint16_t dim = c(142, 154, 168);
    const uint16_t accent = c(116, 210, 190);
    const uint16_t orbit = c(42, 56, 62);

    faculty175_display_fill_rgb565(bg);
    faculty175_display_fill_rect(0, 0, FACULTY175_LCD_W, 54, panel);
    faculty175_display_draw_centered_text("NOTES", 14, accent);

    const int cx = FACULTY175_LCD_W / 2;
    const int cy = FACULTY175_LCD_H / 2 - 12;
    draw_thick_circle(cx, cy, 134, orbit);
    faculty175_display_draw_circle(cx, cy, 122, c(30, 42, 50));
    draw_orbit_ticks(cx, cy, 146, anim_ms, c(72, 92, 96));

    faculty175_display_fill_circle(cx, cy, 68, c(18, 28, 34));
    faculty175_display_draw_circle(cx, cy, 68, accent);
    draw_notebook_icon(cx, cy);

    faculty175_display_draw_centered_text("COMMONPLACE NOTES", 326, ink);
    faculty175_display_draw_centered_text("OFFLINE FIRST", 352, dim);
    faculty175_display_draw_centered_text("SYNC VIA CASTALIA", 378, dim);

    faculty175_display_flush();
}
