#include "faculty175_face_alethiometer.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_random.h"
#include "esp_timer.h"
#include "nvs.h"

#include "faculty175_board.h"
#include "faculty175_face_alethiometer_glyphs.h"

#define ALETH_NVS_NS "alethi"
#define ALETH_SYMBOL_COUNT 36
#define ALETH_NEEDLE_COUNT 4
#define ALETH_QUESTION_MAX 192
#define ALETH_SPOKEN_MAX 384
#define ALETH_REVEAL_Q1_MS 250
#define ALETH_REVEAL_Q2_MS 700
#define ALETH_REVEAL_Q3_MS 1150
#define ALETH_REVEAL_SEARCH_MS 1500
#define ALETH_REVEAL_ANSWER_MS 3000

typedef struct {
    const char *name;
} aleth_symbol_t;

static const aleth_symbol_t k_symbols[ALETH_SYMBOL_COUNT] = {
    {"RIDER"},     {"CLOVER"}, {"SHIP"},      {"HOUSE"}, {"TREE"},    {"CLOUDS"},
    {"SNAKE"},     {"COFFIN"}, {"BOUQUET"},   {"SCYTHE"},{"WHIP"},    {"BIRDS"},
    {"CHILD"},     {"FOX"},    {"BEAR"},      {"STARS"}, {"STORK"},   {"DOG"},
    {"TOWER"},     {"GARDEN"}, {"MOUNTAIN"},  {"ROADS"}, {"MICE"},    {"HEART"},
    {"RING"},      {"BOOK"},   {"LETTER"},    {"MAN"},   {"WOMAN"},   {"LILY"},
    {"SUN"},       {"MOON"},   {"KEY"},       {"FISH"},  {"ANCHOR"},  {"CROSS"},
};

static int s_target[ALETH_NEEDLE_COUNT] = {0, 5, 17, 30};
static int s_display_target[ALETH_NEEDLE_COUNT] = {0, 5, 17, 30};
static float s_angle[ALETH_NEEDLE_COUNT] = {-1.5708f, -0.7f, 1.1f, 2.2f};
static char s_question[ALETH_QUESTION_MAX];
static char s_spoken[ALETH_SPOKEN_MAX];
static bool s_loaded;
static bool s_revealing;
static bool s_searching;
static int64_t s_reveal_started_us;
static int64_t s_search_started_us;
static int s_answer_search_start;
static int s_search_start_target[ALETH_NEEDLE_COUNT];

static uint16_t c(uint8_t r, uint8_t g, uint8_t b)
{
    return faculty175_display_rgb888(r, g, b);
}

static float symbol_angle(int idx)
{
    return -1.5707963f + ((float)idx * 6.2831853f / (float)ALETH_SYMBOL_COUNT);
}

static float norm_angle(float a)
{
    while (a < -3.1415926f) {
        a += 6.2831853f;
    }
    while (a > 3.1415926f) {
        a -= 6.2831853f;
    }
    return a;
}

static void draw_thick_line(int x0, int y0, int x1, int y1, uint16_t color, int w)
{
    for (int d = -w; d <= w; ++d) {
        faculty175_display_draw_line(x0 + d, y0, x1 + d, y1, color);
        faculty175_display_draw_line(x0, y0 + d, x1, y1 + d, color);
    }
}

static void draw_radial_line(int cx, int cy, float angle, int r0, int r1, uint16_t color, int w)
{
    const int x0 = cx + (int)lrintf(cosf(angle) * (float)r0);
    const int y0 = cy + (int)lrintf(sinf(angle) * (float)r0);
    const int x1 = cx + (int)lrintf(cosf(angle) * (float)r1);
    const int y1 = cy + (int)lrintf(sinf(angle) * (float)r1);
    draw_thick_line(x0, y0, x1, y1, color, w);
}

static void save_targets(void)
{
    nvs_handle_t nvs;
    if (nvs_open(ALETH_NVS_NS, NVS_READWRITE, &nvs) != ESP_OK) {
        return;
    }
    for (int i = 0; i < ALETH_NEEDLE_COUNT; ++i) {
        char key[4];
        snprintf(key, sizeof(key), "n%d", i);
        (void)nvs_set_i32(nvs, key, s_target[i]);
    }
    (void)nvs_set_str(nvs, "question", s_question);
    (void)nvs_set_str(nvs, "spoken", s_spoken);
    (void)nvs_commit(nvs);
    nvs_close(nvs);
}

