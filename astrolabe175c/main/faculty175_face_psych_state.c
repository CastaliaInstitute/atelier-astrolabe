#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include "faculty175_board.h"
#include "faculty175_face_psych_state.h"
#include "nvs.h"

#define PSYCH_STATE_NVS_NS "psych_state"
#define PSYCH_STATE_NVS_SKIN "skin"
#define PSYCH_STATE_NVS_HAIR "hair"
#define PSYCH_STATE_NVS_EYES "eyes"
#define PSYCH_STATE_NVS_BEARD "beard"
#define PSYCH_STATE_NVS_GLASSES "glasses"
#define PSYCH_STATE_NVS_NOSE "nose"
#define PSYCH_STATE_NVS_MOUTH "mouth"
#define PSYCH_STATE_NVS_AROUSAL "arousal"
#define PSYCH_STATE_NVS_VALENCE "valence"
#define PSYCH_STATE_NVS_MOOD "mood"
#define PSYCH_STATE_DEFAULT_SKIN 1
#define PSYCH_STATE_DEFAULT_HAIR 0
#define PSYCH_STATE_DEFAULT_EYE 2
#define PSYCH_STATE_DEFAULT_BEARD 0
#define PSYCH_STATE_DEFAULT_GLASSES 0
#define PSYCH_STATE_DEFAULT_NOSE 2
#define PSYCH_STATE_DEFAULT_MOUTH 0
#define PSYCH_STATE_DEFAULT_AROUSAL 50
#define PSYCH_STATE_DEFAULT_VALENCE 50
#define PSYCH_STATE_SKIN_COUNT 6
#define PSYCH_STATE_HAIR_COUNT 6
#define PSYCH_STATE_EYE_COUNT 6
#define PSYCH_STATE_BEARD_COUNT 4
#define PSYCH_STATE_GLASSES_COUNT 2
#define PSYCH_STATE_NOSE_COUNT 5
#define PSYCH_STATE_MOUTH_COUNT 6
#define PSYCH_STATE_EMOTION_COUNT 101
#define PSYCH_STATE_MOOD_COUNT 6

typedef enum {
    PSYCH_STYLE_SKIN = 0,
    PSYCH_STYLE_HAIR = 1,
    PSYCH_STYLE_EYES = 2,
    PSYCH_STYLE_BEARD = 3,
    PSYCH_STYLE_GLASSES = 4,
    PSYCH_STYLE_NOSE = 5,
    PSYCH_STYLE_MOUTH = 6,
    PSYCH_STYLE_AROUSAL = 7,
    PSYCH_STYLE_VALENCE = 8,
    PSYCH_STYLE_COUNT = 9,
} psych_state_style_field_t;

typedef struct {
    bool valid;
    bool has_style;
    uint8_t skin_tone;
    uint8_t hair_color;
    uint8_t eye_color;
    uint8_t facial_hair;
    uint8_t glasses;
} faculty175_rotary_state_t;

static bool faculty175_rotary_state_get(faculty175_rotary_state_t *state)
{
    if (state != NULL) {
        memset(state, 0, sizeof(*state));
    }
    return false;
}

typedef struct {
    const char *name;
    uint8_t value;
    uint8_t max_value;
    uint8_t def_value;
} psych_state_style_opt_t;

static psych_state_style_opt_t s_style[] = {
    {"skin", PSYCH_STATE_DEFAULT_SKIN, PSYCH_STATE_SKIN_COUNT, PSYCH_STATE_DEFAULT_SKIN},
    {"hair", PSYCH_STATE_DEFAULT_HAIR, PSYCH_STATE_HAIR_COUNT, PSYCH_STATE_DEFAULT_HAIR},
    {"eyes", PSYCH_STATE_DEFAULT_EYE, PSYCH_STATE_EYE_COUNT, PSYCH_STATE_DEFAULT_EYE},
    {"beard", PSYCH_STATE_DEFAULT_BEARD, PSYCH_STATE_BEARD_COUNT, PSYCH_STATE_DEFAULT_BEARD},
    {"glasses", PSYCH_STATE_DEFAULT_GLASSES, PSYCH_STATE_GLASSES_COUNT, PSYCH_STATE_DEFAULT_GLASSES},
    {"nose", PSYCH_STATE_DEFAULT_NOSE, PSYCH_STATE_NOSE_COUNT, PSYCH_STATE_DEFAULT_NOSE},
    {"mouth", PSYCH_STATE_DEFAULT_MOUTH, PSYCH_STATE_MOUTH_COUNT, PSYCH_STATE_DEFAULT_MOUTH},
    {"arousal", PSYCH_STATE_DEFAULT_AROUSAL, PSYCH_STATE_EMOTION_COUNT, PSYCH_STATE_DEFAULT_AROUSAL},
    {"valence", PSYCH_STATE_DEFAULT_VALENCE, PSYCH_STATE_EMOTION_COUNT, PSYCH_STATE_DEFAULT_VALENCE},
};

