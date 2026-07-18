#include "faculty175_face_babel.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "faculty175_board.h"
#include "faculty175_util.h"

typedef struct {
    const char *alias;
    const char *display;
    const char *code;
} babel_language_t;

typedef struct {
    bool active;
    char language[32];
    char code[8];
    char phrase[128];
    char transcript[192];
    char reply[192];
    uint32_t updated_ms;
} babel_state_t;

static portMUX_TYPE s_babel_mux = portMUX_INITIALIZER_UNLOCKED;
EXT_RAM_BSS_ATTR static babel_state_t s_babel;

static const babel_language_t s_languages[] = {
    { "spanish", "Spanish", "ES" },
    { "espanol", "Spanish", "ES" },
    { "french", "French", "FR" },
    { "german", "German", "DE" },
    { "italian", "Italian", "IT" },
    { "portuguese", "Portuguese", "PT" },
    { "brazilian portuguese", "Portuguese", "PT" },
    { "japanese", "Japanese", "JA" },
    { "korean", "Korean", "KO" },
    { "chinese", "Chinese", "ZH" },
    { "mandarin", "Mandarin", "ZH" },
    { "cantonese", "Cantonese", "YUE" },
    { "arabic", "Arabic", "AR" },
    { "hindi", "Hindi", "HI" },
    { "latin", "Latin", "LA" },
    { "greek", "Greek", "EL" },
    { "hebrew", "Hebrew", "HE" },
    { "russian", "Russian", "RU" },
    { "ukrainian", "Ukrainian", "UK" },
    { "polish", "Polish", "PL" },
    { "dutch", "Dutch", "NL" },
    { "swedish", "Swedish", "SV" },
    { "norwegian", "Norwegian", "NO" },
    { "danish", "Danish", "DA" },
    { "finnish", "Finnish", "FI" },
    { "turkish", "Turkish", "TR" },
    { "vietnamese", "Vietnamese", "VI" },
    { "thai", "Thai", "TH" },
    { "irish", "Irish", "GA" },
    { "welsh", "Welsh", "CY" },
    { "english", "English", "EN" },
};

static uint16_t rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return faculty175_display_rgb888(r, g, b);
}

static bool is_boundary(char c)
{
    return c == '\0' || isspace((unsigned char)c) || c == ',' || c == '.' || c == ':' || c == ';' ||
           c == '"' || c == '\'' || c == '?' || c == '!';
}

static void lower_copy(char *out, size_t out_len, const char *in)
{
    if (out_len == 0) {
        return;
    }
    size_t i = 0;
    for (; i + 1 < out_len && in != NULL && in[i] != '\0'; ++i) {
        out[i] = (char)tolower((unsigned char)in[i]);
    }
    out[i] = '\0';
}

static void trim_phrase(char *text)
{
    if (text == NULL) {
        return;
    }
    char *start = text;
    while (*start != '\0' && (isspace((unsigned char)*start) || *start == ',' || *start == ':' ||
                              *start == ';' || *start == '"' || *start == '\'')) {
        ++start;
    }
    if (start != text) {
        memmove(text, start, strlen(start) + 1);
    }
    size_t len = strlen(text);
    while (len > 0) {
        const char c = text[len - 1];
        if (!isspace((unsigned char)c) && c != '"' && c != '\'' && c != '.' && c != ',' && c != ';') {
            break;
        }
        text[--len] = '\0';
    }
}

static void uppercase_copy(char *out, size_t out_len, const char *in)
{
    if (out_len == 0) {
        return;
    }
    size_t i = 0;
    for (; i + 1 < out_len && in != NULL && in[i] != '\0'; ++i) {
        out[i] = (char)toupper((unsigned char)in[i]);
    }
    out[i] = '\0';
}

static bool capture_language_after(const char *lower,
                                   const char *original,
                                   const char *prefix,
                                   const babel_language_t **out_lang,
                                   const char **out_phrase)
{
    const char *hit = strstr(lower, prefix);
    if (hit == NULL) {
        return false;
    }
    const size_t prefix_len = strlen(prefix);
    const char *after = hit + prefix_len;
    for (size_t i = 0; i < sizeof(s_languages) / sizeof(s_languages[0]); ++i) {
        const size_t alias_len = strlen(s_languages[i].alias);
        if (strncmp(after, s_languages[i].alias, alias_len) == 0 && is_boundary(after[alias_len])) {
            const size_t phrase_off = (size_t)(after - lower) + alias_len;
            *out_lang = &s_languages[i];
            *out_phrase = original + phrase_off;
            return true;
        }
    }
    return false;
}

static bool capture_in_language_say(const char *lower,
                                    const char *original,
                                    const babel_language_t **out_lang,
                                    const char **out_phrase)
{
    const char *hit = strstr(lower, "in ");
    while (hit != NULL) {
        const char *after = hit + 3;
        for (size_t i = 0; i < sizeof(s_languages) / sizeof(s_languages[0]); ++i) {
            const size_t alias_len = strlen(s_languages[i].alias);
            if (strncmp(after, s_languages[i].alias, alias_len) != 0 || !is_boundary(after[alias_len])) {
                continue;
            }
            const char *say = strstr(after + alias_len, "say ");
            if (say != NULL) {
                *out_lang = &s_languages[i];
                *out_phrase = original + (size_t)(say - lower) + 4;
                return true;
            }
        }
        hit = strstr(hit + 1, "in ");
    }
    return false;
}

