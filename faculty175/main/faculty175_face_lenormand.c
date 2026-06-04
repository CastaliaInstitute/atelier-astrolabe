#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "astrolabe_time.h"
#include "faculty175_board.h"
#include "faculty175_face_alethiometer_glyphs.h"

#define LENORMAND_CARD_COUNT 36

typedef struct {
    const char *title;
    const char *keyword;
    uint8_t glyph;
    uint8_t r;
    uint8_t g;
    uint8_t b;
} lenormand_card_t;

static const lenormand_card_t k_cards[LENORMAND_CARD_COUNT] = {
    {"RIDER", "message", 34, 230, 176, 96},      {"CLOVER", "chance", 23, 116, 214, 134},
    {"SHIP", "passage", 16, 110, 190, 220},      {"HOUSE", "home", 29, 230, 190, 118},
    {"TREE", "roots", 10, 106, 194, 120},        {"CLOUDS", "unclear", 19, 160, 178, 190},
    {"SNAKE", "turning", 11, 176, 210, 96},      {"COFFIN", "ending", 4, 160, 142, 118},
    {"BOUQUET", "gift", 28, 230, 150, 188},      {"SCYTHE", "cut", 9, 230, 112, 92},
    {"WHIP", "friction", 17, 208, 126, 96},      {"BIRDS", "talk", 1, 238, 190, 86},
    {"CHILD", "new", 32, 156, 210, 238},         {"FOX", "strategy", 15, 224, 132, 80},
    {"BEAR", "power", 8, 218, 176, 90},          {"STARS", "guidance", 30, 172, 196, 255},
    {"STORK", "change", 34, 210, 220, 238},      {"DOG", "loyalty", 7, 238, 124, 146},
    {"TOWER", "structure", 13, 186, 160, 220},   {"GARDEN", "public", 12, 112, 206, 160},
    {"MOUNTAIN", "block", 20, 156, 176, 184},    {"CROSSROADS", "choice", 21, 214, 180, 96},
    {"MICE", "loss", 25, 168, 150, 132},         {"HEART", "love", 7, 238, 104, 132},
    {"RING", "bond", 31, 232, 198, 88},          {"BOOK", "hidden", 14, 142, 184, 228},
    {"LETTER", "news", 5, 222, 206, 150},        {"MAN", "querent", 18, 158, 206, 238},
    {"WOMAN", "querent", 18, 238, 158, 210},     {"LILY", "peace", 2, 232, 220, 152},
    {"SUN", "success", 2, 248, 206, 84},         {"MOON", "recognition", 3, 176, 178, 238},
    {"KEY", "answer", 5, 232, 196, 86},          {"FISH", "flow", 22, 102, 194, 220},
    {"ANCHOR", "stability", 6, 118, 184, 214},   {"CROSS", "burden", 26, 206, 176, 122},
};

static uint16_t c(uint8_t r, uint8_t g, uint8_t b)
{
    return faculty175_display_rgb888(r, g, b);
}

static uint16_t blend(uint16_t bg, uint16_t fg, uint8_t alpha)
{
    if (alpha == 0) {
        return bg;
    }
    if (alpha >= 255) {
        return fg;
    }
    const uint8_t br = (uint8_t)(((bg >> 11) & 0x1f) * 255 / 31);
    const uint8_t bg_g = (uint8_t)(((bg >> 5) & 0x3f) * 255 / 63);
    const uint8_t bb = (uint8_t)((bg & 0x1f) * 255 / 31);
    const uint8_t fr = (uint8_t)(((fg >> 11) & 0x1f) * 255 / 31);
    const uint8_t fg_g = (uint8_t)(((fg >> 5) & 0x3f) * 255 / 63);
    const uint8_t fb = (uint8_t)((fg & 0x1f) * 255 / 31);
    const uint16_t ia = (uint16_t)(255u - alpha);
    return c((uint8_t)((br * ia + fr * alpha) / 255u),
             (uint8_t)((bg_g * ia + fg_g * alpha) / 255u),
             (uint8_t)((bb * ia + fb * alpha) / 255u));
}

