#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifndef ASTROLABE_FACE_SHARED_466
#include "faculty175_board.h"
#include "faculty175_charts.h"
#endif
#include "faculty175_bazi_math.h"

bool faculty175_astro_positions_at_epoch(time_t epoch, faculty175_chart_positions_t *out);

static uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) { return faculty175_display_rgb888(r, g, b); }

static int px(int value) { return (int)((float)value * (float)FACULTY175_LCD_W / 360.0f + 0.5f); }
static int py(int value) { return (int)((float)value * (float)FACULTY175_LCD_H / 360.0f + 0.5f); }
static int pr(int value) { return px(value); }
static void line(int x0, int y0, int x1, int y1, uint16_t color)
{ faculty175_display_draw_line(px(x0), py(y0), px(x1), py(y1), color); }
static void circle(int x, int y, int radius, uint16_t color)
{ faculty175_display_draw_circle(px(x), py(y), pr(radius), color); }
static void fill_circle(int x, int y, int radius, uint16_t color)
{ faculty175_display_fill_circle(px(x), py(y), pr(radius), color); }
static void fill_rect(int x, int y, int width, int height, uint16_t color)
{ faculty175_display_fill_rect(px(x), py(y), px(width), py(height), color); }
static void text_at(const char *value, int x, int y, uint16_t color)
{ faculty175_display_draw_text(value, px(x), py(y), color); }
static void centered_text(const char *value, int y, uint16_t color)
{ faculty175_display_draw_centered_text(value, py(y), color); }

static void centered_at(const char *text, int cx, int y, uint16_t color)
{
    const int width = text != NULL ? (int)strlen(text) * 6 : 0;
    text_at(text, cx - width / 2, y, color);
}

static int positive_mod(int value, int modulo)
{
    const int result = value % modulo;
    return result < 0 ? result + modulo : result;
}

static void draw_stamp(int cx, int cy, int r, uint16_t red, uint16_t ink)
{
    circle(cx, cy, r, red);
    circle(cx, cy, r - 6, rgb(45, 13, 20));
    for (int i = 0; i < 8; ++i) {
        const int x = cx - r + 13 + i * ((r * 2 - 26) / 7);
        line(x, cy - r + 8, x + 8, cy + r - 8, red);
    }
    line(cx - r + 8, cy, cx + r - 8, cy, ink);
    line(cx, cy - r + 8, cx, cy + r - 8, ink);
}

static void draw_pillar(int x, int y, const char *stem, const char *branch, const char *element,
                        uint16_t accent, uint16_t pale)
{
    circle(x, y, 38, rgb(31, 15, 24));
    circle(x, y, 34, accent);
    circle(x, y, 29, rgb(18, 10, 18));
    centered_at(stem, x, y - 16, pale);
    line(x - 22, y - 3, x + 22, y - 3, rgb(77, 45, 55));
    centered_at(branch, x, y + 3, accent);
    centered_at(element, x, y + 19, rgb(177, 144, 135));
}