static void load_targets(void)
{
    if (s_loaded) {
        return;
    }
    s_loaded = true;
    nvs_handle_t nvs;
    if (nvs_open(ALETH_NVS_NS, NVS_READONLY, &nvs) != ESP_OK) {
        faculty175_face_alethiometer_cast(esp_random());
        return;
    }
    bool ok = true;
    for (int i = 0; i < ALETH_NEEDLE_COUNT; ++i) {
        char key[4];
        int32_t v = 0;
        snprintf(key, sizeof(key), "n%d", i);
        if (nvs_get_i32(nvs, key, &v) != ESP_OK || v < 0 || v >= ALETH_SYMBOL_COUNT) {
            ok = false;
            break;
        }
        s_target[i] = (int)v;
        s_display_target[i] = s_target[i];
        s_angle[i] = symbol_angle(s_target[i]);
    }
    size_t len = sizeof(s_question);
    if (nvs_get_str(nvs, "question", s_question, &len) != ESP_OK) {
        s_question[0] = '\0';
    }
    len = sizeof(s_spoken);
    if (nvs_get_str(nvs, "spoken", s_spoken, &len) != ESP_OK) {
        s_spoken[0] = '\0';
    }
    nvs_close(nvs);
    if (!ok) {
        faculty175_face_alethiometer_cast(esp_random());
    }
}

void faculty175_face_alethiometer_init(void)
{
    load_targets();
}

void faculty175_face_alethiometer_cast(uint32_t seed)
{
    uint32_t x = seed != 0 ? seed : esp_random();
    bool used[ALETH_SYMBOL_COUNT] = {};
    for (int i = 0; i < ALETH_NEEDLE_COUNT; ++i) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        int idx = (int)(x % ALETH_SYMBOL_COUNT);
        while (used[idx]) {
            idx = (idx + 1) % ALETH_SYMBOL_COUNT;
        }
        used[idx] = true;
        s_target[i] = idx;
        s_display_target[i] = idx;
    }
    s_loaded = true;
    s_question[0] = '\0';
    s_spoken[0] = '\0';
    s_revealing = false;
    s_searching = false;
    save_targets();
}

void faculty175_face_alethiometer_begin_search(void)
{
    load_targets();
    for (int i = 0; i < ALETH_NEEDLE_COUNT; ++i) {
        s_search_start_target[i] = s_display_target[i];
    }
    s_search_started_us = esp_timer_get_time();
    s_revealing = false;
    s_searching = true;
}

void faculty175_face_alethiometer_cancel_search(void)
{
    s_searching = false;
    for (int i = 0; i < ALETH_NEEDLE_COUNT; ++i) {
        s_display_target[i] = s_target[i];
    }
}

static void advance_reveal(void)
{
    if (s_searching) {
        const int64_t elapsed_us = esp_timer_get_time() - s_search_started_us;
        const uint32_t step = elapsed_us > 0 ? (uint32_t)(elapsed_us / 90000) : 0;
        static const int k_search_stride[ALETH_NEEDLE_COUNT] = {1, -2, 3, -1};
        for (int i = 0; i < ALETH_NEEDLE_COUNT; ++i) {
            int target = s_search_start_target[i] + (int)step * k_search_stride[i];
            target %= ALETH_SYMBOL_COUNT;
            if (target < 0) {
                target += ALETH_SYMBOL_COUNT;
            }
            s_display_target[i] = target;
        }
        return;
    }
    if (!s_revealing) {
        return;
    }
    const int64_t elapsed_us = esp_timer_get_time() - s_reveal_started_us;
    const uint32_t elapsed_ms = elapsed_us > 0 ? (uint32_t)(elapsed_us / 1000) : 0;
    if (elapsed_ms >= ALETH_REVEAL_Q1_MS) {
        s_display_target[0] = s_target[0];
    }
    if (elapsed_ms >= ALETH_REVEAL_Q2_MS) {
        s_display_target[1] = s_target[1];
    }
    if (elapsed_ms >= ALETH_REVEAL_Q3_MS) {
        s_display_target[2] = s_target[2];
    }
    if (elapsed_ms >= ALETH_REVEAL_ANSWER_MS) {
        s_display_target[3] = s_target[3];
        s_revealing = false;
    } else if (elapsed_ms >= ALETH_REVEAL_SEARCH_MS) {
        const uint32_t step = (elapsed_ms - ALETH_REVEAL_SEARCH_MS) / 90u;
        s_display_target[3] = (s_answer_search_start + (int)step) % ALETH_SYMBOL_COUNT;
    }
}

