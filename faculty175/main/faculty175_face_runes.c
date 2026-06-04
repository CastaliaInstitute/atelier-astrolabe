#include "faculty175_face_runes.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_random.h"
#include "nvs.h"

#include "faculty175_board.h"

#define RUNES_NVS_NS "runes"
#define RUNE_COUNT 24
#define SPREAD_COUNT 3

typedef struct {
    const char *name;
    const char *sound;
    const char *keyword;
} rune_t;

static const rune_t k_runes[RUNE_COUNT] = {
    {"FEHU", "F", "RESOURCE"},       {"URUZ", "U", "STRENGTH"},
    {"THURISAZ", "TH", "THRESHOLD"}, {"ANSUZ", "A", "MESSAGE"},
    {"RAIDHO", "R", "JOURNEY"},      {"KENAZ", "K", "TORCH"},
    {"GEBO", "G", "GIFT"},           {"WUNJO", "W", "JOY"},
    {"HAGALAZ", "H", "STORM"},       {"NAUTHIZ", "N", "NEED"},
    {"ISA", "I", "STILLNESS"},       {"JERA", "J", "HARVEST"},
    {"EIHWAZ", "EI", "ENDURANCE"},   {"PERTHRO", "P", "CHANCE"},
    {"ALGIZ", "Z", "PROTECTION"},    {"SOWILO", "S", "SUN"},
    {"TIWAZ", "T", "JUSTICE"},       {"BERKANO", "B", "GROWTH"},
    {"EHWAZ", "E", "TRUST"},         {"MANNAZ", "M", "SELF"},
    {"LAGUZ", "L", "FLOW"},          {"INGWAZ", "NG", "SEED"},
    {"DAGAZ", "D", "DAYBREAK"},      {"OTHALA", "O", "INHERIT"},
};

static const char *const k_slots[SPREAD_COUNT] = {"PAST", "NOW", "NEXT"};
static int s_spread[SPREAD_COUNT] = {0, 10, 22};
static bool s_loaded;

static uint16_t c(uint8_t r, uint8_t g, uint8_t b)
{
    return faculty175_display_rgb888(r, g, b);
}

static void draw_thick_line(int x0, int y0, int x1, int y1, uint16_t color, int w)
{
    for (int d = -w; d <= w; ++d) {
        faculty175_display_draw_line(x0 + d, y0, x1 + d, y1, color);
        faculty175_display_draw_line(x0, y0 + d, x1, y1 + d, color);
    }
}

static void draw_centered_at(const char *text, int cx, int y, uint16_t color)
{
    if (text == NULL) {
        return;
    }
    const int w = (int)strlen(text) * 6;
    faculty175_display_draw_text(text, cx - w / 2, y, color);
}

static void draw_rune_glyph(int idx, int cx, int cy, uint16_t color)
{
    const int y0 = cy - 34;
    const int y1 = cy + 34;
    draw_thick_line(cx, y0, cx, y1, color, 1);
    switch (idx % 12) {
        case 0:
            draw_thick_line(cx, y0 + 6, cx + 28, cy - 12, color, 1);
            draw_thick_line(cx, cy - 2, cx + 24, cy + 14, color, 1);
            break;
        case 1:
            draw_thick_line(cx, y0, cx - 26, cy, color, 1);
            draw_thick_line(cx - 26, cy, cx, y1, color, 1);
            draw_thick_line(cx, y0, cx + 26, cy, color, 1);
            draw_thick_line(cx + 26, cy, cx, y1, color, 1);
            break;
        case 2:
            draw_thick_line(cx - 24, y0, cx + 24, y1, color, 1);
            draw_thick_line(cx + 24, y0, cx - 24, y1, color, 1);
            break;
        case 3:
            draw_thick_line(cx, y0 + 8, cx + 28, cy - 10, color, 1);
            draw_thick_line(cx, cy - 1, cx + 28, cy + 18, color, 1);
            break;
        case 4:
            draw_thick_line(cx - 26, y0 + 8, cx + 24, y1 - 8, color, 1);
            draw_thick_line(cx - 24, cy + 8, cx + 26, cy - 8, color, 1);
            break;
        case 5:
            draw_thick_line(cx - 24, cy + 6, cx + 24, cy - 24, color, 1);
            draw_thick_line(cx - 24, cy + 24, cx + 24, cy - 6, color, 1);
            break;
        case 6:
            draw_thick_line(cx - 28, y0 + 6, cx + 28, y1 - 6, color, 1);
            draw_thick_line(cx + 28, y0 + 6, cx - 28, y1 - 6, color, 1);
            break;
        case 7:
            draw_thick_line(cx, cy, cx - 28, y0 + 6, color, 1);
            draw_thick_line(cx, cy, cx + 28, y0 + 6, color, 1);
            break;
        case 8:
            draw_thick_line(cx - 24, y0 + 10, cx + 24, y1 - 10, color, 1);
            break;
        case 9:
            draw_thick_line(cx, cy - 6, cx + 26, y0 + 12, color, 1);
            draw_thick_line(cx, cy + 8, cx - 26, y1 - 12, color, 1);
            break;
        case 10:
            draw_thick_line(cx - 30, cy, cx + 30, cy, color, 1);
            break;
        default:
            draw_thick_line(cx, cy - 4, cx + 26, y0 + 12, color, 1);
            draw_thick_line(cx, cy + 4, cx - 26, y1 - 12, color, 1);
            break;
    }
}

