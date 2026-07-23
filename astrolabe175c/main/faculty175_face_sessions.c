#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "faculty175_board.h"
#include "faculty175_face_sessions.h"

static uint16_t rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return faculty175_display_rgb888(r, g, b);
}

static uint8_t sample_level(const uint8_t *waveform,
                            const uint8_t *waveform_stream,
                            size_t waveform_len,
                            size_t index,
                            float phase)
{
    if (waveform_len > 0) {
        const size_t i = index % waveform_len;
        const uint8_t live = waveform_stream != NULL ? waveform_stream[i] : 0;
        const uint8_t captured = waveform != NULL ? waveform[i] : 0;
        const uint8_t level = live > captured ? live : captured;
        if (level > 2) {
            return level;
        }
    }
    return (uint8_t)(22.0f + 18.0f * (sinf(phase + (float)index * 0.63f) + 1.0f));
}

static const char *state_word(faculty175_ui_state_t state)
{
    switch (state) {
        case FACULTY175_UI_CAPTURE: return "listening";
        case FACULTY175_UI_THINK: return "reflecting";
        case FACULTY175_UI_SPEAK: return "speaking";
        case FACULTY175_UI_ERROR: return "offline";
        case FACULTY175_UI_LISTEN:
        default: return "ready";
    }
}

static uint16_t state_accent(faculty175_ui_state_t state, bool journal)
{
    if (state == FACULTY175_UI_ERROR) {
        return rgb(214, 91, 105);
    }
    if (state == FACULTY175_UI_THINK) {
        return journal ? rgb(191, 155, 236) : rgb(126, 158, 238);
    }
    if (state == FACULTY175_UI_SPEAK) {
        return journal ? rgb(229, 188, 239) : rgb(246, 197, 116);
    }
    return journal ? rgb(203, 180, 231) : rgb(111, 226, 205);
}

static void draw_conversation(faculty175_ui_state_t state,
                              const char *detail,
                              uint32_t anim_ms,
                              const uint8_t *waveform,
                              const uint8_t *waveform_stream,
                              size_t waveform_len)
{
    const int cx = FACULTY175_LCD_W / 2;
    const int cy = FACULTY175_LCD_H / 2 - 8;
    const float phase = (float)(anim_ms % 2400u) / 2400.0f * 6.2831853f;
    const uint16_t bg = rgb(4, 10, 15);
    const uint16_t accent = state_accent(state, false);
    const uint16_t dim = rgb(42, 76, 83);
    const uint16_t faint = rgb(20, 39, 46);
    const uint16_t ink = rgb(224, 235, 232);

    faculty175_display_fill_rgb565(bg);
    for (int i = 0; i < 22; ++i) {
        const int x = 26 + ((i * 83) % 414);
        const int y = 36 + ((i * 137) % 360);
        faculty175_display_fill_circle(x, y, (i % 5) == 0 ? 2 : 1, faint);
    }

    if (state == FACULTY175_UI_THINK) {
        for (int orbit = 0; orbit < 3; ++orbit) {
            const float a = phase * (orbit == 1 ? -1.3f : 1.0f) + orbit * 2.0944f;
            const int radius = 78 + orbit * 22;
            faculty175_display_draw_circle(cx, cy, radius, orbit == 0 ? dim : faint);
            faculty175_display_fill_circle(cx + (int)lrintf(cosf(a) * radius),
                                           cy + (int)lrintf(sinf(a) * radius),
                                           5 - orbit,
                                           accent);
        }
    } else {
        for (int ring = 0; ring < 3; ++ring) {
            const int pulse = state == FACULTY175_UI_CAPTURE
                                  ? (int)((anim_ms / 18u + (uint32_t)ring * 31u) % 30u)
                                  : (int)lrintf((sinf(phase + ring * 0.9f) + 1.0f) * 4.0f);
            faculty175_display_draw_circle(cx, cy, 72 + ring * 27 + pulse, ring == 0 ? accent : dim);
        }
    }

    for (int r = 46; r >= 12; r -= 8) {
        const uint8_t shade = (uint8_t)(72 + (46 - r) * 3);
        faculty175_display_fill_circle(cx, cy, r,
                                       state == FACULTY175_UI_ERROR
                                           ? rgb(shade + 45, shade / 2, shade / 2)
                                           : rgb(shade / 2, shade + 55, shade + 48));
    }

    if (state == FACULTY175_UI_CAPTURE || state == FACULTY175_UI_SPEAK) {
        for (int i = 0; i < 24; ++i) {
            const float a = ((float)i / 24.0f) * 6.2831853f;
            const uint8_t level = sample_level(waveform, waveform_stream, waveform_len, (size_t)i, phase);
            const int inner = 54;
            const int outer = inner + 10 + (level * 34) / 255;
            faculty175_display_draw_line(cx + (int)lrintf(cosf(a) * inner),
                                         cy + (int)lrintf(sinf(a) * inner),
                                         cx + (int)lrintf(cosf(a) * outer),
                                         cy + (int)lrintf(sinf(a) * outer),
                                         accent);
        }
    }

    faculty175_display_draw_centered_text("CONVERSATION", 30, dim);
    faculty175_display_fill_circle(178, 367, 3, state == FACULTY175_UI_ERROR ? accent : rgb(92, 188, 147));
    faculty175_display_draw_text(state_word(state), 190, 360, ink);
    if (detail != NULL && detail[0] != '\0' && state != FACULTY175_UI_LISTEN) {
        char brief[32];
        snprintf(brief, sizeof(brief), "%.28s", detail);
        faculty175_display_draw_centered_text(brief, 392, dim);
    } else {
        faculty175_display_draw_centered_text("tap to interrupt", 392, dim);
    }
}