static bool s_loaded;
static psych_state_style_field_t s_focus = PSYCH_STYLE_COUNT;
static uint8_t s_mood;

typedef struct {
    const char *label;
    uint8_t arousal;
    uint8_t valence;
} psych_state_mood_t;

static const psych_state_mood_t k_moods[PSYCH_STATE_MOOD_COUNT] = {
    {"CALM", 30, 65},
    {"BRIGHT", 65, 85},
    {"TENDER", 35, 45},
    {"LOW", 25, 20},
    {"TENSE", 80, 25},
    {"ENERGIZED", 90, 70},
};

static const char *k_skin_names[PSYCH_STATE_SKIN_COUNT] = {
    "Default",
    "Tan",
    "Light",
    "Brown",
    "Dark",
    "Olive",
};

static const char *k_hair_names[PSYCH_STATE_HAIR_COUNT] = {
    "Black",
    "Brown",
    "Blond",
    "Red",
    "Grey",
    "Pastel",
};

static const char *k_eye_names[PSYCH_STATE_EYE_COUNT] = {
    "Brown",
    "Blue",
    "Green",
    "Amber",
    "Gray",
    "Hazel",
};

static const char *k_beard_names[PSYCH_STATE_BEARD_COUNT] = {
    "None",
    "Stubble",
    "Mustache",
    "Full",
};

static const char *k_glasses_names[PSYCH_STATE_GLASSES_COUNT] = {
    "Off",
    "On",
};
static const char *k_nose_names[PSYCH_STATE_NOSE_COUNT] = {
    "Tiny",
    "Small",
    "Round",
    "Wide",
    "Long",
};
static const char *k_mouth_names[PSYCH_STATE_MOUTH_COUNT] = {
    "Closed",
    "Smile",
    "Frown",
    "Wide",
    "Pout",
    "Tongue",
};

static const int16_t k_emoji_cx = FACULTY175_LCD_W / 2;
static const int16_t k_emoji_cy = 210;
static const int16_t k_emoji_r = 104;
static const int16_t k_emoji_eye_y = k_emoji_cy - 22;
static const int16_t k_emoji_left_x = k_emoji_cx - 42;
static const int16_t k_emoji_right_x = k_emoji_cx + 42;
static const int16_t k_emoji_nose_y = k_emoji_cy + 4;
static const int16_t k_emoji_mouth_y = k_emoji_cy + 45;
static const int16_t k_emoji_beard_y = k_emoji_cy + 30;
static const int16_t k_eye_radius = 12;
static const int16_t k_skin_focus_margin = 9;
static uint8_t merged_style_value(psych_state_style_field_t field, const faculty175_rotary_state_t *state);

static uint16_t psych_color(uint8_t r, uint8_t g, uint8_t b)
{
    return faculty175_display_rgb888(r, g, b);
}

static uint16_t psych_color_skin(uint8_t idx)
{
    switch (idx) {
        case 0:
            return psych_color(244, 220, 196);
        case 1:
            return psych_color(237, 191, 148);
        case 2:
            return psych_color(232, 173, 108);
        case 3:
            return psych_color(214, 150, 95);
        case 4:
            return psych_color(167, 128, 90);
        case 5:
            return psych_color(102, 72, 44);
        default:
            return psych_color(214, 150, 95);
    }
}

static uint16_t psych_color_hair(uint8_t idx)
{
    switch (idx) {
        case 0:
            return psych_color(20, 18, 16);
        case 1:
            return psych_color(74, 47, 27);
        case 2:
            return psych_color(194, 146, 73);
        case 3:
            return psych_color(182, 98, 35);
        case 4:
            return psych_color(149, 144, 135);
        case 5:
            return psych_color(168, 90, 130);
        default:
            return psych_color(74, 47, 27);
    }
}

