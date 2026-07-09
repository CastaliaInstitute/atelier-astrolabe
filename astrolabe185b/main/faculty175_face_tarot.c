#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_random.h"

#include "faculty175_board.h"
#include "faculty175_face_tarot.h"
#include "faculty175_face_tarot_assets.h"
#include "faculty175_face_tarot_image.h"

static int s_card = -1;
static bool s_loaded;

static uint16_t c(uint8_t r, uint8_t g, uint8_t b)
{
    return faculty175_display_rgb888(r, g, b);
}

static void centered_at(const char *text, int cx, int y, uint16_t color)
{
    if (text == NULL || text[0] == '\0') {
        return;
    }
    const int w = (int)strlen(text) * 6;
    faculty175_display_draw_text(text, cx - w / 2, y, color);
}

static void thick_line(int x0, int y0, int x1, int y1, uint16_t color, int half_w)
{
    for (int d = -half_w; d <= half_w; ++d) {
        faculty175_display_draw_line(x0 + d, y0, x1 + d, y1, color);
        faculty175_display_draw_line(x0, y0 + d, x1, y1 + d, color);
    }
}

static void radial_line(int cx, int cy, float angle, int r0, int r1, uint16_t color)
{
    const int x0 = cx + (int)lrintf(cosf(angle) * (float)r0);
    const int y0 = cy + (int)lrintf(sinf(angle) * (float)r0);
    const int x1 = cx + (int)lrintf(cosf(angle) * (float)r1);
    const int y1 = cy + (int)lrintf(sinf(angle) * (float)r1);
    faculty175_display_draw_line(x0, y0, x1, y1, color);
}

static uint16_t card_color(const faculty175_tarot_card_t *card, int lift)
{
    const int r = (int)card->r + lift;
    const int g = (int)card->g + lift;
    const int b = (int)card->b + lift;
    return c((uint8_t)(r < 0 ? 0 : (r > 255 ? 255 : r)),
             (uint8_t)(g < 0 ? 0 : (g > 255 ? 255 : g)),
             (uint8_t)(b < 0 ? 0 : (b > 255 ? 255 : b)));
}

static void load_card(void)
{
    if (s_loaded) {
        return;
    }
    s_loaded = true;
    s_card = (int)(esp_random() % FACULTY175_TAROT_CARD_COUNT);
}

void faculty175_face_tarot_draw_card(uint32_t seed_ms)
{
    int next = (int)((esp_random() ^ seed_ms) % FACULTY175_TAROT_CARD_COUNT);
    if (s_card == next) {
        next = (next + 1) % FACULTY175_TAROT_CARD_COUNT;
    }
    s_card = next;
    s_loaded = true;
}

int faculty175_face_tarot_current_card(void)
{
    load_card();
    return s_card;
}

static void draw_arcana_mark(int idx, int cx, int cy, uint16_t ink, uint16_t shadow)
{
    const int n = idx % 22;
    if (n == 0) {
        faculty175_display_draw_circle(cx, cy, 36, ink);
        faculty175_display_fill_circle(cx, cy - 20, 5, ink);
        thick_line(cx - 18, cy + 10, cx + 22, cy - 14, ink, 1);
        return;
    }
    const int spokes = 3 + (n % 8);
    for (int i = 0; i < spokes; ++i) {
        const float a = -1.5707963f + ((float)i * 6.2831853f / (float)spokes);
        radial_line(cx, cy, a, 8, 42, i == 0 ? ink : shadow);
    }
    faculty175_display_draw_circle(cx, cy, 42, ink);
    faculty175_display_draw_circle(cx, cy, 26, shadow);
    faculty175_display_fill_circle(cx, cy, 7, ink);
}

