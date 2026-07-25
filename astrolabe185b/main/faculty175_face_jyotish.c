#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#ifndef ASTROLABE_FACE_SHARED_466
#include "faculty175_board.h"
#include "faculty175_charts.h"
#endif

static uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) { return faculty175_display_rgb888(r, g, b); }

static int px(int value) { return (int)lrintf((float)value * (float)FACULTY175_LCD_W / 360.0f); }
static int py(int value) { return (int)lrintf((float)value * (float)FACULTY175_LCD_H / 360.0f); }
static int pr(int value) { return (int)lrintf((float)value * (float)FACULTY175_LCD_W / 360.0f); }
static void line(int x0, int y0, int x1, int y1, uint16_t color)
{ faculty175_display_draw_line(px(x0), py(y0), px(x1), py(y1), color); }
static void circle(int x, int y, int radius, uint16_t color)
{ faculty175_display_draw_circle(px(x), py(y), pr(radius), color); }
static void fill_circle(int x, int y, int radius, uint16_t color)
{ faculty175_display_fill_circle(px(x), py(y), pr(radius), color); }
static void text(const char *value, int x, int y, uint16_t color)
{ faculty175_display_draw_text(value, px(x), py(y), color); }

static void polar_line(int cx, int cy, float a, int r0, int r1, uint16_t color)
{
    line(cx + (int)lrintf(cosf(a) * r0), cy + (int)lrintf(sinf(a) * r0),
         cx + (int)lrintf(cosf(a) * r1), cy + (int)lrintf(sinf(a) * r1), color);
}

static void arc(int cx, int cy, int r, float start, float end, uint16_t color)
{
    int last_x = cx + (int)lrintf(cosf(start) * r);
    int last_y = cy + (int)lrintf(sinf(start) * r);
    for (int i = 1; i <= 12; ++i) {
        const float a = start + (end - start) * (float)i / 12.0f;
        const int x = cx + (int)lrintf(cosf(a) * r);
        const int y = cy + (int)lrintf(sinf(a) * r);
        line(last_x, last_y, x, y, color);
        last_x = x;
        last_y = y;
    }
}

static void diamond(int cx, int cy, int r, uint16_t color)
{
    line(cx, cy - r, cx + r, cy, color);
    line(cx + r, cy, cx, cy + r, color);
    line(cx, cy + r, cx - r, cy, color);
    line(cx - r, cy, cx, cy - r, color);
}

static void draw_lotus(int cx, int cy, uint16_t gold, uint16_t rose)
{
    arc(cx, cy + 18, 32, 3.65f, 5.78f, rose);
    arc(cx, cy + 18, 32, 0.50f, 2.63f, rose);
    arc(cx, cy + 12, 22, 3.85f, 5.55f, gold);
    arc(cx, cy + 12, 22, 0.73f, 2.43f, gold);
    line(cx, cy + 4, cx, cy + 33, gold);
}

void faculty175_face_jyotish_draw(uint32_t anim_ms)
{
    const int cx = 180;
    const int cy = 180;
    const uint16_t bg = rgb(8, 7, 22);
    const uint16_t indigo = rgb(35, 32, 74);
    const uint16_t violet = rgb(88, 72, 145);
    const uint16_t gold = rgb(235, 188, 98);
    const uint16_t rose = rgb(213, 112, 137);
    const uint16_t pale = rgb(222, 211, 202);
    const char *const signs[] = {"Me", "Ta", "Mi", "Ka", "Si", "Ka", "Tu", "Vr", "Dh", "Ma", "Ku", "Na"};
    const char *const planets[] = {"Su", "Mo", "Ma", "Me", "Ju", "Ve", "Sa", "Ra", "Ke"};
    const int r = 148;

    faculty175_display_fill_rgb565(bg);
    circle(cx, cy, 174, rgb(25, 21, 50));
    circle(cx, cy, 169, violet);
    circle(cx, cy, 161, indigo);
    circle(cx, cy, r, rgb(139, 91, 145));
    circle(cx, cy, 116, violet);

    for (int i = 0; i < 24; ++i) {
        const float a = -1.5708f + (float)i * 6.2831853f / 24.0f;
        polar_line(cx, cy, a, 151, i % 2 == 0 ? 166 : 160, i % 2 == 0 ? gold : violet);
    }
    for (int i = 0; i < 12; ++i) {
        const float a = -1.5708f + (float)i * 6.2831853f / 12.0f;
        polar_line(cx, cy, a, 106, r, i % 3 == 0 ? gold : rgb(118, 102, 170));
        const float la = a + 0.2618f;
        const int x = cx + (int)lrintf(cosf(la) * 130.0f);
        const int y = cy + (int)lrintf(sinf(la) * 130.0f);
        text(signs[i], x - 6, y - 5, pale);
    }

    diamond(cx, cy, 96, gold);
    diamond(cx, cy, 48, rgb(123, 91, 151));
    line(cx - 96, cy, cx + 96, cy, rgb(92, 71, 128));
    line(cx, cy - 96, cx, cy + 96, rgb(92, 71, 128));
    line(cx - 48, cy - 48, cx + 48, cy + 48, rgb(72, 58, 110));
    line(cx + 48, cy - 48, cx - 48, cy + 48, rgb(72, 58, 110));

    faculty175_charts_ensure_family_seed();
    faculty175_birth_chart_t birth = {};
    faculty175_chart_positions_t positions = {};
    const bool has_birth = faculty175_charts_primary(&birth) && faculty175_charts_birth_positions(&birth, &positions);
    const float drift = (float)(anim_ms % 180000u) / 180000.0f * 6.2831853f;
    for (int i = 0; i < 9; ++i) {
        float lon = has_birth ? (float)positions.lon[i < 7 ? i : i - 2] - 24.0f :
                                (float)i * 41.0f + drift * 12.0f;
        const float a = -1.5708f + lon * 0.0174532925f;
        const int x = cx + (int)lrintf(cosf(a) * 72.0f);
        const int y = cy + (int)lrintf(sinf(a) * 72.0f);
        const uint16_t col = i == 0 ? gold : (i == 1 ? rgb(172, 208, 255) : rose);
        fill_circle(x, y, i == 0 ? 8 : 6, rgb(20, 15, 42));
        circle(x, y, i == 0 ? 9 : 7, col);
        text(planets[i], x - 6, y - 4, col);
    }

    fill_circle(cx, cy, 32, rgb(21, 15, 44));
    circle(cx, cy, 32, gold);
    draw_lotus(cx, cy - 8, gold, rose);
    faculty175_display_draw_bezel_label(has_birth ? "JYOTISH / RASI" : "JYOTISH / SKY", false, pr(166), anim_ms, pale);

    char footer[80];
    if (has_birth) {
        snprintf(footer, sizeof(footer), "%s  %s  %s", birth.name, faculty175_charts_zodiac_abbr(positions.lon[0] - 24.0),
                 faculty175_charts_zodiac_abbr(positions.lon[1] - 24.0));
    } else {
        snprintf(footer, sizeof(footer), "NAVAGRAHA  /  LAGNA  /  DRISHTI");
    }
    faculty175_display_draw_bezel_label(footer, true, pr(166), anim_ms, rgb(190, 175, 211));
    faculty175_display_draw_bezel_label(has_birth ? "NATAL RASI" : "SIDEREAL VIEW", false, pr(154), anim_ms, gold);
    faculty175_display_flush();
}