static uint16_t psych_color_eyes(uint8_t idx)
{
    switch (idx) {
        case 0:
            return psych_color(95, 65, 32);
        case 1:
            return psych_color(56, 108, 191);
        case 2:
            return psych_color(62, 143, 71);
        case 3:
            return psych_color(191, 125, 56);
        case 4:
            return psych_color(116, 112, 112);
        case 5:
            return psych_color(93, 61, 32);
        default:
            return psych_color(95, 65, 32);
    }
}

static void draw_focus_highlight(psych_state_style_field_t field)
{
    const uint16_t focus = psych_color(170, 232, 255);

    switch (field) {
        case PSYCH_STYLE_SKIN:
            faculty175_display_draw_circle(k_emoji_cx, k_emoji_cy, k_emoji_r + k_skin_focus_margin, focus);
            break;
        case PSYCH_STYLE_HAIR:
            faculty175_display_fill_circle(k_emoji_cx, k_emoji_cy - k_emoji_r + 8, k_emoji_r - 10, focus);
            faculty175_display_fill_rect(k_emoji_cx - k_emoji_r + 12, k_emoji_cy - k_emoji_r - 2, 2 * (k_emoji_r - 12), 18, psych_color(18, 22, 28));
            break;
        case PSYCH_STYLE_EYES:
            faculty175_display_fill_rect(k_emoji_left_x - 14, k_emoji_eye_y - 10, 28, 18, focus);
            faculty175_display_fill_rect(k_emoji_right_x - 14, k_emoji_eye_y - 10, 28, 18, focus);
            break;
        case PSYCH_STYLE_BEARD:
            faculty175_display_fill_rect(k_emoji_cx - 26, k_emoji_beard_y + 2, 52, 16, focus);
            break;
        case PSYCH_STYLE_GLASSES:
            faculty175_display_fill_rect(k_emoji_left_x - 18, k_emoji_eye_y - 14, 36, 14, focus);
            faculty175_display_fill_rect(k_emoji_right_x - 18, k_emoji_eye_y - 14, 36, 14, focus);
            break;
        case PSYCH_STYLE_NOSE:
            faculty175_display_fill_rect(k_emoji_cx - 8, k_emoji_nose_y - 2, 16, 10, focus);
            break;
        case PSYCH_STYLE_MOUTH:
            faculty175_display_fill_rect(k_emoji_cx - 20, k_emoji_mouth_y - 8, 40, 16, focus);
            break;
        case PSYCH_STYLE_AROUSAL:
            faculty175_display_fill_rect(k_emoji_cx - 28, k_emoji_cy - 40, 56, 28, focus);
            break;
        case PSYCH_STYLE_VALENCE:
            faculty175_display_fill_rect(k_emoji_cx - 2, k_emoji_cy - 20, 40, 40, focus);
            break;
        default:
            break;
    }
}

