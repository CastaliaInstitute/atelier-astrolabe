#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#include "faculty175_board.h"

static uint16_t rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return faculty175_display_rgb888(r, g, b);
}

static void draw_session(uint32_t anim_ms, bool journal)
{
    const int cx = FACULTY175_LCD_W / 2;
    const int cy = FACULTY175_LCD_H / 2 - 8;
    const uint16_t bg = journal ? rgb(9, 12, 22) : rgb(7, 15, 20);
    const uint16_t accent = journal ? rgb(214, 178, 255) : rgb(93, 226, 205);
    const uint16_t ink = rgb(238, 242, 244);
    const uint16_t dim = rgb(118, 136, 148);
    const float phase = (float)(anim_ms % 2200u) / 2200.0f * 6.2831853f;

    faculty175_display_fill_rgb565(bg);
    faculty175_display_draw_centered_text(journal ? "JOURNAL" : "CONVERSATION", 26, accent);
    for (int ring = 0; ring < 4; ++ring) {
        const int pulse = (int)lrintf((sinf(phase + ring * 0.7f) + 1.0f) * 4.0f);
        faculty175_display_draw_circle(cx, cy, 62 + ring * 21 + pulse, ring == 0 ? accent : dim);
    }
    for (int i = -5; i <= 5; ++i) {
        const float wave = sinf(phase * 2.0f + (float)i * 0.72f);
        const int height = 12 + (int)lrintf(fabsf(wave) * (journal ? 48.0f : 62.0f));
        const int x = cx + i * 16;
        faculty175_display_draw_line(x, cy - height, x, cy + height, accent);
    }
    faculty175_display_fill_circle(cx, cy, 8, rgb(255, 94, 104));
    faculty175_display_draw_centered_text("LISTENING · SESSION ACTIVE", 342, ink);
    faculty175_display_draw_centered_text(journal ? "TRANSCRIBE · QUEUE · SYNC" : "LISTEN · THINK · SPEAK", 370, dim);
    faculty175_display_draw_centered_text("SWIPE AWAY TO END", 398, dim);
    faculty175_display_flush();
}

void faculty175_face_journal_draw(uint32_t anim_ms)
{
    draw_session(anim_ms, true);
}

void faculty175_face_conversation_draw(uint32_t anim_ms)
{
    draw_session(anim_ms, false);
}