static void spread_save(void)
{
    nvs_handle_t nvs;
    if (nvs_open(RUNES_NVS_NS, NVS_READWRITE, &nvs) != ESP_OK) {
        return;
    }
    for (int i = 0; i < SPREAD_COUNT; ++i) {
        char key[4];
        snprintf(key, sizeof(key), "r%d", i);
        (void)nvs_set_i32(nvs, key, s_spread[i]);
    }
    (void)nvs_commit(nvs);
    nvs_close(nvs);
}

static void spread_load(void)
{
    if (s_loaded) {
        return;
    }
    s_loaded = true;
    nvs_handle_t nvs;
    if (nvs_open(RUNES_NVS_NS, NVS_READONLY, &nvs) != ESP_OK) {
        faculty175_face_runes_cast();
        return;
    }
    bool ok = true;
    for (int i = 0; i < SPREAD_COUNT; ++i) {
        char key[4];
        int32_t v = 0;
        snprintf(key, sizeof(key), "r%d", i);
        if (nvs_get_i32(nvs, key, &v) != ESP_OK || v < 0 || v >= RUNE_COUNT) {
            ok = false;
            break;
        }
        s_spread[i] = (int)v;
    }
    nvs_close(nvs);
    if (!ok) {
        faculty175_face_runes_cast();
    }
}

void faculty175_face_runes_cast(void)
{
    bool used[RUNE_COUNT] = {};
    for (int i = 0; i < SPREAD_COUNT; ++i) {
        int idx = (int)(esp_random() % RUNE_COUNT);
        while (used[idx]) {
            idx = (idx + 1) % RUNE_COUNT;
        }
        used[idx] = true;
        s_spread[i] = idx;
    }
    s_loaded = true;
    spread_save();
}

static void draw_token(int x, int y, int rune_idx, const char *slot, uint16_t accent, uint16_t dim)
{
    faculty175_display_fill_circle(x, y, 58, c(38, 27, 20));
    faculty175_display_draw_circle(x, y, 58, c(154, 105, 58));
    faculty175_display_draw_circle(x, y, 50, c(72, 48, 28));
    draw_rune_glyph(rune_idx, x, y - 4, accent);
    draw_centered_at(slot, x, y + 70, dim);
}

void faculty175_face_runes_draw(uint32_t anim_ms)
{
    (void)anim_ms;
    spread_load();
    const uint16_t bg = c(6, 8, 14);
    const uint16_t panel = c(18, 16, 24);
    const uint16_t ink = c(235, 230, 212);
    const uint16_t dim = c(144, 132, 118);
    const uint16_t accent = c(238, 188, 96);
    faculty175_display_fill_rgb565(bg);
    faculty175_display_fill_rect(0, 0, FACULTY175_LCD_W, 54, panel);
    faculty175_display_draw_centered_text("RUNES", 14, accent);

    const int xs[SPREAD_COUNT] = {104, 233, 362};
    const int ys[SPREAD_COUNT] = {224, 204, 224};
    for (int i = 0; i < SPREAD_COUNT; ++i) {
        draw_token(xs[i], ys[i], s_spread[i], k_slots[i], accent, dim);
    }

    char line[96];
    snprintf(line, sizeof(line), "%s  %s  %s", k_runes[s_spread[0]].name, k_runes[s_spread[1]].name,
             k_runes[s_spread[2]].name);
    faculty175_display_draw_centered_text(line, 340, ink);
    snprintf(line, sizeof(line), "%s / %s / %s", k_runes[s_spread[0]].keyword, k_runes[s_spread[1]].keyword,
             k_runes[s_spread[2]].keyword);
    faculty175_display_draw_centered_text(line, 366, dim);
    faculty175_display_draw_centered_text("BUTTON CASTS AGAIN", 402, c(116, 210, 190));
    faculty175_display_flush();
}