static void draw_memoji_face(const faculty175_rotary_state_t *state)
{
    const uint8_t skin = merged_style_value(PSYCH_STYLE_SKIN, state);
    const uint8_t hair = merged_style_value(PSYCH_STYLE_HAIR, state);
    const uint8_t eyes = merged_style_value(PSYCH_STYLE_EYES, state);
    const uint8_t beard = merged_style_value(PSYCH_STYLE_BEARD, state);
    const uint8_t glasses = merged_style_value(PSYCH_STYLE_GLASSES, state);
    const uint8_t nose = merged_style_value(PSYCH_STYLE_NOSE, state);
    const uint8_t mouth = merged_style_value(PSYCH_STYLE_MOUTH, state);
    const uint8_t arousal = merged_style_value(PSYCH_STYLE_AROUSAL, state);
    const uint8_t valence = merged_style_value(PSYCH_STYLE_VALENCE, state);
    const int16_t arousal_delta = (int16_t)arousal - 50;
    const int16_t valence_delta = (int16_t)valence - 50;

    const uint16_t skin_color = psych_color_skin(skin);
    const uint16_t hair_color = psych_color_hair(hair);
    const uint16_t eye_color = psych_color_eyes(eyes);
    const uint16_t eye_white = psych_color(248, 248, 248);
    const uint16_t lash = psych_color(16, 16, 16);
    const uint16_t ink = psych_color(18, 20, 22);
    const uint16_t beard_color = psych_color(36, 28, 26);
    const uint16_t blush = psych_color(237, 120, 132);

    faculty175_display_fill_circle(k_emoji_cx, k_emoji_cy, k_emoji_r, skin_color);
    faculty175_display_draw_circle(k_emoji_cx, k_emoji_cy, k_emoji_r, psych_color(98, 71, 52));
    if (hair > 0) {
        faculty175_display_fill_rect(k_emoji_cx - k_emoji_r + 2, k_emoji_cy - k_emoji_r - 1, 2 * k_emoji_r - 4, 34, hair_color);
        for (int16_t i = 0; i < 6; ++i) {
            const int16_t x = k_emoji_cx - 44 + (int16_t)i * 16;
            const int16_t y = k_emoji_cy - k_emoji_r + (int16_t)(i % 2) * 4;
            const int16_t rw = 8 + ((int16_t)i % 3);
            const int16_t rh = 18 + ((int16_t)i % 2) * 4;
            faculty175_display_fill_rect(x, y, rw, rh, hair_color);
        }
    }

    const int16_t eye_open = arousal_delta < -25 ? -2 : (arousal_delta > 25 ? 1 : 0);
    faculty175_display_fill_circle(k_emoji_left_x, k_emoji_eye_y, k_eye_radius + (int16_t)(arousal_delta > 30 ? 1 : 0), eye_white);
    faculty175_display_fill_circle(k_emoji_right_x, k_emoji_eye_y, k_eye_radius + (int16_t)(arousal_delta > 30 ? 1 : 0), eye_white);
    faculty175_display_fill_circle(k_emoji_left_x - 3 + (int16_t)(eyes % 3), k_emoji_eye_y - 2 + eye_open, 3, eye_color);
    faculty175_display_fill_circle(k_emoji_right_x - 3 + (int16_t)(eyes % 3), k_emoji_eye_y - 2 + eye_open, 3, eye_color);
    faculty175_display_fill_rect(k_emoji_left_x - 3, k_emoji_eye_y - 10, 16, 2, lash);
    faculty175_display_fill_rect(k_emoji_right_x - 3, k_emoji_eye_y - 10, 16, 2, lash);
    faculty175_display_draw_text("*", k_emoji_left_x - 1, k_emoji_eye_y - 1, ink);
    faculty175_display_draw_text("*", k_emoji_right_x - 1, k_emoji_eye_y - 1, ink);

    if (valence_delta > 20) {
        const uint16_t blush = psych_color(237, 120, 132);
        faculty175_display_fill_rect(k_emoji_cx - 58, k_emoji_cy - 22, 12, 8, blush);
        faculty175_display_fill_rect(k_emoji_cx + 46, k_emoji_cy - 22, 12, 8, blush);
    }

    if (glasses > 0) {
        faculty175_display_fill_rect(k_emoji_left_x - 14, k_emoji_eye_y - 11, 28, 10, psych_color(220, 226, 232));
        faculty175_display_fill_rect(k_emoji_right_x - 14, k_emoji_eye_y - 11, 28, 10, psych_color(220, 226, 232));
        faculty175_display_draw_line(k_emoji_left_x + 14, k_emoji_eye_y - 6, k_emoji_right_x - 14, k_emoji_eye_y - 6, psych_color(10, 10, 10));
    }

    if (valence_delta > 22) {
        faculty175_display_fill_rect(k_emoji_left_x - 20, k_emoji_eye_y - 12, 10, 5, ink);
        faculty175_display_fill_rect(k_emoji_right_x + 10, k_emoji_eye_y - 12, 10, 5, ink);
    } else if (valence_delta < -22) {
        faculty175_display_fill_rect(k_emoji_left_x - 3, k_emoji_eye_y + 2, 3, 3, ink);
        faculty175_display_fill_rect(k_emoji_right_x - 3, k_emoji_eye_y + 2, 3, 3, ink);
    }

    const int16_t nose_y = k_emoji_cy + 2;
    if (nose >= 1) {
        faculty175_display_draw_line(k_emoji_cx,
                                    k_emoji_nose_y - 1,
                                    k_emoji_cx - 8 + (int16_t)(nose % 3),
                                    k_emoji_nose_y + 10 + (int16_t)(nose / 2),
                                    ink);
        faculty175_display_draw_line(k_emoji_cx,
                                    k_emoji_nose_y - 1,
                                    k_emoji_cx + 8 - (int16_t)(nose % 3),
                                    k_emoji_nose_y + 10 + (int16_t)(nose / 2),
                                    ink);
        faculty175_display_fill_rect(k_emoji_cx - 2, k_emoji_nose_y + 2, 5, 2, ink);
    }
    if (nose > 3) {
        faculty175_display_draw_line(k_emoji_cx - 10, k_emoji_nose_y + 5, k_emoji_cx + 10, k_emoji_nose_y + 5, ink);
    }

    const uint8_t mapped_mouth =
        arousal_delta > 28 && valence_delta > 10 ? 5 :
        arousal_delta > 10 && valence_delta >= -10 ? 1 :
        valence_delta >= 30 ? 3 :
        valence_delta >= 8 ? 0 :
        valence_delta <= -30 ? 2 :
        valence_delta <= -8 ? 4 :
        mouth;

    if (mapped_mouth == 0) {
        faculty175_display_draw_line(k_emoji_cx - 18, k_emoji_mouth_y, k_emoji_cx + 18, k_emoji_mouth_y, ink);
    } else if (mapped_mouth == 1 || mapped_mouth == 5) {
        faculty175_display_draw_line(k_emoji_cx - 18, k_emoji_mouth_y + 3, k_emoji_cx + 18, k_emoji_mouth_y + 3, ink);
        faculty175_display_draw_line(k_emoji_cx - 18, k_emoji_mouth_y + 3, k_emoji_cx - 14, k_emoji_mouth_y + 7, ink);
        faculty175_display_draw_line(k_emoji_cx + 18, k_emoji_mouth_y + 3, k_emoji_cx + 14, k_emoji_mouth_y + 7, ink);
    } else if (mapped_mouth == 2 || mapped_mouth == 6) {
        faculty175_display_draw_line(k_emoji_cx - 18, k_emoji_mouth_y + 6, k_emoji_cx + 18, k_emoji_mouth_y + 6, ink);
        faculty175_display_draw_line(k_emoji_cx - 18, k_emoji_mouth_y + 6, k_emoji_cx - 14, k_emoji_mouth_y + 2, ink);
        faculty175_display_draw_line(k_emoji_cx + 18, k_emoji_mouth_y + 6, k_emoji_cx + 14, k_emoji_mouth_y + 2, ink);
    } else if (mapped_mouth == 3) {
        faculty175_display_draw_line(k_emoji_cx - 18, k_emoji_mouth_y + 4, k_emoji_cx + 18, k_emoji_mouth_y + 4, ink);
        for (int i = -12; i <= 12; i += 6) {
            faculty175_display_fill_rect(k_emoji_cx + i, k_emoji_mouth_y + 2, 4, 4, ink);
        }
    } else if (mapped_mouth == 4) {
        faculty175_display_draw_line(k_emoji_cx - 16, k_emoji_mouth_y, k_emoji_cx - 8, k_emoji_mouth_y + 4, ink);
        faculty175_display_fill_rect(k_emoji_cx - 8, k_emoji_mouth_y + 2, 16, 2, ink);
        faculty175_display_draw_line(k_emoji_cx + 8, k_emoji_mouth_y, k_emoji_cx + 16, k_emoji_mouth_y + 4, ink);
    }

    if (beard > 0) {
        if (beard == 1) {
            faculty175_display_draw_line(k_emoji_cx - 8, k_emoji_beard_y + 4, k_emoji_cx + 8, k_emoji_beard_y + 4, beard_color);
        } else if (beard == 2) {
            faculty175_display_fill_rect(k_emoji_cx - 12, k_emoji_beard_y, 24, 10, beard_color);
        } else if (beard == 3) {
            faculty175_display_fill_rect(k_emoji_cx - 16, k_emoji_beard_y, 32, 12, beard_color);
            faculty175_display_fill_rect(k_emoji_cx - 22, k_emoji_beard_y + 4, 44, 8, beard_color);
        }
    }

    if (s_focus < PSYCH_STYLE_COUNT) {
        draw_focus_highlight(s_focus);
    }
}