static void draw_journal(faculty175_ui_state_t state,
                         const char *detail,
                         uint32_t anim_ms,
                         const uint8_t *waveform,
                         const uint8_t *waveform_stream,
                         size_t waveform_len)
{
    const float phase = (float)(anim_ms % 2600u) / 2600.0f * 6.2831853f;
    const uint16_t bg = rgb(8, 9, 18);
    const uint16_t paper = rgb(15, 16, 30);
    const uint16_t guide = rgb(33, 31, 53);
    const uint16_t accent = state_accent(state, true);
    const uint16_t ink = rgb(224, 218, 233);
    const uint16_t dim = rgb(112, 105, 133);
    const int left = 62;
    const int right = FACULTY175_LCD_W - 62;

    faculty175_display_fill_rgb565(bg);
    faculty175_display_fill_rect(left, 50, right - left, 344, paper);
    faculty175_display_draw_line(left + 26, 50, left + 26, 394, rgb(58, 41, 59));
    for (int y = 104; y <= 342; y += 34) {
        faculty175_display_draw_line(left + 14, y, right - 14, y, guide);
    }

    faculty175_display_draw_centered_text("JOURNAL", 24, dim);
    const int baseline = 218;
    const int samples = 38;
    for (int i = 0; i < samples; ++i) {
        const uint8_t level = sample_level(waveform, waveform_stream, waveform_len, (size_t)i, phase);
        const int x = left + 18 + i * (right - left - 36) / (samples - 1);
        const int h = 3 + (level * 30) / 255;
        faculty175_display_draw_line(x, baseline - h, x, baseline + h, accent);
    }

    /* A few quiet strokes suggest accumulating handwritten thought without
     * inventing transcript text that the recognizer has not delivered. */
    for (int row = 0; row < 3; ++row) {
        const int y = 270 + row * 34;
        const int length = 190 + ((row * 47 + (int)(anim_ms / 240u)) % 70);
        faculty175_display_draw_line(left + 42, y, left + 42 + length, y, row == 0 ? ink : dim);
        faculty175_display_fill_circle(left + 32, y, 2, accent);
    }

    faculty175_display_fill_circle(151, 418, 3, state == FACULTY175_UI_ERROR ? rgb(214, 91, 105)
                                                                             : rgb(92, 188, 147));
    faculty175_display_draw_text(state == FACULTY175_UI_ERROR ? "saved locally" : "recording safely",
                                 163,
                                 411,
                                 dim);
    if (detail != NULL && detail[0] != '\0' && state != FACULTY175_UI_LISTEN) {
        char brief[28];
        snprintf(brief, sizeof(brief), "%.24s", detail);
        faculty175_display_draw_centered_text(brief, 366, accent);
    } else {
        faculty175_display_draw_centered_text(state_word(state), 366, accent);
    }
}

void faculty175_face_session_draw(bool journal,
                                  faculty175_ui_state_t state,
                                  const char *detail,
                                  uint32_t anim_ms,
                                  const uint8_t *waveform,
                                  const uint8_t *waveform_stream,
                                  size_t waveform_len)
{
    if (journal) {
        draw_journal(state, detail, anim_ms, waveform, waveform_stream, waveform_len);
    } else {
        draw_conversation(state, detail, anim_ms, waveform, waveform_stream, waveform_len);
    }
    faculty175_display_flush();
}

void faculty175_face_journal_draw(uint32_t anim_ms)
{
    faculty175_face_session_draw(true, FACULTY175_UI_LISTEN, NULL, anim_ms, NULL, NULL, 0);
}

void faculty175_face_conversation_draw(uint32_t anim_ms)
{
    faculty175_face_session_draw(false, FACULTY175_UI_LISTEN, NULL, anim_ms, NULL, NULL, 0);
}