static void centered_at(const char *text, int cx, int y, uint16_t color)
{
    if (text == NULL || text[0] == '\0') {
        return;
    }
    const int w = (int)strlen(text) * 6;
    faculty175_display_draw_text(text, cx - w / 2, y, color);
}

static void rect_outline(int x, int y, int w, int h, uint16_t color)
{
    faculty175_display_draw_line(x, y, x + w, y, color);
    faculty175_display_draw_line(x + w, y, x + w, y + h, color);
    faculty175_display_draw_line(x + w, y + h, x, y + h, color);
    faculty175_display_draw_line(x, y + h, x, y, color);
}

static int daily_index(uint32_t anim_ms)
{
    if (!astrolabe_time_valid()) {
        return (int)((anim_ms / 60000u) % LENORMAND_CARD_COUNT);
    }
    struct tm local = {};
    astrolabe_time_local(&local);
    const int yday = local.tm_yday < 0 ? 0 : local.tm_yday;
    const int year = local.tm_year + 1900;
    return (yday + year * 11) % LENORMAND_CARD_COUNT;
}

static void draw_ring(int active, uint16_t accent)
{
    const int cx = FACULTY175_LCD_W / 2;
    const int cy = FACULTY175_LCD_H / 2;
    const uint16_t dim = c(58, 48, 68);
    for (int i = 0; i < LENORMAND_CARD_COUNT; ++i) {
        const float a = -1.5707963f + (float)i * 6.2831853f / (float)LENORMAND_CARD_COUNT;
        const int x0 = cx + (int)lrintf(cosf(a) * 205.0f);
        const int y0 = cy + (int)lrintf(sinf(a) * 205.0f);
        const int x1 = cx + (int)lrintf(cosf(a) * 219.0f);
        const int y1 = cy + (int)lrintf(sinf(a) * 219.0f);
        faculty175_display_draw_line(x0, y0, x1, y1, i == active ? accent : dim);
    }
    faculty175_display_draw_circle(cx, cy, 222, blend(c(10, 9, 15), accent, 102));
    faculty175_display_draw_circle(cx, cy, 204, dim);
}

static void draw_card(int cx, int cy, const lenormand_card_t *card, int idx)
{
    const uint16_t outer = c(18, 14, 20);
    const uint16_t paper = c(246, 235, 210);
    const uint16_t ink = c(36, 28, 32);
    const uint16_t accent = c(card->r, card->g, card->b);
    faculty175_display_fill_rect(cx - 80, cy - 112, 160, 224, outer);
    rect_outline(cx - 80, cy - 112, 160, 224, accent);
    rect_outline(cx - 74, cy - 106, 148, 212, paper);
    faculty175_display_fill_rect(cx - 68, cy - 100, 136, 200, paper);

    char num[8];
    snprintf(num, sizeof(num), "%02d", idx + 1);
    centered_at(num, cx, cy - 90, accent);
    faculty175_face_alethiometer_draw_glyph(cx, cy - 15, card->glyph, ink, 3);
    centered_at(card->title, cx, cy + 62, ink);
    centered_at(card->keyword, cx, cy + 88, blend(paper, accent, 178));
}

void faculty175_face_lenormand_draw(uint32_t anim_ms)
{
    const int idx = daily_index(anim_ms);
    const lenormand_card_t *card = &k_cards[idx];
    const uint16_t bg = c(10, 9, 15);
    const uint16_t accent = c(card->r, card->g, card->b);
    const int cx = FACULTY175_LCD_W / 2;
    const int cy = FACULTY175_LCD_H / 2;

    faculty175_display_fill_rgb565(bg);
    draw_ring(idx, accent);
    draw_card(cx, cy, card, idx);
    faculty175_display_draw_bezel_label("LENORMAND", false, 216, anim_ms, accent);
    faculty175_display_draw_bezel_label(astrolabe_time_valid() ? "DAILY CARD" : "WAITING FOR TIME", true, 216,
                                        anim_ms, c(166, 154, 178));
    faculty175_display_flush();
}