static void normalize_field(uint8_t field_idx, uint8_t *value)
{
    if (value == NULL || field_idx >= PSYCH_STYLE_COUNT) {
        return;
    }
    if (*value >= s_style[field_idx].max_value) {
        *value = s_style[field_idx].def_value;
    }
}

static uint8_t cycle_value(uint8_t value, int delta, uint8_t max)
{
    if (max == 0) {
        return 0;
    }
    const int base = (int)value;
    int next = base + delta;
    if (next < 0) {
        next = (int)max - 1;
    } else if (next >= (int)max) {
        next = 0;
    }
    return (uint8_t)next;
}

static void save_style_style(void)
{
    nvs_handle_t nvs;
    if (nvs_open(PSYCH_STATE_NVS_NS, NVS_READWRITE, &nvs) != ESP_OK) {
        return;
    }
    (void)nvs_set_u8(nvs, PSYCH_STATE_NVS_SKIN, s_style[PSYCH_STYLE_SKIN].value);
    (void)nvs_set_u8(nvs, PSYCH_STATE_NVS_HAIR, s_style[PSYCH_STYLE_HAIR].value);
    (void)nvs_set_u8(nvs, PSYCH_STATE_NVS_EYES, s_style[PSYCH_STYLE_EYES].value);
    (void)nvs_set_u8(nvs, PSYCH_STATE_NVS_BEARD, s_style[PSYCH_STYLE_BEARD].value);
    (void)nvs_set_u8(nvs, PSYCH_STATE_NVS_GLASSES, s_style[PSYCH_STYLE_GLASSES].value);
    (void)nvs_set_u8(nvs, PSYCH_STATE_NVS_NOSE, s_style[PSYCH_STYLE_NOSE].value);
    (void)nvs_set_u8(nvs, PSYCH_STATE_NVS_MOUTH, s_style[PSYCH_STYLE_MOUTH].value);
    (void)nvs_set_u8(nvs, PSYCH_STATE_NVS_AROUSAL, s_style[PSYCH_STYLE_AROUSAL].value);
    (void)nvs_set_u8(nvs, PSYCH_STATE_NVS_VALENCE, s_style[PSYCH_STYLE_VALENCE].value);
    (void)nvs_set_u8(nvs, PSYCH_STATE_NVS_MOOD, s_mood);
    (void)nvs_commit(nvs);
    nvs_close(nvs);
}

