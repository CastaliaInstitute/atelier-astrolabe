#include "faculty175_face_theritor.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "faculty175_board.h"
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
static theritor_expression_t s_expression = THERITOR_EXPRESSION_CALM;
static char s_respondent[16] = "DANIEL";
static char s_mode[16] = "EDITOR";

static uint16_t rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return faculty175_display_rgb888(r, g, b);
}

void faculty175_face_theritor_set_state(faculty175_ui_state_t state)
{
    s_state = state;
    if (state == FACULTY175_UI_THINK) s_expression = THERITOR_EXPRESSION_THINKING;
    else if (state == FACULTY175_UI_ERROR) s_expression = THERITOR_EXPRESSION_CONCERNED;
}

void faculty175_face_theritor_set_context(const char *respondent, const char *mode)
{
    if (respondent != NULL && respondent[0] != '\0') faculty175_strlcpy(s_respondent, respondent, sizeof(s_respondent));
    if (mode != NULL && mode[0] != '\0') faculty175_strlcpy(s_mode, mode, sizeof(s_mode));
}

void faculty175_face_theritor_set_reply(const char *reply)
{
    if (reply == NULL) return;
    if (strncmp(reply, "🤔", strlen("🤔")) == 0) s_expression = THERITOR_EXPRESSION_THINKING;
    else if (strncmp(reply, "🧐", strlen("🧐")) == 0) s_expression = THERITOR_EXPRESSION_EXAMINING;
    else if (strncmp(reply, "😟", strlen("😟")) == 0) s_expression = THERITOR_EXPRESSION_CONCERNED;
    else if (strncmp(reply, "😊", strlen("😊")) == 0) s_expression = THERITOR_EXPRESSION_WARM;
    else if (strncmp(reply, "😌", strlen("😌")) == 0) s_expression = THERITOR_EXPRESSION_RELIEVED;
    else s_expression = THERITOR_EXPRESSION_CALM;
}

static const char *state_label(void)
{
    switch (s_state) {
        case FACULTY175_UI_CAPTURE: return "LISTENING";
        case FACULTY175_UI_THINK: return "CONSULTING SOURCES";
        case FACULTY175_UI_SPEAK: return "SPEAKING";
        case FACULTY175_UI_ERROR: return "CONNECTION PAUSED";
        default: return "READY TO INTERVIEW";
    }
}

static void draw_eye(int x, int y, bool wink, uint16_t ink)
{
    if (wink) {
        faculty175_display_draw_line(x - 18, y, x + 18, y, ink);
        faculty175_display_draw_line(x - 14, y + 1, x + 14, y + 1, ink);
    } else {
        faculty175_display_fill_circle(x, y, 13, ink);
    }
}

static void draw_expression(int cx, int cy, uint16_t ink, uint16_t accent)
{
    const bool blink = s_state == FACULTY175_UI_THINK;
    draw_eye(cx - 58, cy - 34, blink, ink);
    draw_eye(cx + 58, cy - 34, s_expression == THERITOR_EXPRESSION_EXAMINING, ink);
    if (s_expression == THERITOR_EXPRESSION_EXAMINING) {
        faculty175_display_draw_circle(cx + 58, cy - 34, 25, accent);
        faculty175_display_draw_line(cx + 76, cy - 16, cx + 94, cy + 2, accent);
    }
    if (s_expression == THERITOR_EXPRESSION_CONCERNED) {
        faculty175_display_draw_line(cx - 22, cy + 58, cx, cy + 48, ink);
        faculty175_display_draw_line(cx, cy + 48, cx + 22, cy + 58, ink);
    } else if (s_expression == THERITOR_EXPRESSION_WARM || s_expression == THERITOR_EXPRESSION_RELIEVED) {
        for (int x = -34; x <= 34; ++x) {
            const float u = (float)x / 34.0f;
            const int y = cy + 45 + (int)(18.0f * (1.0f - u * u));
            faculty175_display_draw_pixel(cx + x, y, ink);
        }
    } else {
        faculty175_display_draw_line(cx - 28, cy + 54, cx + 28, cy + 54, ink);
    }
}

void faculty175_face_theritor_draw(uint32_t anim_ms)
{
    const uint16_t bg = rgb(12, 8, 18);
    const uint16_t panel = rgb(29, 20, 40);
    const uint16_t ink = rgb(244, 232, 220);
    const uint16_t dim = rgb(166, 143, 174);
    const uint16_t accent = s_state == FACULTY175_UI_ERROR ? rgb(224, 112, 116) : rgb(214, 155, 196);
    const int cx = FACULTY175_LCD_W / 2;
    const int cy = FACULTY175_LCD_H / 2 - 10;
    const int pulse = 3 + (int)(4.0f * sinf((float)(anim_ms % 2400u) / 2400.0f * 6.2831853f));

    faculty175_display_fill_rgb565(bg);
    faculty175_display_fill_rect(0, 0, FACULTY175_LCD_W, 58, panel);
    faculty175_display_draw_centered_text("THERITOR", 16, accent);
    faculty175_display_draw_circle(cx, cy, 145 + pulse, rgb(72, 44, 76));
    faculty175_display_draw_circle(cx, cy, 132, accent);
    faculty175_display_fill_circle(cx, cy, 118, panel);
    draw_expression(cx, cy, ink, accent);

    char context[40];
    snprintf(context, sizeof(context), "%s / %s", s_respondent, s_mode);
    faculty175_display_draw_centered_text(context, 348, ink);
    faculty175_display_draw_centered_text(state_label(), 376, dim);
    faculty175_display_draw_centered_text("TAP PERSON / HOLD MODE", 404, rgb(112, 92, 124));
    faculty175_display_flush();
}