static bool parse_translate_intent(const char *transcript,
                                   const babel_language_t **out_lang,
                                   char *out_phrase,
                                   size_t out_phrase_len)
{
    char lower[224];
    lower_copy(lower, sizeof(lower), transcript);

    const char *phrase = NULL;
    const babel_language_t *lang = NULL;
    if (!capture_language_after(lower, transcript, "say in ", &lang, &phrase) &&
        !capture_language_after(lower, transcript, "translate to ", &lang, &phrase) &&
        !capture_language_after(lower, transcript, "translate in ", &lang, &phrase) &&
        !capture_in_language_say(lower, transcript, &lang, &phrase)) {
        return false;
    }
    if (lang == NULL || phrase == NULL) {
        return false;
    }

    faculty175_strlcpy(out_phrase, phrase, out_phrase_len);
    trim_phrase(out_phrase);
    if (out_phrase[0] == '\0') {
        faculty175_strlcpy(out_phrase, transcript, out_phrase_len);
        trim_phrase(out_phrase);
    }
    *out_lang = lang;
    return true;
}

bool faculty175_face_babel_update_from_transcript(const char *transcript)
{
    if (transcript == NULL || transcript[0] == '\0') {
        return false;
    }
    const babel_language_t *lang = NULL;
    char phrase[128] = {};
    if (!parse_translate_intent(transcript, &lang, phrase, sizeof(phrase))) {
        return false;
    }

    const uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    portENTER_CRITICAL(&s_babel_mux);
    s_babel.active = true;
    faculty175_strlcpy(s_babel.language, lang->display, sizeof(s_babel.language));
    faculty175_strlcpy(s_babel.code, lang->code, sizeof(s_babel.code));
    faculty175_strlcpy(s_babel.phrase, phrase, sizeof(s_babel.phrase));
    faculty175_strlcpy(s_babel.transcript, transcript, sizeof(s_babel.transcript));
    s_babel.reply[0] = '\0';
    s_babel.updated_ms = now_ms;
    portEXIT_CRITICAL(&s_babel_mux);
    return true;
}

void faculty175_face_babel_set_reply(const char *reply)
{
    if (reply == NULL || reply[0] == '\0') {
        return;
    }
    const uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    portENTER_CRITICAL(&s_babel_mux);
    if (s_babel.active) {
        faculty175_strlcpy(s_babel.reply, reply, sizeof(s_babel.reply));
        s_babel.updated_ms = now_ms;
    }
    portEXIT_CRITICAL(&s_babel_mux);
}

bool faculty175_face_babel_active(void)
{
    bool active = false;
    portENTER_CRITICAL(&s_babel_mux);
    active = s_babel.active;
    portEXIT_CRITICAL(&s_babel_mux);
    return active;
}

static void centered_at(const char *text, int cx, int y, uint16_t color)
{
    if (text == NULL || text[0] == '\0') {
        return;
    }
    const int w = (int)strlen(text) * 6;
    faculty175_display_draw_text(text, cx - w / 2, y, color);
}

static void draw_wrapped(const char *text, int x, int y, int max_chars, int max_lines, uint16_t color)
{
    if (text == NULL || text[0] == '\0' || max_chars <= 0 || max_lines <= 0) {
        return;
    }
    char upper[192];
    uppercase_copy(upper, sizeof(upper), text);
    const char *p = upper;
    for (int line = 0; line < max_lines && *p != '\0'; ++line) {
        while (*p == ' ') {
            ++p;
        }
        char row[40] = {};
        int n = 0;
        int last_space = -1;
        while (p[n] != '\0' && n < max_chars && n + 1 < (int)sizeof(row)) {
            row[n] = p[n];
            if (row[n] == ' ') {
                last_space = n;
            }
            ++n;
        }
        if (p[n] != '\0' && last_space > 0) {
            row[last_space] = '\0';
            p += last_space + 1;
        } else {
            row[n] = '\0';
            p += n;
        }
        faculty175_display_draw_text(row, x, y + line * 18, color);
    }
}