static void draw_round_table(const faculty175_tarot_card_t *card, int selected, uint32_t anim_ms)
{
    const int cx = FACULTY175_LCD_W / 2;
    const int cy = FACULTY175_LCD_H / 2 + 4;
    const uint16_t dim = c(68, 48, 64);
    const uint16_t gold = c(218, 178, 86);
    const uint16_t ink = c(238, 232, 210);
    const uint16_t accent = card_color(card, 24);

    faculty175_display_draw_circle(cx, cy, 214, c(64, 42, 54));
    faculty175_display_draw_circle(cx, cy, 205, gold);
    faculty175_display_draw_circle(cx, cy, 186, dim);
    faculty175_display_draw_circle(cx, cy, 150, c(42, 32, 48));
    for (int i = 0; i < FACULTY175_TAROT_CARD_COUNT; ++i) {
        const float a = -1.5707963f + ((float)i * 6.2831853f / (float)FACULTY175_TAROT_CARD_COUNT);
        radial_line(cx, cy, a, i == selected ? 176 : 190, 207, i == selected ? accent : dim);
        const int x = cx + (int)lrintf(cosf(a) * 166.0f);
        const int y = cy + (int)lrintf(sinf(a) * 166.0f);
        faculty175_display_fill_circle(x, y, i == selected ? 8 : 4, i == selected ? accent : c(118, 92, 82));
    }

    const float spin = (float)(anim_ms % 9000u) / 9000.0f * 6.2831853f;
    for (int i = 0; i < 7; ++i) {
        const float a = spin + ((float)i * 6.2831853f / 7.0f);
        const int x = cx + (int)lrintf(cosf(a) * 116.0f);
        const int y = cy + (int)lrintf(sinf(a) * 116.0f);
        faculty175_display_fill_circle(x, y, 2 + (i % 2), c(138, 112, 74));
    }
    faculty175_display_fill_circle(cx, cy, 112, c(18, 14, 24));
    faculty175_display_draw_circle(cx, cy, 112, accent);
    faculty175_display_draw_circle(cx, cy, 96, c(84, 58, 72));
    draw_arcana_mark(selected, cx, cy - 12, ink, accent);
}

void faculty175_face_tarot_draw(uint32_t anim_ms)
{
    load_card();
    const faculty175_tarot_card_t *card = faculty175_tarot_card_get(s_card);
    if (card == NULL) {
        return;
    }
    const uint16_t bg = c(7, 6, 12);
    const uint16_t header = c(22, 16, 26);
    const uint16_t ink = c(238, 232, 210);
    const uint16_t dim = c(148, 132, 118);
    const uint16_t accent = card_color(card, 24);

    faculty175_tarot_image_request(s_card, card);

    faculty175_display_fill_rgb565(bg);
    const bool image_drawn = faculty175_tarot_image_draw_cached(s_card);

    faculty175_display_fill_rect(0, 0, FACULTY175_LCD_W, 54, image_drawn ? c(6, 7, 11) : header);
    faculty175_display_draw_centered_text("TAROT", 14, accent);
    faculty175_display_draw_centered_text("FULL DECK", 34, dim);

    if (!image_drawn) {
        draw_round_table(card, s_card, anim_ms);
    }

    faculty175_display_fill_rect(0, 330, FACULTY175_LCD_W, 136, image_drawn ? c(6, 7, 11) : bg);
    faculty175_display_draw_line(0, 330, FACULTY175_LCD_W, 330, accent);
    char line[80];
    snprintf(line, sizeof(line), "%s  %s", card->roman, card->title);
    faculty175_display_draw_centered_text(line, 344, ink);
    snprintf(line, sizeof(line), "CARD OF THE DAY: %s", card->keyword);
    faculty175_display_draw_centered_text(line, 370, accent);
    if (image_drawn) {
        centered_at("BUTTON DRAWS AGAIN", FACULTY175_LCD_W / 2, 400, dim);
    } else if (faculty175_tarot_image_busy()) {
        centered_at("FETCHING CARD ART", FACULTY175_LCD_W / 2, 400, dim);
    } else {
        const char *err = faculty175_tarot_image_error();
        centered_at((err != NULL && err[0] != '\0') ? err : "BUTTON DRAWS AGAIN", FACULTY175_LCD_W / 2, 400, dim);
    }

    faculty175_display_flush();
}