static void ensure_style_loaded(void)
{
    if (s_loaded) {
        return;
    }
    s_loaded = true;
    nvs_handle_t nvs;
    if (nvs_open(PSYCH_STATE_NVS_NS, NVS_READONLY, &nvs) != ESP_OK) {
        return;
    }

    nvs_get_u8(nvs, PSYCH_STATE_NVS_SKIN, &s_style[PSYCH_STYLE_SKIN].value);
    nvs_get_u8(nvs, PSYCH_STATE_NVS_HAIR, &s_style[PSYCH_STYLE_HAIR].value);
    nvs_get_u8(nvs, PSYCH_STATE_NVS_EYES, &s_style[PSYCH_STYLE_EYES].value);
    nvs_get_u8(nvs, PSYCH_STATE_NVS_BEARD, &s_style[PSYCH_STYLE_BEARD].value);
    nvs_get_u8(nvs, PSYCH_STATE_NVS_GLASSES, &s_style[PSYCH_STYLE_GLASSES].value);
    nvs_get_u8(nvs, PSYCH_STATE_NVS_NOSE, &s_style[PSYCH_STYLE_NOSE].value);
    nvs_get_u8(nvs, PSYCH_STATE_NVS_MOUTH, &s_style[PSYCH_STYLE_MOUTH].value);
    nvs_get_u8(nvs, PSYCH_STATE_NVS_AROUSAL, &s_style[PSYCH_STYLE_AROUSAL].value);
    nvs_get_u8(nvs, PSYCH_STATE_NVS_VALENCE, &s_style[PSYCH_STYLE_VALENCE].value);
    nvs_get_u8(nvs, PSYCH_STATE_NVS_MOOD, &s_mood);
    nvs_close(nvs);

    normalize_field(PSYCH_STYLE_SKIN, &s_style[PSYCH_STYLE_SKIN].value);
    normalize_field(PSYCH_STYLE_HAIR, &s_style[PSYCH_STYLE_HAIR].value);
    normalize_field(PSYCH_STYLE_EYES, &s_style[PSYCH_STYLE_EYES].value);
    normalize_field(PSYCH_STYLE_BEARD, &s_style[PSYCH_STYLE_BEARD].value);
    normalize_field(PSYCH_STYLE_GLASSES, &s_style[PSYCH_STYLE_GLASSES].value);
    normalize_field(PSYCH_STYLE_NOSE, &s_style[PSYCH_STYLE_NOSE].value);
    normalize_field(PSYCH_STYLE_MOUTH, &s_style[PSYCH_STYLE_MOUTH].value);
    normalize_field(PSYCH_STYLE_AROUSAL, &s_style[PSYCH_STYLE_AROUSAL].value);
    normalize_field(PSYCH_STYLE_VALENCE, &s_style[PSYCH_STYLE_VALENCE].value);
    if (s_mood >= PSYCH_STATE_MOOD_COUNT) {
        s_mood = 0;
    }
}