static void draw_fish(int cx, int cy, int scale, uint32_t anim_ms)
{
    if (scale < 1) {
        scale = 1;
    }
    const int bob = (int)((anim_ms / 180u) % 7u) - 3;
    cy += bob;
    const uint16_t orange = rgb(245, 142, 42);
    const uint16_t gold = rgb(255, 196, 86);
    const uint16_t amber = rgb(210, 88, 28);
    const uint16_t dark = rgb(18, 24, 30);
    const uint16_t fin = rgb(255, 176, 62);

    faculty175_display_fill_circle(cx, cy, 28 * scale, orange);
    faculty175_display_fill_circle(cx + 22 * scale, cy, 20 * scale, orange);
    faculty175_display_fill_circle(cx - 8 * scale, cy - 8 * scale, 17 * scale, gold);

    const int tail_x = cx - 42 * scale;
    faculty175_display_draw_line(tail_x, cy, tail_x - 30 * scale, cy - 24 * scale, amber);
    faculty175_display_draw_line(tail_x, cy, tail_x - 30 * scale, cy + 24 * scale, amber);
    faculty175_display_draw_line(tail_x - 30 * scale, cy - 24 * scale, tail_x - 18 * scale, cy, amber);
    faculty175_display_draw_line(tail_x - 30 * scale, cy + 24 * scale, tail_x - 18 * scale, cy, amber);
    for (int i = 0; i < 8 * scale; ++i) {
        faculty175_display_draw_line(tail_x - i, cy - i / 2, tail_x - 24 * scale + i, cy - 20 * scale + i, amber);
        faculty175_display_draw_line(tail_x - i, cy + i / 2, tail_x - 24 * scale + i, cy + 20 * scale - i, amber);
    }

    const int fin_wave = (int)((anim_ms / 120u) % 5u) - 2;
    faculty175_display_draw_line(cx - 6 * scale, cy - 26 * scale, cx + 14 * scale, cy - (42 + fin_wave) * scale, fin);
    faculty175_display_draw_line(cx + 14 * scale, cy - (42 + fin_wave) * scale, cx + 24 * scale, cy - 20 * scale, fin);
    faculty175_display_draw_line(cx - 4 * scale, cy + 20 * scale, cx + 18 * scale, cy + (42 - fin_wave) * scale, fin);
    faculty175_display_draw_line(cx + 18 * scale, cy + (42 - fin_wave) * scale, cx + 30 * scale, cy + 14 * scale, fin);

    faculty175_display_fill_circle(cx + 38 * scale, cy - 8 * scale, 5 * scale, rgb(246, 250, 250));
    faculty175_display_fill_circle(cx + 40 * scale, cy - 8 * scale, 2 * scale, dark);
    faculty175_display_draw_line(cx + 35 * scale, cy + 14 * scale, cx + 48 * scale, cy + 10 * scale, amber);
    faculty175_display_draw_circle(cx + 2 * scale, cy, 31 * scale, rgb(255, 184, 72));
}

void faculty175_face_babel_draw(uint32_t anim_ms)
{
    babel_state_t state = {};
    portENTER_CRITICAL(&s_babel_mux);
    state = s_babel;
    portEXIT_CRITICAL(&s_babel_mux);

    const int cx = FACULTY175_LCD_W / 2;
    const uint16_t bg = rgb(5, 8, 13);
    const uint16_t panel = rgb(15, 22, 30);
    const uint16_t accent = rgb(82, 198, 236);
    const uint16_t warm = rgb(238, 178, 82);
    const uint16_t text = rgb(232, 238, 246);
    const uint16_t dim = rgb(126, 142, 158);
    const uint16_t line = rgb(42, 54, 68);

    faculty175_display_fill_rgb565(bg);
    faculty175_display_fill_rect(64, 92, 338, 296, panel);
    faculty175_display_draw_line(64, 92, 402, 92, accent);
    faculty175_display_draw_line(402, 92, 402, 388, line);
    faculty175_display_draw_line(402, 388, 64, 388, line);
    faculty175_display_draw_line(64, 388, 64, 92, line);

    draw_fish(cx, state.active ? 164 : 176, 1, anim_ms);

    if (!state.active) {
        centered_at("SAY IN SPANISH", cx, 248, warm);
        centered_at("\"HELLO\"", cx, 276, text);
        centered_at("LISTENING FOR TRANSLATE", cx, 334, dim);
        faculty175_display_draw_bezel_label("BABEL FISH", false, 216, anim_ms, warm);
        faculty175_display_flush();
        return;
    }

    char code[12];
    uppercase_copy(code, sizeof(code), state.code);
    centered_at("TO", cx - 82, 118, dim);
    centered_at(code, cx + 82, 118, text);

    char lang[40];
    uppercase_copy(lang, sizeof(lang), state.language);
    centered_at(lang, cx, 238, warm);
    faculty175_display_draw_text("PHRASE", 96, 276, dim);
    draw_wrapped(state.phrase, 96, 296, 34, 2, text);

    if (state.reply[0] != '\0') {
        faculty175_display_draw_text("CASTALIA", 96, 344, dim);
        draw_wrapped(state.reply, 96, 364, 34, 1, accent);
    } else {
        faculty175_display_draw_text("HEARD", 96, 344, dim);
        draw_wrapped(state.transcript, 96, 364, 34, 1, accent);
    }

    faculty175_display_draw_bezel_label("BABEL FISH", false, 216, anim_ms, accent);
    faculty175_display_draw_bezel_label(state.language, true, 216, anim_ms, warm);
    faculty175_display_flush();
}
