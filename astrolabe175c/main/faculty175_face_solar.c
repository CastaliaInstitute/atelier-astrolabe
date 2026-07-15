#include <math.h>
#include "faculty175_board.h"
#include "faculty175_cycle_arcs.h"
#include "faculty175_face_solar_image.h"

static uint16_t c(uint8_t r, uint8_t g, uint8_t b)
{
    return faculty175_display_rgb888(r, g, b);
}

static void draw_fallback_sun(uint32_t anim_ms)
{
    const int cx = FACULTY175_LCD_W / 2;
    const int cy = FACULTY175_LCD_H / 2;
    faculty175_display_fill_rgb565(c(1, 2, 6));

    for (int r = 212; r >= 72; r -= 8) {
        const uint8_t glow = (uint8_t)(28 + (212 - r) / 3);
        faculty175_display_draw_circle(cx, cy, r, c(glow, glow / 2, 5));
    }
    faculty175_display_fill_circle(cx, cy, 156, c(108, 70, 8));
    faculty175_display_fill_circle(cx, cy, 146, c(180, 120, 14));
    faculty175_display_fill_circle(cx - 22, cy - 18, 118, c(214, 150, 22));
    faculty175_display_fill_circle(cx + 42, cy + 28, 74, c(242, 185, 35));

    for (int i = 0; i < 20; ++i) {
        const float a = ((float)i / 20.0f) * 6.2831853f + (float)(anim_ms % 6000u) * 0.00018f;
        const int x0 = cx + (int)lrintf(cosf(a) * 92.0f);
        const int y0 = cy + (int)lrintf(sinf(a) * 92.0f);
        const int x1 = cx + (int)lrintf(cosf(a + 0.14f) * 148.0f);
        const int y1 = cy + (int)lrintf(sinf(a + 0.14f) * 148.0f);
        faculty175_display_draw_line(x0, y0, x1, y1, c(255, 210, 74));
    }
}

static void draw_overlay(uint32_t anim_ms)
{
    faculty175_cycle_draw_solar_arc(FACULTY175_LCD_W / 2, FACULTY175_LCD_H / 2, 214, anim_ms);
    faculty175_cycle_draw_solar_year_arc(FACULTY175_LCD_W / 2, FACULTY175_LCD_H / 2, 190, anim_ms);
}

void faculty175_face_solar_draw(uint32_t anim_ms)
{
    faculty175_solar_image_request(false);
    const bool drew_image = faculty175_solar_image_draw_cached();
    if (!drew_image) {
        draw_fallback_sun(anim_ms);
    }
    draw_overlay(anim_ms);
    faculty175_display_flush();
}