void faculty175_face_bazi_draw(uint32_t anim_ms)
{
    (void)anim_ms;
    const int cx = 180;
    const uint16_t ink = rgb(232, 210, 178);
    const uint16_t red = rgb(176, 47, 47);
    const uint16_t jade = rgb(79, 174, 142);
    const uint16_t water = rgb(87, 145, 205);
    const uint16_t fire = rgb(226, 103, 61);
    const uint16_t earth = rgb(193, 155, 76);
    const uint16_t metal = rgb(194, 204, 193);
    const char *const stems[] = {"JIA", "YI", "BING", "DING", "WU", "JI", "GENG", "XIN", "REN", "GUI"};
    const char *const branches[] = {"RAT", "OX", "TIGER", "RABBIT", "DRAGON", "SNAKE", "HORSE", "GOAT", "MONKEY", "ROOSTER", "DOG", "PIG"};
    const char *const elements[] = {"WOOD", "WOOD", "FIRE", "FIRE", "EARTH", "EARTH", "METAL", "METAL", "WATER", "WATER"};
    const uint16_t element_color[] = {jade, jade, fire, fire, earth, earth, metal, metal, water, water};
    const char *const pillar_names[] = {"YEAR", "MONTH", "DAY", "HOUR"};

    time_t now = time(NULL);
    struct tm today = {};
    localtime_r(&now, &today);
    faculty175_charts_ensure_family_seed();
    faculty175_birth_chart_t birth = {};
    const bool has_birth = faculty175_charts_primary(&birth);
    if (has_birth) {
        today.tm_year = (int)birth.year - 1900;
        today.tm_mon = (int)birth.month - 1;
        today.tm_mday = birth.day;
        today.tm_hour = birth.hour;
    }

    faculty175_chart_positions_t positions = {};
    double sun_longitude = 0.0;
    if (has_birth && faculty175_charts_birth_positions(&birth, &positions)) {
        sun_longitude = positions.lon[0];
    } else {
        faculty175_chart_positions_t now_positions = {};
        if (faculty175_astro_positions_at_epoch(time(NULL), &now_positions)) {
            sun_longitude = now_positions.lon[0];
        }
    }
    faculty175_bazi_chart_t chart = {};
    faculty175_bazi_calculate(today.tm_year + 1900, today.tm_mon + 1, today.tm_mday,
                              today.tm_hour, today.tm_min, sun_longitude, &chart);
    const int pillar_stems[] = {chart.stem[0], chart.stem[1], chart.stem[2], chart.stem[3]};
    const int pillar_branches[] = {chart.branch[0], chart.branch[1], chart.branch[2], chart.branch[3]};

    faculty175_display_fill_rgb565(rgb(12, 8, 14));
    fill_rect(0, 0, 360, 360, rgb(12, 8, 14));
    for (int i = 0; i < 6; ++i) {
        circle(cx, 180, 54 + i * 22, i % 2 == 0 ? rgb(36, 20, 30) : rgb(52, 24, 31));
    }
    centered_text("BA ZI", 42, ink);
    centered_text("FOUR PILLARS", 61, rgb(190, 140, 125));

    static const int pillar_x[] = {180, 274, 180, 86};
    static const int pillar_y[] = {104, 180, 256, 180};
    static const int pillar_label_x[] = {180, 313, 180, 47};
    static const int pillar_label_y[] = {75, 174, 276, 174};
    for (int i = 0; i < 4; ++i) {
        const int x = pillar_x[i];
        const int y = pillar_y[i];
        const int stem = pillar_stems[i];
        const int branch = pillar_branches[i];
        line(cx, 180, x, y, rgb(81, 35, 49));
        draw_pillar(x, y, stems[stem], branches[branch], elements[stem], element_color[stem], ink);
        centered_at(pillar_names[i], pillar_label_x[i], pillar_label_y[i], rgb(152, 113, 113));
    }

    int balance[5] = {0, 0, 0, 0, 0};
    for (int i = 0; i < 4; ++i) {
        const int element = pillar_stems[i] / 2;
        balance[element] += 2;
        balance[positive_mod(pillar_branches[i], 5)] += 1;
    }
    const uint16_t bars[] = {jade, fire, earth, metal, water};
    for (int i = 0; i < 5; ++i) {
        const float angle = -1.5708f + (float)i * 1.2566f;
        const int x = cx + (int)(cosf(angle) * 62.0f);
        const int y = 180 + (int)(sinf(angle) * 62.0f);
        fill_circle(x, y, 4 + balance[i], bars[i]);
    }
    draw_stamp(cx, 180, 37, red, rgb(106, 32, 40));
    centered_text("DAY", 172, ink);
    centered_text("MASTER", 188, rgb(190, 140, 125));
    faculty175_display_draw_bezel_label(has_birth ? "BAZI / NATAL SEAL" : "BAZI / DAY PILLARS", false, pr(166), anim_ms, ink);
    char footer[72];
    snprintf(footer, sizeof(footer), "%s  %s  %s", stems[chart.stem[2]], branches[chart.branch[2]], elements[chart.stem[2]]);
    faculty175_display_draw_bezel_label(footer, true, pr(166), anim_ms, red);
    faculty175_display_flush();
}