static bool parse_json_string_after(const char *json, const char *key, char *out, size_t cap)
{
    if (json == NULL || key == NULL || out == NULL || cap == 0) {
        return false;
    }
    out[0] = '\0';
    const char *p = strstr(json, key);
    if (p == NULL) {
        return false;
    }
    p = strchr(p, ':');
    if (p == NULL) {
        return false;
    }
    p++;
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (*p != '"') {
        return false;
    }
    p++;
    size_t w = 0;
    while (*p != '\0' && *p != '"' && w + 1 < cap) {
        if (*p == '\\' && p[1] != '\0') {
            p++;
            switch (*p) {
                case 'n': out[w++] = ' '; break;
                case 't': out[w++] = ' '; break;
                case '"': out[w++] = '"'; break;
                case '\\': out[w++] = '\\'; break;
                default: out[w++] = *p; break;
            }
            p++;
        } else {
            out[w++] = *p++;
        }
    }
    out[w] = '\0';
    return w > 0;
}

static bool parse_json_int_after(const char *json, const char *key, int *out)
{
    if (json == NULL || key == NULL || out == NULL) {
        return false;
    }
    const char *p = strstr(json, key);
    if (p == NULL) {
        return false;
    }
    p = strchr(p, ':');
    if (p == NULL) {
        return false;
    }
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\"') {
        p++;
    }
    char *end = NULL;
    const long v = strtol(p, &end, 10);
    if (end == p || v < 0 || v >= ALETH_SYMBOL_COUNT) {
        return false;
    }
    *out = (int)v;
    return true;
}

static bool parse_json_int_array_after(const char *json, const char *key, int *out, size_t count)
{
    if (json == NULL || key == NULL || out == NULL || count == 0) {
        return false;
    }
    const char *p = strstr(json, key);
    if (p == NULL) {
        return false;
    }
    p = strchr(p, '[');
    if (p == NULL) {
        return false;
    }
    p++;
    for (size_t i = 0; i < count; ++i) {
        while (*p == ' ' || *p == '\t' || *p == ',' || *p == '\"') {
            p++;
        }
        char *end = NULL;
        const long v = strtol(p, &end, 10);
        if (end == p || v < 0 || v >= ALETH_SYMBOL_COUNT) {
            return false;
        }
        out[i] = (int)v;
        p = end;
    }
    return true;
}

bool faculty175_face_alethiometer_apply_reply(const char *question_text, const char *reply_json)
{
    int question[3] = {};
    int answer = 0;
    char spoken[ALETH_SPOKEN_MAX] = {};
    if (!parse_json_int_array_after(reply_json, "\"questionSymbols\"", question, 3) ||
        !parse_json_int_after(reply_json, "\"answerSymbol\"", &answer)) {
        return false;
    }
    bool used[ALETH_SYMBOL_COUNT] = {};
    for (int i = 0; i < 3; ++i) {
        if (used[question[i]]) {
            return false;
        }
        used[question[i]] = true;
    }
    if (used[answer]) {
        return false;
    }
    s_target[0] = question[0];
    s_target[1] = question[1];
    s_target[2] = question[2];
    s_target[3] = answer;
    s_answer_search_start = (s_display_target[3] + 1) % ALETH_SYMBOL_COUNT;
    s_reveal_started_us = esp_timer_get_time();
    s_searching = false;
    s_revealing = true;
    if (question_text != NULL) {
        strlcpy(s_question, question_text, sizeof(s_question));
    }
    if (parse_json_string_after(reply_json, "\"spoken\"", spoken, sizeof(spoken))) {
        strlcpy(s_spoken, spoken, sizeof(s_spoken));
    }
    s_loaded = true;
    save_targets();
    return true;
}

uint32_t faculty175_face_alethiometer_reveal_duration_ms(void)
{
    return ALETH_REVEAL_ANSWER_MS + 250u;
}

bool faculty175_face_alethiometer_current(int out_targets[4])
{
    if (out_targets == NULL) {
        return false;
    }
    load_targets();
    advance_reveal();
    for (int i = 0; i < ALETH_NEEDLE_COUNT; ++i) {
        out_targets[i] = s_display_target[i];
    }
    return true;
}

bool faculty175_face_alethiometer_context(int out_targets[4],
                                          char *question,
                                          size_t question_cap,
                                          char *spoken,
                                          size_t spoken_cap)
{
    if (out_targets == NULL) {
        return false;
    }
    load_targets();
    for (int i = 0; i < ALETH_NEEDLE_COUNT; ++i) {
        out_targets[i] = s_target[i];
    }
    if (question != NULL && question_cap > 0) {
        strlcpy(question, s_question, question_cap);
    }
    if (spoken != NULL && spoken_cap > 0) {
        strlcpy(spoken, s_spoken, spoken_cap);
    }
    return true;
}