static const char *style_name_for(psych_state_style_field_t field, uint8_t value)
{
    switch (field) {
        case PSYCH_STYLE_SKIN:
            return k_skin_names[value < PSYCH_STATE_SKIN_COUNT ? value : PSYCH_STATE_DEFAULT_SKIN];
        case PSYCH_STYLE_HAIR:
            return k_hair_names[value < PSYCH_STATE_HAIR_COUNT ? value : PSYCH_STATE_DEFAULT_HAIR];
        case PSYCH_STYLE_EYES:
            return k_eye_names[value < PSYCH_STATE_EYE_COUNT ? value : PSYCH_STATE_DEFAULT_EYE];
        case PSYCH_STYLE_BEARD:
            return k_beard_names[value < PSYCH_STATE_BEARD_COUNT ? value : PSYCH_STATE_DEFAULT_BEARD];
        case PSYCH_STYLE_GLASSES:
            return k_glasses_names[value < PSYCH_STATE_GLASSES_COUNT ? value : PSYCH_STATE_DEFAULT_GLASSES];
        case PSYCH_STYLE_NOSE:
            return k_nose_names[value < PSYCH_STATE_NOSE_COUNT ? value : PSYCH_STATE_DEFAULT_NOSE];
        case PSYCH_STYLE_MOUTH:
            return k_mouth_names[value < PSYCH_STATE_MOUTH_COUNT ? value : PSYCH_STATE_DEFAULT_MOUTH];
        case PSYCH_STYLE_AROUSAL:
            return "arousal";
        case PSYCH_STYLE_VALENCE:
            return "valence";
        default:
            return "-";
    }
}

static uint8_t style_count_for(psych_state_style_field_t field)
{
    switch (field) {
        case PSYCH_STYLE_SKIN:
            return PSYCH_STATE_SKIN_COUNT;
        case PSYCH_STYLE_HAIR:
            return PSYCH_STATE_HAIR_COUNT;
        case PSYCH_STYLE_EYES:
            return PSYCH_STATE_EYE_COUNT;
        case PSYCH_STYLE_BEARD:
            return PSYCH_STATE_BEARD_COUNT;
        case PSYCH_STYLE_GLASSES:
            return PSYCH_STATE_GLASSES_COUNT;
        case PSYCH_STYLE_NOSE:
            return PSYCH_STATE_NOSE_COUNT;
        case PSYCH_STYLE_MOUTH:
            return PSYCH_STATE_MOUTH_COUNT;
        case PSYCH_STYLE_AROUSAL:
            return PSYCH_STATE_EMOTION_COUNT;
        case PSYCH_STYLE_VALENCE:
            return PSYCH_STATE_EMOTION_COUNT;
        default:
            return 1;
    }
}

static uint8_t local_style_value(psych_state_style_field_t field)
{
    return s_style[field].value;
}

static uint8_t merged_style_value(psych_state_style_field_t field, const faculty175_rotary_state_t *state)
{
    const bool has_remote = state != NULL && state->valid && state->has_style;
    if (!has_remote) {
        return local_style_value(field);
    }

    switch (field) {
        case PSYCH_STYLE_SKIN:
            return state->skin_tone;
        case PSYCH_STYLE_HAIR:
            return state->hair_color;
        case PSYCH_STYLE_EYES:
            return state->eye_color;
        case PSYCH_STYLE_BEARD:
            return state->facial_hair;
        case PSYCH_STYLE_GLASSES:
            return state->glasses;
        default:
            return local_style_value(field);
    }
}

