#include "faculty175_face_crystal_ball.h"

#include <math.h>
#include <stdbool.h>

#include "faculty175_board.h"
#include "faculty175_face_alethiometer.h"
#include "faculty175_face_alethiometer_glyphs.h"

static uint16_t c(uint8_t r, uint8_t g, uint8_t b)
{
    return faculty175_display_rgb888(r, g, b);
}

static uint8_t clamp_u8(int v)
{
    if (v < 0) {
        return 0;
    }
    if (v > 255) {
        return 255;
    }
    return (uint8_t)v;
}

static uint16_t gas_color(float v, float r)
{
    const int blue = 34 + (int)(v * 88.0f) + (int)((1.0f - r) * 34.0f);
    const int green = 22 + (int)(v * 46.0f);
    const int violet = 56 + (int)(v * 74.0f) + (int)((1.0f - r) * 48.0f);
    return c(clamp_u8(violet), clamp_u8(green), clamp_u8(blue));
}

static void draw_swirl(uint32_t anim_ms)
{
    const int cx = FACULTY175_LCD_W / 2;
    const int cy = FACULTY175_LCD_H / 2;
    const float t = (float)anim_ms * 0.001f;

    faculty175_display_fill_rgb565(c(3, 4, 11));
    for (int y = 0; y < FACULTY175_LCD_H; y += 4) {
        for (int x = 0; x < FACULTY175_LCD_W; x += 4) {
            const float dx = (float)(x - cx);
            const float dy = (float)(y - cy);
            const float rr = sqrtf(dx * dx + dy * dy);
            if (rr > 228.0f) {
                continue;
            }
            const float a = atan2f(dy, dx);
            const float rn = rr / 228.0f;
            const float twist = a * 3.0f + rn * 16.0f - t * 1.25f;
            const float wave = sinf(twist) + 0.65f * sinf(a * 7.0f - rn * 10.0f + t * 0.72f);
            const float veil = 0.5f + 0.5f * wave;
            const int block = rr < 206.0f ? 5 : 4;
            faculty175_display_fill_rect(x, y, block, block, gas_color(veil, rn));
        }
    }

    faculty175_display_draw_circle(cx, cy, 211, c(86, 105, 142));
    faculty175_display_draw_circle(cx, cy, 213, c(21, 28, 46));
}

static void draw_warped_glyph(int idx, int slot, bool answer, uint32_t anim_ms)
{
    const int cx = FACULTY175_LCD_W / 2;
    const int cy = FACULTY175_LCD_H / 2;
    const float t = (float)anim_ms * 0.001f;
    const float phase = (float)slot * 1.5707963f + t * (answer ? 0.42f : 0.29f);
    const float wobble = sinf(t * 1.7f + (float)idx * 0.41f);
    const float radius = answer ? 48.0f + wobble * 12.0f : 112.0f + wobble * 18.0f;
    const float warp = sinf(t * 2.3f + (float)slot) * 18.0f;
    const int x = cx + (int)lrintf(cosf(phase + warp * 0.005f) * radius);
    const int y = cy + (int)lrintf(sinf(phase * 1.13f) * (radius * 0.72f));
    const float fade = 0.54f + 0.46f * sinf(t * 1.8f + (float)slot * 1.2f);
    const uint16_t color = answer
                               ? c(112, 210, 246)
                               : c(clamp_u8(156 + (int)(fade * 80.0f)),
                                   clamp_u8(124 + (int)(fade * 68.0f)),
                                   clamp_u8(202 + (int)(fade * 48.0f)));

    const uint8_t scale = answer ? 3 : 2;
    faculty175_face_alethiometer_draw_glyph(x + 2, y + 3, idx, c(15, 13, 28), scale);
    faculty175_face_alethiometer_draw_glyph(x, y, idx, color, scale);
}

void faculty175_face_crystal_ball_cast(uint32_t seed_ms)
{
    faculty175_face_alethiometer_cast(seed_ms);
}

void faculty175_face_crystal_ball_draw(uint32_t anim_ms)
{
    int targets[4] = {};
    (void)faculty175_face_alethiometer_current(targets);

    draw_swirl(anim_ms);
    for (int i = 0; i < 3; ++i) {
        draw_warped_glyph(targets[i], i, false, anim_ms);
    }
    draw_warped_glyph(targets[3], 3, true, anim_ms);

    faculty175_display_draw_centered_text("CRYSTAL BALL", 402, c(174, 204, 230));
    faculty175_display_flush();
}