const char *faculty175_face_alethiometer_symbol_name(int idx)
{
    return idx >= 0 && idx < ALETH_SYMBOL_COUNT ? k_symbols[idx].name : "";
}

static void animate_needles(uint32_t anim_ms)
{
    (void)anim_ms;
    advance_reveal();
    for (int i = 0; i < ALETH_NEEDLE_COUNT; ++i) {
        const float target = symbol_angle(s_display_target[i]);
        const float d = norm_angle(target - s_angle[i]);
        s_angle[i] = norm_angle(s_angle[i] + d * (i == 3 ? 0.12f : 0.09f));
    }
}

static void draw_ring(void)
{
    const int cx = FACULTY175_LCD_W / 2;
    const int cy = FACULTY175_LCD_H / 2;
    const uint16_t gold = c(214, 172, 84);
    const uint16_t dim = c(92, 70, 48);
    const uint16_t ink = c(226, 205, 148);
    const uint16_t blue = c(112, 182, 230);

    faculty175_display_draw_circle(cx, cy, 150, dim);
    for (int i = 0; i < ALETH_SYMBOL_COUNT; ++i) {
        const float a = symbol_angle(i);
        const int tx = cx + (int)lrintf(cosf(a) * 174.0f);
        const int ty = cy + (int)lrintf(sinf(a) * 174.0f);
        bool hit = false;
        for (int n = 0; n < ALETH_NEEDLE_COUNT; ++n) {
            hit = hit || s_display_target[n] == i;
        }
        if (hit) {
            faculty175_display_fill_circle(tx, ty, 13, c(26, 22, 22));
            faculty175_display_draw_circle(tx, ty, 13, i == s_display_target[3] ? blue : gold);
        }
        faculty175_face_alethiometer_draw_glyph(tx, ty, i, hit ? (i == s_display_target[3] ? blue : gold) : ink, 1);
    }
}

static void draw_center(void)
{
    const int cx = FACULTY175_LCD_W / 2;
    const int cy = FACULTY175_LCD_H / 2;
    faculty175_display_fill_circle(cx, cy, 92, c(26, 18, 28));
    faculty175_display_draw_circle(cx, cy, 92, c(128, 92, 50));
    faculty175_display_draw_circle(cx, cy, 70, c(70, 54, 62));
    faculty175_display_draw_circle(cx, cy, 48, c(78, 86, 98));
    for (int i = 0; i < 12; ++i) {
        draw_radial_line(cx, cy, -1.5707963f + ((float)i * 6.2831853f / 12.0f), 0, 86,
                         (i % 3 == 0) ? c(136, 100, 54) : c(66, 86, 110), 0);
    }
    faculty175_display_fill_circle(cx, cy, 17, c(20, 14, 20));
    faculty175_display_draw_circle(cx, cy, 17, c(230, 184, 88));
    faculty175_display_fill_circle(cx, cy, 5, c(230, 184, 88));
}

static void draw_needle(float angle, int len, uint16_t color, bool answer)
{
    const int cx = FACULTY175_LCD_W / 2;
    const int cy = FACULTY175_LCD_H / 2;
    draw_radial_line(cx, cy, angle, -22, len, color, 1);
    (void)answer;
}

void faculty175_face_alethiometer_draw(uint32_t anim_ms)
{
    load_targets();
    animate_needles(anim_ms);

    faculty175_display_fill_rgb565(c(8, 7, 12));
    draw_ring();
    draw_center();
    draw_needle(s_angle[0], 132, c(225, 182, 92), false);
    draw_needle(s_angle[1], 122, c(196, 146, 80), false);
    draw_needle(s_angle[2], 112, c(178, 128, 70), false);
    draw_needle(s_angle[3], 184, c(102, 178, 230), true);
    faculty175_display_fill_circle(FACULTY175_LCD_W / 2, FACULTY175_LCD_H / 2, 9, c(238, 198, 96));
    faculty175_display_draw_centered_text("ALETHIOMETER", 44, c(228, 198, 128));

    char line[80];
    snprintf(line, sizeof(line), "%s  %s  %s", k_symbols[s_display_target[0]].name,
             k_symbols[s_display_target[1]].name, k_symbols[s_display_target[2]].name);
    faculty175_display_draw_centered_text(line, 382, c(222, 206, 158));
    snprintf(line, sizeof(line), "ANSWER %s", k_symbols[s_display_target[3]].name);
    faculty175_display_draw_centered_text(line, 404, c(150, 194, 226));
    faculty175_display_flush();
}