static bool psych_state_style_hit(int16_t x, int16_t y, psych_state_style_field_t *out_field)
{
    if (out_field == NULL) {
        return false;
    }
    const int16_t dx = x - k_emoji_cx;
    const int16_t dy = y - k_emoji_cy;
    const int32_t dist2 = (int32_t)dx * (int32_t)dx + (int32_t)dy * (int32_t)dy;
    const int32_t max_hit2 = ((int32_t)k_emoji_r + 12) * ((int32_t)k_emoji_r + 12);
    if (dist2 > max_hit2) {
        return false;
    }

    if (dy <= -k_emoji_r + 12) {
        *out_field = PSYCH_STYLE_HAIR;
        return true;
    }
    if (dy < -24) {
        if (x < k_emoji_cx) {
            *out_field = PSYCH_STYLE_AROUSAL;
        } else {
            *out_field = PSYCH_STYLE_VALENCE;
        }
        return true;
    }
    if (abs(dy - (-12)) <= 10) {
        if (abs(dx) < 12) {
            *out_field = PSYCH_STYLE_NOSE;
            return true;
        }
        if (abs(dx) > 18) {
            *out_field = PSYCH_STYLE_GLASSES;
            return true;
        }
        *out_field = PSYCH_STYLE_EYES;
        return true;
    }
    if (abs(dy - 2) <= 10) {
        *out_field = PSYCH_STYLE_NOSE;
        return true;
    }
    if (abs(dy - 18) <= 12) {
        *out_field = PSYCH_STYLE_MOUTH;
        return true;
    }
    if (dy > 22) {
        *out_field = PSYCH_STYLE_BEARD;
        return true;
    }

    if (abs(dx) <= 10 && abs(dy) <= 20) {
        *out_field = PSYCH_STYLE_NOSE;
        return true;
    }

    *out_field = PSYCH_STYLE_SKIN;
    return true;
}

void faculty175_face_psych_state_draw(uint32_t anim_ms)
{
    (void)anim_ms;
    faculty175_rotary_state_t state = {};
    ensure_style_loaded();
    const bool has_state = faculty175_rotary_state_get(&state) && state.valid;
    const bool has_remote_style = has_state && state.has_style;
    const uint16_t background = has_remote_style ? psych_color(7, 10, 18) : psych_color(10, 12, 16);
    const uint16_t ambient = has_remote_style ? psych_color(14, 34, 54) : psych_color(18, 22, 28);

    faculty175_display_fill_rgb565(background);
    for (int16_t y = 0; y < FACULTY175_LCD_H; ++y) {
        const int16_t blend = (int16_t)(y * 10 / 480);
        const uint16_t strip = psych_color((background & 0xF800U) == 0 ? 0 : (uint8_t)blend,
                                          (background & 0x7E0U) == 0 ? 0 : (uint8_t)(20 + blend),
                                          (background & 0x1FU) == 0 ? 0 : (uint8_t)(28 + blend));
        faculty175_display_fill_rect(0, y, FACULTY175_LCD_W, 1, ambient);
        (void)strip;
    }

    s_style[PSYCH_STYLE_AROUSAL].value = k_moods[s_mood].arousal;
    s_style[PSYCH_STYLE_VALENCE].value = k_moods[s_mood].valence;
    draw_memoji_face(&state);
    faculty175_display_draw_text("HOW ARE YOU?", 192, 54, psych_color(224, 232, 246));
    faculty175_display_draw_text("<", 74, 410, psych_color(145, 172, 206));
    faculty175_display_draw_text(k_moods[s_mood].label, 205, 410, psych_color(224, 232, 246));
    faculty175_display_draw_text(">", 397, 410, psych_color(145, 172, 206));

    faculty175_display_flush();
}

bool faculty175_face_psych_state_action(uint32_t seed_ms)
{
    (void)seed_ms;
    ensure_style_loaded();
    s_mood = cycle_value(s_mood, 1, PSYCH_STATE_MOOD_COUNT);
    save_style_style();
    return true;
}

bool faculty175_face_psych_state_tap(int16_t x, int16_t y)
{
    (void)y;
    ensure_style_loaded();
    if (x < 0 || x >= FACULTY175_LCD_W) {
        return false;
    }
    s_mood = cycle_value(s_mood, x < FACULTY175_LCD_W / 2 ? -1 : 1, PSYCH_STATE_MOOD_COUNT);
    save_style_style();
    return true;
}

bool faculty175_face_psych_state_style_delta(int delta)
{
    ensure_style_loaded();
    if (delta == 0) {
        return false;
    }
    s_mood = cycle_value(s_mood, delta, PSYCH_STATE_MOOD_COUNT);
    save_style_style();
    return true;
}

const char *faculty175_face_psych_state_mood_label(void)
{
    ensure_style_loaded();
    return k_moods[s_mood].label;
}

void faculty175_face_psych_state_mood_values(uint8_t *arousal, uint8_t *valence)
{
    ensure_style_loaded();
    if (arousal != NULL) {
        *arousal = k_moods[s_mood].arousal;
    }
    if (valence != NULL) {
        *valence = k_moods[s_mood].valence;
    }
}
