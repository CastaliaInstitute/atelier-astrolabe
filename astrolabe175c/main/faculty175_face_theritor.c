#include "faculty175_face_theritor.h"

#include <string.h>

#include "esp_attr.h"
#include "faculty175_board.h"
#include "faculty175_theritor_emojinq.h"
#include "faculty175_util.h"

typedef enum {
    THERITOR_EXPRESSION_CALM = 0,
    THERITOR_EXPRESSION_THINKING,
    THERITOR_EXPRESSION_EXAMINING,
    THERITOR_EXPRESSION_CONCERNED,
    THERITOR_EXPRESSION_WARM,
    THERITOR_EXPRESSION_RELIEVED,
} theritor_expression_t;

static faculty175_ui_state_t s_state = FACULTY175_UI_LISTEN;
static theritor_expression_t s_expression = THERITOR_EXPRESSION_WARM;
static char s_respondent[16] = "DANIEL";
static char s_mode[16] = "EDITOR";
EXT_RAM_BSS_ATTR static uint16_t s_emojinq_sprite[THERITOR_EMOJINQ_SIZE * THERITOR_EMOJINQ_SIZE];
static int s_emojinq_sprite_expression = -1;
static bool s_dirty = true;

static uint16_t rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return faculty175_display_rgb888(r, g, b);
}

void faculty175_face_theritor_set_state(faculty175_ui_state_t state)
{
    if (s_state == state) return;
    const faculty175_ui_state_t previous = s_state;
    s_state = state;
    if (state == FACULTY175_UI_THINK) s_expression = THERITOR_EXPRESSION_THINKING;
    else if (state == FACULTY175_UI_ERROR) s_expression = THERITOR_EXPRESSION_CONCERNED;
    else if (previous == FACULTY175_UI_ERROR) s_expression = THERITOR_EXPRESSION_WARM;
    /* SPEAK and LISTEN intentionally retain the expression selected from the
     * LLM's Unicode emoji. The face should remain the nonverbal part of the
     * answer instead of snapping back to an unrelated idle smile. */
    s_dirty = true;
}

void faculty175_face_theritor_set_context(const char *respondent, const char *mode)
{
    if (respondent != NULL && respondent[0] != '\0' && strcmp(s_respondent, respondent) != 0) {
        faculty175_strlcpy(s_respondent, respondent, sizeof(s_respondent));
        s_dirty = true;
    }
    if (mode != NULL && mode[0] != '\0' && strcmp(s_mode, mode) != 0) {
        faculty175_strlcpy(s_mode, mode, sizeof(s_mode));
        s_dirty = true;
    }
}

void faculty175_face_theritor_set_reply(const char *reply)
{
    if (reply == NULL) return;
    theritor_expression_t next = THERITOR_EXPRESSION_WARM;
    if (strncmp(reply, "🤔", strlen("🤔")) == 0) next = THERITOR_EXPRESSION_THINKING;
    else if (strncmp(reply, "🧐", strlen("🧐")) == 0) next = THERITOR_EXPRESSION_EXAMINING;
    else if (strncmp(reply, "😟", strlen("😟")) == 0) next = THERITOR_EXPRESSION_CONCERNED;
    else if (strncmp(reply, "😊", strlen("😊")) == 0) next = THERITOR_EXPRESSION_WARM;
    else if (strncmp(reply, "😌", strlen("😌")) == 0) next = THERITOR_EXPRESSION_RELIEVED;
    if (s_expression != next) {
        s_expression = next;
        s_dirty = true;
    }
}

void faculty175_face_theritor_invalidate(void)
{
    s_dirty = true;
}

static void draw_emojinq_expression(int left, int top, uint16_t background)
{
    if (s_emojinq_sprite_expression != (int)s_expression) {
        const theritor_emojinq_glyph_t *glyph = &k_theritor_emojinq_glyphs[(int)s_expression];
        const uint16_t silver_shadow = rgb(82, 88, 98);
        const uint16_t silver_low = rgb(164, 170, 180);
        const uint16_t silver_mid = rgb(204, 209, 218);
        const uint16_t silver_high = rgb(236, 239, 244);
        for (size_t i = 0; i < sizeof(s_emojinq_sprite) / sizeof(s_emojinq_sprite[0]); ++i) {
            s_emojinq_sprite[i] = background;
        }

        /* A restrained offset gives the monochrome Emojinq brush form the
         * depth of engraved silver without recoloring it as a platform emoji. */
        for (uint16_t i = 0; i < glyph->count; ++i) {
            const theritor_emojinq_span_t *span = &glyph->spans[i];
            if (span->y + 2 >= THERITOR_EMOJINQ_SIZE) continue;
            const int x1 = span->x1 + 2 < THERITOR_EMOJINQ_SIZE
                               ? span->x1 + 2
                               : THERITOR_EMOJINQ_SIZE - 1;
            for (int x = span->x0 + 2; x <= x1; ++x) {
                s_emojinq_sprite[(span->y + 2) * THERITOR_EMOJINQ_SIZE + x] = silver_shadow;
            }
        }
        for (uint16_t i = 0; i < glyph->count; ++i) {
            const theritor_emojinq_span_t *span = &glyph->spans[i];
            const uint16_t silver = span->y < (THERITOR_EMOJINQ_SIZE * 28 / 100) ? silver_high
                                  : span->y < (THERITOR_EMOJINQ_SIZE * 68 / 100) ? silver_mid
                                                 : silver_low;
            for (int x = span->x0; x <= span->x1; ++x) {
                s_emojinq_sprite[span->y * THERITOR_EMOJINQ_SIZE + x] = silver;
            }
        }
        s_emojinq_sprite_expression = (int)s_expression;
    }
    faculty175_display_draw_rgb565(s_emojinq_sprite,
                                   left,
                                   top,
                                   THERITOR_EMOJINQ_SIZE,
                                   THERITOR_EMOJINQ_SIZE);
}

void faculty175_face_theritor_draw(uint32_t anim_ms)
{
    (void)anim_ms;
    if (!s_dirty) return;
    const uint16_t bg = rgb(7, 8, 11);
    const int cy = FACULTY175_LCD_H / 2;

    faculty175_display_fill_rgb565(bg);
    draw_emojinq_expression((FACULTY175_LCD_W - THERITOR_EMOJINQ_SIZE) / 2,
                            cy - THERITOR_EMOJINQ_SIZE / 2,
                            bg);
    faculty175_display_flush();
    s_dirty = false;
}
