#include <math.h>
#include <stdint.h>

#include "faculty175_board.h"
#include "faculty175_cycle_arcs.h"

static uint16_t c(uint8_t r, uint8_t g, uint8_t b)
{
    return faculty175_display_rgb888(r, g, b);
}

void faculty175_face_moon_draw(uint32_t anim_ms)
{
    const int cx = FACULTY175_LCD_W / 2;
    const int cy = FACULTY175_LCD_H / 2;
    const float phase = faculty175_cycle_lunar_phase(anim_ms);

    faculty175_display_fill_rgb565(c(2, 3, 8));
    faculty175_display_fill_circle(cx, cy, 130, c(178, 184, 190));

    const int shadow = -78 + (int)lrintf(phase * 156.0f);
    faculty175_display_fill_circle(cx + shadow, cy, 132, c(2, 3, 8));

    for (int i = 0; i < 18; ++i) {
        const float a = ((float)i / 18.0f) * 6.2831853f;
        const int r = 36 + (i * 17) % 78;
        const int x = cx + (int)lrintf(cosf(a) * (float)r);
        const int y = cy + (int)lrintf(sinf(a) * (float)r);
        faculty175_display_fill_circle(x, y, 2 + (i % 3), c(120, 126, 132));
    }

    faculty175_cycle_draw_lunar_arc(cx, cy, 212, anim_ms);
    faculty175_display_flush();
}
