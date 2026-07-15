#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "astrolabe_time.h"
#include "faculty175_board.h"
#include "faculty175_charts.h"
#include "faculty175_ephemeris.h"

enum {
    HD_CX = FACULTY175_LCD_W / 2,
    HD_CY = FACULTY175_LCD_H / 2,
};

#define HD_QUARTER_START_DEG (-180.0f)
#define HD_ZODIAC_START_DEG (-137.5f)
#define HD_RAVE_START_DEGREE 358.25f
#define HD_PIE_OUTER_R 164
#define HD_LINE_OUTER_R 176
#define HD_HEX_OUTER_R 190
#define HD_ZODIAC_OUTER_R 206
#define HD_QUARTER_OUTER_R 224
#define HD_BODY_SCALE 68
#define HD_LINE_MID_R ((HD_PIE_OUTER_R + HD_LINE_OUTER_R) / 2)
#define HD_TRANSIT_BODY_COUNT FACULTY175_HD_BODY_COUNT

typedef struct {
    const char *label;
    int x;
    int y;
    uint8_t r;
    uint8_t g;
    uint8_t b;
    bool defined;
} hd_center_t;

typedef struct {
    int x;
    int y;
} hd_point_t;

typedef struct {
    int center_a;
    int center_b;
    uint8_t gate_a;
    uint8_t gate_b;
    const char *gates;
    const char *circuit;
} hd_channel_t;

typedef struct {
    const char *body;
    uint8_t gate;
    uint8_t line;
} hd_gate_t;

typedef struct {
    uint8_t gate;
    bool design;
    bool personality;
} hd_gate_state_t;

typedef enum {
    HD_MODE_TRANSIT = 0,
    HD_MODE_NATAL,
    HD_MODE_CONNECTION,
} hd_mode_t;

typedef enum {
    HD_REL_NONE = 0,
    HD_REL_DOMINANCE_A,
    HD_REL_DOMINANCE_B,
    HD_REL_COMPROMISE,
    HD_REL_COMPANIONSHIP,
    HD_REL_ELECTROMAGNETIC,
} hd_relationship_t;

typedef struct {
    bool connection;
    bool ok;
    hd_gate_t person_a[HD_TRANSIT_BODY_COUNT * 2];
    hd_gate_t person_b[HD_TRANSIT_BODY_COUNT * 2];
    size_t person_a_count;
    size_t person_b_count;
    hd_relationship_t relationships[36];
} hd_relationship_ctx_t;

static float mandala_gate_start_deg(int slot);

typedef struct {
    uint16_t bg;
    uint16_t text;
    uint16_t subtext;
    uint16_t dim;
    uint16_t ring;
    uint16_t inner_ring;
    uint16_t accent;
    uint16_t design;
    uint16_t personality;
    uint16_t center_defined_mix;
    uint16_t center_undefined;
    uint16_t center_rim;
    uint16_t center_text;
    uint16_t channel_dark;
    uint16_t gate_fill;
    uint16_t gate_border;
    uint16_t gate_text;
} hd_palette_t;

typedef enum {
    HD_CENTER_HEAD = 0,
    HD_CENTER_AJNA,
    HD_CENTER_THROAT,
    HD_CENTER_G,
    HD_CENTER_EGO,
    HD_CENTER_SOLAR,
    HD_CENTER_SPLEEN,
    HD_CENTER_SACRAL,
    HD_CENTER_ROOT,
    HD_CENTER_COUNT,
} hd_center_index_t;

static const hd_center_t k_centers[HD_CENTER_COUNT] = {
    {"HEAD", 233, 80, 186, 152, 226, false},
    {"AJNA", 233, 121, 160, 186, 232, false},
    {"THROAT", 233, 168, 104, 196, 224, true},
    {"G", 233, 226, 236, 188, 86, true},
    {"EGO", 286, 242, 214, 128, 92, false},
    {"SP", 294, 310, 198, 102, 184, false},
    {"SPL", 172, 310, 94, 188, 140, true},
    {"SAC", 233, 318, 226, 158, 74, true},
    {"ROOT", 233, 369, 206, 86, 86, true},
};

static const hd_point_t k_body_centers[HD_CENTER_COUNT] = {
    {0, -138},
    {0, -84},
    {0, -28},
    {0, 28},
    {54, 46},
    {104, 92},
    {-104, 92},
    {0, 92},
    {0, 142},
};

static const hd_channel_t k_body_channels[] = {
    {HD_CENTER_HEAD, HD_CENTER_AJNA, 64, 47, "64-47", "abstract"},
    {HD_CENTER_HEAD, HD_CENTER_AJNA, 61, 24, "61-24", "knowing"},
    {HD_CENTER_HEAD, HD_CENTER_AJNA, 63, 4, "63-4", "logic"},
    {HD_CENTER_AJNA, HD_CENTER_THROAT, 43, 23, "43-23", "knowing"},
    {HD_CENTER_AJNA, HD_CENTER_THROAT, 17, 62, "17-62", "logic"},
    {HD_CENTER_AJNA, HD_CENTER_THROAT, 11, 56, "11-56", "abstract"},
    {HD_CENTER_THROAT, HD_CENTER_G, 1, 8, "1-8", "knowing"},
    {HD_CENTER_THROAT, HD_CENTER_G, 7, 31, "7-31", "logic"},
    {HD_CENTER_THROAT, HD_CENTER_G, 13, 33, "13-33", "abstract"},
    {HD_CENTER_THROAT, HD_CENTER_G, 10, 20, "10-20", "integration"},
    {HD_CENTER_THROAT, HD_CENTER_EGO, 21, 45, "21-45", "ego"},
    {HD_CENTER_THROAT, HD_CENTER_SOLAR, 12, 22, "12-22", "knowing"},
    {HD_CENTER_THROAT, HD_CENTER_SOLAR, 35, 36, "35-36", "abstract"},
    {HD_CENTER_THROAT, HD_CENTER_SPLEEN, 16, 48, "16-48", "logic"},
    {HD_CENTER_THROAT, HD_CENTER_SPLEEN, 20, 57, "20-57", "integration"},
    {HD_CENTER_THROAT, HD_CENTER_SACRAL, 20, 34, "20-34", "integration"},
    {HD_CENTER_G, HD_CENTER_SACRAL, 10, 34, "10-34", "integration"},
    {HD_CENTER_G, HD_CENTER_SACRAL, 2, 14, "2-14", "knowing"},
    {HD_CENTER_G, HD_CENTER_SACRAL, 5, 15, "5-15", "logic"},
    {HD_CENTER_G, HD_CENTER_SACRAL, 29, 46, "29-46", "abstract"},
    {HD_CENTER_G, HD_CENTER_SPLEEN, 57, 10, "57-10", "integration"},
    {HD_CENTER_G, HD_CENTER_EGO, 25, 51, "25-51", "centering"},
    {HD_CENTER_EGO, HD_CENTER_SOLAR, 37, 40, "37-40", "ego"},
    {HD_CENTER_EGO, HD_CENTER_SPLEEN, 26, 44, "26-44", "ego"},
    {HD_CENTER_SACRAL, HD_CENTER_ROOT, 3, 60, "3-60", "knowing"},
    {HD_CENTER_SACRAL, HD_CENTER_ROOT, 9, 52, "9-52", "logic"},
    {HD_CENTER_SACRAL, HD_CENTER_ROOT, 42, 53, "42-53", "abstract"},
    {HD_CENTER_SACRAL, HD_CENTER_SPLEEN, 34, 57, "34-57", "integration"},
    {HD_CENTER_SACRAL, HD_CENTER_SPLEEN, 27, 50, "27-50", "defense"},
    {HD_CENTER_SACRAL, HD_CENTER_SOLAR, 59, 6, "59-6", "defense"},
    {HD_CENTER_ROOT, HD_CENTER_SPLEEN, 58, 18, "58-18", "logic"},
    {HD_CENTER_ROOT, HD_CENTER_SPLEEN, 38, 28, "38-28", "knowing"},
    {HD_CENTER_ROOT, HD_CENTER_SPLEEN, 54, 32, "54-32", "ego"},
    {HD_CENTER_ROOT, HD_CENTER_SOLAR, 41, 30, "41-30", "abstract"},
    {HD_CENTER_ROOT, HD_CENTER_SOLAR, 39, 55, "39-55", "knowing"},
    {HD_CENTER_ROOT, HD_CENTER_SOLAR, 19, 49, "19-49", "ego"},
};

static const uint8_t k_mandala_gate_order[64] = {
    25, 17, 21, 51, 42, 3, 27, 24, 2, 23, 8, 20, 16, 35, 45, 12,
    15, 52, 39, 53, 62, 56, 31, 33, 7, 4, 29, 59, 40, 64, 47, 6,
    46, 18, 48, 57, 32, 50, 28, 44, 1, 43, 14, 34, 9, 5, 26, 11,
    10, 58, 38, 54, 61, 60, 41, 19, 13, 49, 30, 55, 37, 63, 22, 36,
};

static hd_mode_t s_hd_mode = HD_MODE_TRANSIT;
static hd_relationship_ctx_t s_hd_relationship;

static uint16_t c(uint8_t r, uint8_t g, uint8_t b)
{
    return faculty175_display_rgb888(r, g, b);
}

static uint16_t blend565(uint16_t bg, uint16_t fg, float alpha)
{
    if (alpha <= 0.0f) {
        return bg;
    }
    if (alpha >= 1.0f) {
        return fg;
    }
    const uint8_t br = (uint8_t)(((bg >> 11) & 0x1f) * 255 / 31);
    const uint8_t bg_g = (uint8_t)(((bg >> 5) & 0x3f) * 255 / 63);
    const uint8_t bb = (uint8_t)((bg & 0x1f) * 255 / 31);
    const uint8_t fr = (uint8_t)(((fg >> 11) & 0x1f) * 255 / 31);
    const uint8_t fg_g = (uint8_t)(((fg >> 5) & 0x3f) * 255 / 63);
    const uint8_t fb = (uint8_t)((fg & 0x1f) * 255 / 31);
    const float ia = 1.0f - alpha;
    return c((uint8_t)(br * ia + fr * alpha),
             (uint8_t)(bg_g * ia + fg_g * alpha),
             (uint8_t)(bb * ia + fb * alpha));
}

static float wrap360f(float v)
{
    while (v < 0.0f) {
        v += 360.0f;
    }
    while (v >= 360.0f) {
        v -= 360.0f;
    }
    return v;
}

static float hd_days_since_j2000(uint32_t anim_ms)
{
    time_t now = astrolabe_time_valid() ? astrolabe_time_now() : time(NULL);
    if (now > 946728000) {
        return (float)((double)(now - 946728000) / 86400.0);
    }
    return 9400.0f + (float)(anim_ms % 86400000u) / 86400000.0f;
}

static void hd_gate_line_from_lon(float lon_deg, uint8_t *gate, uint8_t *line)
{
    const float step = 360.0f / 64.0f;
    const float adjusted = wrap360f(wrap360f(lon_deg) - HD_RAVE_START_DEGREE);
    int slot = (int)floorf(adjusted / step);
    if (slot < 0) {
        slot = 0;
    } else if (slot > 63) {
        slot = 63;
    }
    int line_idx = (int)floorf((adjusted - (float)slot * step) / (step / 6.0f));
    if (line_idx < 0) {
        line_idx = 0;
    } else if (line_idx > 5) {
        line_idx = 5;
    }
    if (gate != NULL) {
        *gate = k_mandala_gate_order[slot];
    }
    if (line != NULL) {
        *line = (uint8_t)(line_idx + 1);
    }
}

static size_t build_ephemeris_gates(time_t epoch, hd_gate_t *out, size_t cap)
{
    if (out == NULL || cap == 0) {
        return 0;
    }
    faculty175_hd_positions_t pos = {};
    if (!faculty175_ephemeris_fetch_human_design_epoch(epoch, &pos) || !pos.ok) {
        return 0;
    }
    size_t count = 0;
    for (int i = 0; i < FACULTY175_HD_BODY_COUNT && count < cap; ++i) {
        out[count].body = faculty175_ephemeris_hd_body_label((faculty175_hd_body_t)i);
        hd_gate_line_from_lon((float)pos.lon[i], &out[count].gate, &out[count].line);
        ++count;
    }
    return count;
}

static size_t build_realtime_transits(uint32_t anim_ms, hd_gate_t *out, size_t cap)
{
    const time_t now = astrolabe_time_valid() ? astrolabe_time_now() :
                       (time_t)(946728000 + (int64_t)hd_days_since_j2000(anim_ms) * 86400);
    return build_ephemeris_gates(now, out, cap);
}

static bool build_natal_gates(hd_gate_t *personality,
                              size_t personality_cap,
                              size_t *personality_count,
                              hd_gate_t *design,
                              size_t design_cap,
                              size_t *design_count)
{
    if (personality_count != NULL) {
        *personality_count = 0;
    }
    if (design_count != NULL) {
        *design_count = 0;
    }
    faculty175_birth_chart_t birth = {};
    time_t birth_epoch = 0;
    if (!faculty175_charts_active(&birth) || !faculty175_charts_birth_to_utc(&birth, &birth_epoch)) {
        return false;
    }
    const size_t p_count = build_ephemeris_gates(birth_epoch, personality, personality_cap);
    const time_t design_epoch = birth_epoch - (time_t)(88 * 86400);
    const size_t d_count = build_ephemeris_gates(design_epoch, design, design_cap);
    if (personality_count != NULL) {
        *personality_count = p_count;
    }
    if (design_count != NULL) {
        *design_count = d_count;
    }
    return p_count > 0;
}

static bool build_chart_gates(const faculty175_birth_chart_t *birth,
                              hd_gate_t *out,
                              size_t cap,
                              size_t *out_count)
{
    if (out_count != NULL) {
        *out_count = 0;
    }
    if (birth == NULL || out == NULL || cap == 0) {
        return false;
    }
    time_t birth_epoch = 0;
    if (!faculty175_charts_birth_to_utc(birth, &birth_epoch)) {
        return false;
    }
    size_t count = build_ephemeris_gates(birth_epoch, out, cap);
    if (count < cap) {
        count += build_ephemeris_gates(birth_epoch - (time_t)(88 * 86400), out + count, cap - count);
    }
    if (out_count != NULL) {
        *out_count = count;
    }
    return count > 0;
}

static bool gate_present(const hd_gate_t *gates, size_t count, uint8_t gate)
{
    for (size_t i = 0; i < count; ++i) {
        if (gates[i].gate == gate) {
            return true;
        }
    }
    return false;
}

static hd_relationship_t classify_relationship_channel(const hd_channel_t *channel,
                                                        const hd_gate_t *a,
                                                        size_t a_count,
                                                        const hd_gate_t *b,
                                                        size_t b_count)
{
    if (channel == NULL) {
        return HD_REL_NONE;
    }
    const bool a0 = gate_present(a, a_count, channel->gate_a);
    const bool a1 = gate_present(a, a_count, channel->gate_b);
    const bool b0 = gate_present(b, b_count, channel->gate_a);
    const bool b1 = gate_present(b, b_count, channel->gate_b);
    const bool a_full = a0 && a1;
    const bool b_full = b0 && b1;
    const bool a_half = a0 != a1;
    const bool b_half = b0 != b1;

    if (a_full && b_full) {
        return HD_REL_COMPANIONSHIP;
    }
    if ((a0 && b1 && !a1 && !b0) || (a1 && b0 && !a0 && !b1)) {
        return HD_REL_ELECTROMAGNETIC;
    }
    if ((a_full && b_half) || (b_full && a_half)) {
        return HD_REL_COMPROMISE;
    }
    if (a_full && !b0 && !b1) {
        return HD_REL_DOMINANCE_A;
    }
    if (b_full && !a0 && !a1) {
        return HD_REL_DOMINANCE_B;
    }
    return HD_REL_NONE;
}

static bool build_connection_context(hd_relationship_ctx_t *ctx)
{
    if (ctx == NULL) {
        return false;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->connection = true;
    faculty175_charts_ensure_family_seed();
    faculty175_birth_chart_t primary = {};
    faculty175_birth_chart_t target = {};
    if (!faculty175_charts_primary(&primary) || !faculty175_charts_active(&target)) {
        return false;
    }
    const bool primary_ok = build_chart_gates(&primary,
                                              ctx->person_a,
                                              sizeof(ctx->person_a) / sizeof(ctx->person_a[0]),
                                              &ctx->person_a_count);
    const bool target_ok = build_chart_gates(&target,
                                             ctx->person_b,
                                             sizeof(ctx->person_b) / sizeof(ctx->person_b[0]),
                                             &ctx->person_b_count);
    if (!primary_ok || !target_ok) {
        return false;
    }
    for (size_t i = 0; i < sizeof(k_body_channels) / sizeof(k_body_channels[0]); ++i) {
        ctx->relationships[i] = classify_relationship_channel(&k_body_channels[i],
                                                              ctx->person_a,
                                                              ctx->person_a_count,
                                                              ctx->person_b,
                                                              ctx->person_b_count);
    }
    ctx->ok = true;
    return true;
}

static void draw_tiny_digit(int digit, int x, int y, uint16_t color)
{
    static const uint8_t masks[10] = {
        0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f,
    };
    if (digit < 0 || digit > 9) {
        return;
    }
    const uint8_t mask = masks[digit];
    if ((mask & 0x01) != 0) {
        faculty175_display_draw_line(x, y, x + 2, y, color);
    }
    if ((mask & 0x02) != 0) {
        faculty175_display_draw_line(x + 2, y, x + 2, y + 2, color);
    }
    if ((mask & 0x04) != 0) {
        faculty175_display_draw_line(x + 2, y + 2, x + 2, y + 4, color);
    }
    if ((mask & 0x08) != 0) {
        faculty175_display_draw_line(x, y + 4, x + 2, y + 4, color);
    }
    if ((mask & 0x10) != 0) {
        faculty175_display_draw_line(x, y + 2, x, y + 4, color);
    }
    if ((mask & 0x20) != 0) {
        faculty175_display_draw_line(x, y, x, y + 2, color);
    }
    if ((mask & 0x40) != 0) {
        faculty175_display_draw_line(x, y + 2, x + 2, y + 2, color);
    }
}

static void draw_tiny_gate_number(uint8_t gate, int cx, int cy, uint16_t color)
{
    if (gate >= 10) {
        draw_tiny_digit((int)(gate / 10), cx - 4, cy - 2, color);
        draw_tiny_digit((int)(gate % 10), cx, cy - 2, color);
    } else {
        draw_tiny_digit((int)gate, cx - 1, cy - 2, color);
    }
}

static void draw_triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t color)
{
    faculty175_display_draw_line(x0, y0, x1, y1, color);
    faculty175_display_draw_line(x1, y1, x2, y2, color);
    faculty175_display_draw_line(x2, y2, x0, y0, color);
}

static int edge_fn(int ax, int ay, int bx, int by, int px, int py)
{
    return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}

static void fill_triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t color)
{
    int min_x = x0 < x1 ? x0 : x1;
    min_x = min_x < x2 ? min_x : x2;
    int max_x = x0 > x1 ? x0 : x1;
    max_x = max_x > x2 ? max_x : x2;
    int min_y = y0 < y1 ? y0 : y1;
    min_y = min_y < y2 ? min_y : y2;
    int max_y = y0 > y1 ? y0 : y1;
    max_y = max_y > y2 ? max_y : y2;
    if (min_x < 0) {
        min_x = 0;
    }
    if (min_y < 0) {
        min_y = 0;
    }
    if (max_x >= FACULTY175_LCD_W) {
        max_x = FACULTY175_LCD_W - 1;
    }
    if (max_y >= FACULTY175_LCD_H) {
        max_y = FACULTY175_LCD_H - 1;
    }
    const int area = edge_fn(x0, y0, x1, y1, x2, y2);
    if (area == 0) {
        return;
    }
    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            const int w0 = edge_fn(x1, y1, x2, y2, x, y);
            const int w1 = edge_fn(x2, y2, x0, y0, x, y);
            const int w2 = edge_fn(x0, y0, x1, y1, x, y);
            if ((area > 0 && w0 >= 0 && w1 >= 0 && w2 >= 0) ||
                (area < 0 && w0 <= 0 && w1 <= 0 && w2 <= 0)) {
                faculty175_display_draw_pixel(x, y, color);
            }
        }
    }
}

static void fill_rect_outline(int x, int y, int w, int h, uint16_t fill, uint16_t rim)
{
    faculty175_display_fill_rect(x, y, w, h, fill);
    faculty175_display_draw_line(x, y, x + w, y, rim);
    faculty175_display_draw_line(x + w, y, x + w, y + h, rim);
    faculty175_display_draw_line(x + w, y + h, x, y + h, rim);
    faculty175_display_draw_line(x, y + h, x, y, rim);
}

static void fill_ellipse(int cx, int cy, int rx, int ry, uint16_t color)
{
    if (rx <= 0 || ry <= 0) {
        return;
    }
    const int x0 = cx - rx < 0 ? 0 : cx - rx;
    const int y0 = cy - ry < 0 ? 0 : cy - ry;
    const int x1 = cx + rx >= FACULTY175_LCD_W ? FACULTY175_LCD_W - 1 : cx + rx;
    const int y1 = cy + ry >= FACULTY175_LCD_H ? FACULTY175_LCD_H - 1 : cy + ry;
    const int64_t rr = (int64_t)rx * rx * ry * ry;
    for (int y = y0; y <= y1; ++y) {
        const int dy = y - cy;
        for (int x = x0; x <= x1; ++x) {
            const int dx = x - cx;
            const int64_t v = (int64_t)dx * dx * ry * ry + (int64_t)dy * dy * rx * rx;
            if (v <= rr) {
                faculty175_display_draw_pixel(x, y, color);
            }
        }
    }
}

static void draw_ellipse(int cx, int cy, int rx, int ry, uint16_t color)
{
    if (rx <= 0 || ry <= 0) {
        return;
    }
    int prev_x = 0;
    int prev_y = 0;
    for (int i = 0; i <= 64; ++i) {
        const float a = ((float)i / 64.0f) * 2.0f * (float)M_PI;
        const int x = cx + (int)lrintf(cosf(a) * (float)rx);
        const int y = cy + (int)lrintf(sinf(a) * (float)ry);
        if (i > 0) {
            faculty175_display_draw_line(prev_x, prev_y, x, y, color);
        }
        prev_x = x;
        prev_y = y;
    }
}

static void draw_diamond(int x, int y, int r, uint16_t fill, uint16_t rim)
{
    fill_triangle(x, y - r, x + r, y, x, y + r, fill);
    fill_triangle(x, y - r, x - r, y, x, y + r, fill);
    faculty175_display_draw_line(x, y - r, x + r, y, rim);
    faculty175_display_draw_line(x + r, y, x, y + r, rim);
    faculty175_display_draw_line(x, y + r, x - r, y, rim);
    faculty175_display_draw_line(x - r, y, x, y - r, rim);
}

static int body_x(int local_x)
{
    return HD_CX + (local_x * HD_BODY_SCALE) / 100;
}

static int body_y(int local_y)
{
    return HD_CY + 5 + (local_y * HD_BODY_SCALE) / 100;
}

static int body_s(int px)
{
    int scaled = (px * HD_BODY_SCALE + 50) / 100;
    if (px > 0 && scaled < 1) {
        scaled = 1;
    }
    return scaled;
}

static hd_point_t body_center_pos(int idx)
{
    const hd_point_t p = k_body_centers[idx];
    return (hd_point_t){body_x(p.x), body_y(p.y)};
}

static void draw_body_aura(const hd_palette_t *pal)
{
    const uint16_t aura = blend565(pal->bg, pal->accent, 0.025f);
    const uint16_t aura_edge = blend565(pal->bg, pal->accent, 0.18f);
    fill_ellipse(HD_CX, HD_CY + 24, body_s(112), body_s(154), aura);
    draw_ellipse(HD_CX, HD_CY + 24, body_s(112), body_s(154), aura_edge);
}

static void draw_body_silhouette(const hd_palette_t *pal)
{
    const uint16_t fill = blend565(pal->bg, c(205, 181, 167), 0.34f);
    const uint16_t edge = blend565(pal->bg, c(112, 87, 42), 0.38f);
    const int cx = HD_CX;
    const int cy = HD_CY + body_s(8);

    fill_ellipse(cx, cy - body_s(104), body_s(37), body_s(48), fill);
    fill_rect_outline(cx - body_s(10), cy - body_s(60), body_s(20), body_s(32), fill, fill);
    fill_ellipse(cx, cy - body_s(22), body_s(58), body_s(38), fill);
    fill_triangle(cx - body_s(56), cy, cx + body_s(56), cy, cx + body_s(42), cy + body_s(104), fill);
    fill_triangle(cx - body_s(56), cy, cx - body_s(42), cy + body_s(104), cx + body_s(42), cy + body_s(104), fill);

    fill_triangle(cx - body_s(58), cy - body_s(18), cx - body_s(132), cy + body_s(92), cx - body_s(96), cy + body_s(106), fill);
    fill_triangle(cx + body_s(58), cy - body_s(18), cx + body_s(132), cy + body_s(92), cx + body_s(96), cy + body_s(106), fill);

    draw_ellipse(cx, cy - body_s(104), body_s(37), body_s(48), edge);
    draw_ellipse(cx, cy - body_s(22), body_s(58), body_s(38), edge);
    faculty175_display_draw_line(cx - body_s(56), cy, cx - body_s(42), cy + body_s(104), edge);
    faculty175_display_draw_line(cx + body_s(56), cy, cx + body_s(42), cy + body_s(104), edge);
    faculty175_display_draw_line(cx - body_s(42), cy + body_s(104), cx + body_s(42), cy + body_s(104), edge);
}

static void draw_body_center(int idx, const hd_palette_t *pal)
{
    const hd_center_t *center = &k_centers[idx];
    const hd_point_t p = body_center_pos(idx);
    const uint16_t base = c(center->r, center->g, center->b);
    const uint16_t fill = center->defined ? blend565(base, pal->center_defined_mix, 0.58f) : pal->center_undefined;
    const uint16_t rim = pal->center_rim;
    const uint16_t shadow = blend565(pal->bg, c(0, 0, 0), 0.10f);
    fill_ellipse(p.x, p.y + body_s(2), body_s(23), body_s(15), shadow);
    if (idx == HD_CENTER_HEAD) {
        fill_triangle(p.x, p.y - body_s(22), p.x - body_s(32), p.y + body_s(22), p.x + body_s(32), p.y + body_s(22), fill);
        draw_triangle(p.x, p.y - body_s(22), p.x - body_s(32), p.y + body_s(22), p.x + body_s(32), p.y + body_s(22), rim);
    } else if (idx == HD_CENTER_AJNA) {
        fill_triangle(p.x - body_s(32), p.y - body_s(22), p.x + body_s(32), p.y - body_s(22), p.x, p.y + body_s(24), fill);
        draw_triangle(p.x - body_s(32), p.y - body_s(22), p.x + body_s(32), p.y - body_s(22), p.x, p.y + body_s(24), rim);
    } else if (idx == HD_CENTER_G) {
        draw_diamond(p.x, p.y, body_s(34), fill, rim);
    } else if (idx == HD_CENTER_SACRAL || idx == HD_CENTER_ROOT || idx == HD_CENTER_THROAT) {
        fill_rect_outline(p.x - body_s(31), p.y - body_s(23), body_s(62), body_s(46), fill, rim);
    } else if (idx == HD_CENTER_EGO) {
        fill_triangle(p.x - body_s(23), p.y - body_s(20), p.x + body_s(27), p.y, p.x - body_s(18), p.y + body_s(22), fill);
        draw_triangle(p.x - body_s(23), p.y - body_s(20), p.x + body_s(27), p.y, p.x - body_s(18), p.y + body_s(22), rim);
    } else {
        const int dir = idx == HD_CENTER_SPLEEN ? 1 : -1;
        fill_triangle(p.x - dir * body_s(36), p.y - body_s(34), p.x - dir * body_s(36), p.y + body_s(34), p.x + dir * body_s(28), p.y, fill);
        draw_triangle(p.x - dir * body_s(36), p.y - body_s(34), p.x - dir * body_s(36), p.y + body_s(34), p.x + dir * body_s(28), p.y, rim);
    }
}

static hd_point_t offset_point(hd_point_t a, hd_point_t b, int offset)
{
    const float dx = (float)(b.x - a.x);
    const float dy = (float)(b.y - a.y);
    const float len = sqrtf(dx * dx + dy * dy);
    if (len < 1.0f || offset == 0) {
        return a;
    }
    return (hd_point_t){
        a.x + (int)lrintf((-dy / len) * (float)offset),
        a.y + (int)lrintf((dx / len) * (float)offset),
    };
}

static void fill_body_band(hd_point_t a, hd_point_t b, int width, int offset, uint16_t fill, uint16_t outline)
{
    const hd_point_t orig_a = a;
    const hd_point_t orig_b = b;
    a = offset_point(orig_a, orig_b, offset);
    b = offset_point(orig_b, orig_a, -offset);
    const float dx = (float)(b.x - a.x);
    const float dy = (float)(b.y - a.y);
    const float len = sqrtf(dx * dx + dy * dy);
    if (len < 1.0f) {
        return;
    }
    const int ox = (int)lrintf((-dy / len) * ((float)width * 0.5f));
    const int oy = (int)lrintf((dx / len) * ((float)width * 0.5f));
    const int ax0 = a.x + ox;
    const int ay0 = a.y + oy;
    const int ax1 = a.x - ox;
    const int ay1 = a.y - oy;
    const int bx0 = b.x + ox;
    const int by0 = b.y + oy;
    const int bx1 = b.x - ox;
    const int by1 = b.y - oy;
    fill_triangle(ax0, ay0, bx0, by0, bx1, by1, fill);
    fill_triangle(ax0, ay0, bx1, by1, ax1, ay1, fill);
    faculty175_display_draw_line(ax0, ay0, bx0, by0, outline);
    faculty175_display_draw_line(ax1, ay1, bx1, by1, outline);
}

static void draw_body_segment(hd_point_t a, hd_point_t b, float t0, float t1, uint16_t color, int width, int offset)
{
    const hd_point_t s = {
        a.x + (int)lrintf((float)(b.x - a.x) * t0),
        a.y + (int)lrintf((float)(b.y - a.y) * t0),
    };
    const hd_point_t e = {
        a.x + (int)lrintf((float)(b.x - a.x) * t1),
        a.y + (int)lrintf((float)(b.y - a.y) * t1),
    };
    fill_body_band(s, e, width, offset, color, color);
}

static bool same_channel_pair(const hd_channel_t *a, const hd_channel_t *b)
{
    return a != NULL && b != NULL && ((a->center_a == b->center_a && a->center_b == b->center_b) ||
                                      (a->center_a == b->center_b && a->center_b == b->center_a));
}

static int channel_lane_offset(const hd_channel_t *channels, size_t count, size_t index)
{
    const hd_channel_t *channel = &channels[index];
    int total = 0;
    int seen = 0;
    for (size_t i = 0; i < count; ++i) {
        if (same_channel_pair(channel, &channels[i])) {
            if (i < index) {
                ++seen;
            }
            ++total;
        }
    }
    return (seen * 2 - (total - 1)) * body_s(4);
}

static void draw_channels(const hd_palette_t *pal)
{
    (void)pal;
    const uint16_t inactive_fill = c(229, 226, 216);
    const uint16_t inactive_outline = c(112, 87, 42);
    for (size_t i = 0; i < sizeof(k_body_channels) / sizeof(k_body_channels[0]); ++i) {
        const hd_channel_t *channel = &k_body_channels[i];
        const int offset = channel_lane_offset(k_body_channels, sizeof(k_body_channels) / sizeof(k_body_channels[0]), i);
        const hd_point_t a = body_center_pos(channel->center_a);
        const hd_point_t b = body_center_pos(channel->center_b);
        fill_body_band(a, b, body_s(7), offset, inactive_fill, inactive_outline);
    }
}

static uint16_t relationship_color(hd_relationship_t rel, const hd_palette_t *pal)
{
    switch (rel) {
        case HD_REL_ELECTROMAGNETIC:
            return c(255, 210, 72);
        case HD_REL_COMPANIONSHIP:
            return c(118, 214, 154);
        case HD_REL_COMPROMISE:
            return c(242, 124, 88);
        case HD_REL_DOMINANCE_A:
            return pal->design;
        case HD_REL_DOMINANCE_B:
            return pal->personality;
        case HD_REL_NONE:
        default:
            return pal->dim;
    }
}

static void draw_channel_overlay(const hd_palette_t *pal, const hd_relationship_ctx_t *ctx)
{
    if (ctx == NULL || !ctx->connection || !ctx->ok) {
        return;
    }
    for (size_t i = 0; i < sizeof(k_body_channels) / sizeof(k_body_channels[0]); ++i) {
        const hd_relationship_t rel = ctx->relationships[i];
        if (rel == HD_REL_NONE) {
            continue;
        }
        const hd_channel_t *channel = &k_body_channels[i];
        const int offset = channel_lane_offset(k_body_channels, sizeof(k_body_channels) / sizeof(k_body_channels[0]), i);
        const hd_point_t a = body_center_pos(channel->center_a);
        const hd_point_t b = body_center_pos(channel->center_b);
        const uint16_t color = relationship_color(rel, pal);
        fill_body_band(a, b, body_s(rel == HD_REL_ELECTROMAGNETIC ? 11 : 9), offset, c(250, 246, 232), color);
        if (rel == HD_REL_ELECTROMAGNETIC) {
            draw_body_segment(a, b, 0.0f, 0.50f, pal->design, body_s(5), offset);
            draw_body_segment(a, b, 0.50f, 1.0f, pal->personality, body_s(5), offset);
        } else {
            draw_body_segment(a, b, 0.0f, 1.0f, color, body_s(5), offset);
        }
        faculty175_display_fill_circle((a.x + b.x) / 2, (a.y + b.y) / 2, body_s(3), color);
    }
}

static float mandala_gate_start_deg(int slot)
{
    return HD_ZODIAC_START_DEG + HD_RAVE_START_DEGREE + (float)slot * (360.0f / 64.0f);
}

static void polar_point(float deg, int radius, int *out_x, int *out_y)
{
    const float a = deg * ((float)M_PI / 180.0f);
    *out_x = HD_CX + (int)lrintf(cosf(a) * (float)radius);
    *out_y = HD_CY + (int)lrintf(sinf(a) * (float)radius);
}

static void draw_radial_line(float deg, int r0, int r1, uint16_t color)
{
    int x0;
    int y0;
    int x1;
    int y1;
    polar_point(deg, r0, &x0, &y0);
    polar_point(deg, r1, &x1, &y1);
    faculty175_display_draw_line(x0, y0, x1, y1, color);
}

static void draw_ring_arc(float start_deg, float end_deg, int radius, uint16_t color)
{
    int prev_x = 0;
    int prev_y = 0;
    for (int i = 0; i <= 10; ++i) {
        const float t = (float)i / 10.0f;
        const float deg = start_deg + (end_deg - start_deg) * t;
        int x;
        int y;
        polar_point(deg, radius, &x, &y);
        if (i > 0) {
            faculty175_display_draw_line(prev_x, prev_y, x, y, color);
        }
        prev_x = x;
        prev_y = y;
    }
}

static uint16_t mandala_sector_color(int slot)
{
    static const uint8_t colors[12][3] = {
        {215, 25, 32},  {0, 111, 115}, {138, 106, 18}, {0, 71, 171},
        {215, 25, 32},  {0, 111, 115}, {138, 106, 18}, {58, 46, 163},
        {215, 25, 32},  {0, 111, 115}, {138, 106, 18}, {0, 71, 171},
    };
    const int idx = (slot * 12) / 64;
    return c(colors[idx][0], colors[idx][1], colors[idx][2]);
}

static uint16_t zodiac_color(int sign)
{
    static const uint8_t colors[12][3] = {
        {215, 25, 32},   /* Aries */
        {0, 111, 115},   /* Taurus */
        {138, 106, 18},  /* Gemini */
        {0, 71, 171},    /* Cancer */
        {215, 25, 32},   /* Leo */
        {0, 111, 115},   /* Virgo */
        {138, 106, 18},  /* Libra */
        {58, 46, 163},   /* Scorpio */
        {215, 25, 32},   /* Sagittarius */
        {0, 111, 115},   /* Capricorn */
        {138, 106, 18},  /* Aquarius */
        {0, 71, 171},    /* Pisces */
    };
    const int idx = sign < 0 ? 0 : (sign > 11 ? 11 : sign);
    return c(colors[idx][0], colors[idx][1], colors[idx][2]);
}

static uint16_t quarter_color(int quarter)
{
    static const uint8_t colors[4][3] = {
        {0, 174, 239},
        {72, 208, 24},
        {244, 20, 144},
        {243, 154, 0},
    };
    const int idx = quarter < 0 ? 0 : (quarter > 3 ? 3 : quarter);
    return c(colors[idx][0], colors[idx][1], colors[idx][2]);
}

static uint16_t activation_color(size_t index)
{
    static const uint8_t colors[][3] = {
        {244, 143, 163}, {255, 244, 106}, {167, 122, 74}, {142, 232, 142}, {201, 201, 201},
    };
    const size_t idx = index % (sizeof(colors) / sizeof(colors[0]));
    return c(colors[idx][0], colors[idx][1], colors[idx][2]);
}

static void fill_annular_sector(float start_deg, float end_deg, int inner_r, int outer_r, uint16_t color)
{
    int x0;
    int y0;
    int x1;
    int y1;
    int x2;
    int y2;
    int x3;
    int y3;
    polar_point(start_deg, inner_r, &x0, &y0);
    polar_point(start_deg, outer_r, &x1, &y1);
    polar_point(end_deg, outer_r, &x2, &y2);
    polar_point(end_deg, inner_r, &x3, &y3);
    fill_triangle(x0, y0, x1, y1, x2, y2, color);
    fill_triangle(x0, y0, x2, y2, x3, y3, color);
}

static void fill_annular_arc(float start_deg, float end_deg, int inner_r, int outer_r, uint16_t color, int steps)
{
    if (steps < 1) {
        steps = 1;
    }
    const float step = (end_deg - start_deg) / (float)steps;
    for (int i = 0; i < steps; ++i) {
        fill_annular_sector(start_deg + (float)i * step, start_deg + (float)(i + 1) * step, inner_r, outer_r, color);
    }
}

static void draw_small_cross(int x, int y, int r, uint16_t color)
{
    faculty175_display_draw_line(x - r, y, x + r, y, color);
    faculty175_display_draw_line(x, y - r, x, y + r, color);
}

static void draw_zodiac_glyph(int sign, int x, int y, uint16_t color)
{
#define ZG(v) ((int)lrintf((float)(v) * 0.62f))
#define ZGLINE(x0, y0, x1, y1) faculty175_display_draw_line(x + ZG(x0), y + ZG(y0), x + ZG(x1), y + ZG(y1), color)
#define ZGCIRCLE(cx, cy, r) faculty175_display_draw_circle(x + ZG(cx), y + ZG(cy), ZG(r), color)
    switch (sign) {
        case 0:
            ZGCIRCLE(-4, 2, 5);
            ZGCIRCLE(4, 2, 5);
            ZGLINE(0, 1, 0, 8);
            break;
        case 1:
            ZGCIRCLE(0, 3, 6);
            ZGLINE(-4, -2, -8, -7);
            ZGLINE(4, -2, 8, -7);
            break;
        case 2:
            ZGLINE(-5, -7, -5, 8);
            ZGLINE(5, -7, 5, 8);
            ZGLINE(-7, -7, 7, -7);
            ZGLINE(-7, 8, 7, 8);
            break;
        case 3:
            ZGCIRCLE(-4, -2, 4);
            ZGCIRCLE(4, 5, 4);
            ZGLINE(-1, -1, 7, -7);
            ZGLINE(-7, 10, 1, 4);
            break;
        case 4:
            ZGCIRCLE(-3, 1, 5);
            ZGLINE(2, 3, 9, 8);
            ZGLINE(9, 8, 5, 11);
            break;
        case 5:
            ZGLINE(-8, 8, -8, -7);
            ZGLINE(-8, -7, -2, 1);
            ZGLINE(-2, 1, 4, -7);
            ZGLINE(4, -7, 4, 8);
            ZGLINE(4, 2, 10, 8);
            break;
        case 6:
            ZGLINE(-8, 6, 8, 6);
            ZGLINE(-7, 10, 7, 10);
            ZGCIRCLE(0, 3, 6);
            break;
        case 7:
            ZGLINE(-8, 8, -8, -7);
            ZGLINE(-8, -7, -2, 1);
            ZGLINE(-2, 1, 4, -7);
            ZGLINE(4, -7, 4, 8);
            ZGLINE(5, 2, 11, 8);
            ZGLINE(11, 8, 7, 11);
            break;
        case 8:
            ZGLINE(-7, 7, 8, -8);
            ZGLINE(2, -8, 8, -8);
            ZGLINE(8, -8, 8, -2);
            break;
        case 9:
            ZGLINE(-8, 8, -8, -7);
            ZGLINE(-8, -7, 5, 8);
            ZGLINE(5, 8, 5, -7);
            ZGCIRCLE(9, 6, 4);
            break;
        case 10:
            ZGLINE(-8, -2, -3, -5);
            ZGLINE(-3, -5, 2, -2);
            ZGLINE(2, -2, 7, -5);
            ZGLINE(-8, 5, -3, 2);
            ZGLINE(-3, 2, 2, 5);
            ZGLINE(2, 5, 7, 2);
            break;
        default:
            ZGLINE(-7, -6, -7, 8);
            ZGLINE(7, -6, 7, 8);
            ZGCIRCLE(0, 1, 7);
            break;
    }
#undef ZGCIRCLE
#undef ZGLINE
#undef ZG
}

static void draw_planet_glyph(const char *body, int x, int y, uint16_t color, uint16_t bg)
{
    (void)bg;
#define PG(v) ((int)lrintf((float)(v) * 0.68f))
#define PGLINE(x0, y0, x1, y1) faculty175_display_draw_line(x + PG(x0), y + PG(y0), x + PG(x1), y + PG(y1), color)
    if (body == NULL) {
        return;
    }
    if (strcmp(body, "SUN") == 0) {
        faculty175_display_draw_circle(x, y, PG(6), color);
        faculty175_display_fill_circle(x, y, PG(2), color);
    } else if (strcmp(body, "MOO") == 0) {
        faculty175_display_draw_circle(x - PG(2), y, PG(6), color);
        faculty175_display_draw_circle(x + PG(2), y, PG(6), color);
    } else if (strcmp(body, "MER") == 0) {
        faculty175_display_draw_circle(x, y - PG(1), PG(5), color);
        draw_small_cross(x, y + PG(8), PG(4), color);
        PGLINE(-4, -7, -8, -11);
        PGLINE(4, -7, 8, -11);
    } else if (strcmp(body, "VEN") == 0) {
        faculty175_display_draw_circle(x, y - PG(2), PG(5), color);
        draw_small_cross(x, y + PG(8), PG(4), color);
    } else if (strcmp(body, "MAR") == 0) {
        faculty175_display_draw_circle(x - PG(2), y + PG(2), PG(5), color);
        PGLINE(2, -2, 9, -9);
        PGLINE(4, -9, 9, -9);
        PGLINE(9, -9, 9, -4);
    } else if (strcmp(body, "JUP") == 0) {
        faculty175_display_draw_text("4", x - PG(4), y - PG(6), color);
        PGLINE(-6, 5, 7, 5);
    } else if (strcmp(body, "SAT") == 0) {
        faculty175_display_draw_text("h", x - PG(4), y - PG(7), color);
        PGLINE(4, 3, 8, 8);
    } else if (strcmp(body, "URA") == 0) {
        faculty175_display_draw_circle(x, y + PG(2), PG(4), color);
        PGLINE(0, -8, 0, 8);
        PGLINE(-7, -4, 7, -4);
        PGLINE(-7, -8, -7, 1);
        PGLINE(7, -8, 7, 1);
    } else if (strcmp(body, "NEP") == 0) {
        PGLINE(0, -9, 0, 8);
        PGLINE(-7, -3, 0, -9);
        PGLINE(7, -3, 0, -9);
        PGLINE(-5, 4, 5, 4);
    } else if (strcmp(body, "PLU") == 0) {
        faculty175_display_draw_text("P", x - PG(5), y - PG(8), color);
        faculty175_display_draw_circle(x, y + PG(5), PG(3), color);
    } else if (strcmp(body, "NOD") == 0) {
        faculty175_display_draw_circle(x - PG(4), y, PG(4), color);
        faculty175_display_draw_circle(x + PG(4), y, PG(4), color);
        PGLINE(-8, 5, 8, 5);
    } else {
        faculty175_display_fill_circle(x, y, PG(3), color);
    }
#undef PGLINE
#undef PG
}

static void draw_oriented_segment(float deg, int radius, int tangent_half, uint16_t color)
{
    const float a = deg * ((float)M_PI / 180.0f);
    const float tx = -sinf(a);
    const float ty = cosf(a);
    const int cx = HD_CX + (int)lrintf(cosf(a) * (float)radius);
    const int cy = HD_CY + (int)lrintf(sinf(a) * (float)radius);
    faculty175_display_draw_line(cx - (int)lrintf(tx * (float)tangent_half),
                                 cy - (int)lrintf(ty * (float)tangent_half),
                                 cx + (int)lrintf(tx * (float)tangent_half),
                                 cy + (int)lrintf(ty * (float)tangent_half),
                                 color);
}

static void draw_hexagram_mark(uint8_t gate, float deg, uint16_t color)
{
    uint8_t bits = (uint8_t)((gate * 37u) ^ (gate >> 1) ^ 0x2du);
    for (int line = 0; line < 6; ++line) {
        const int radius = 180 + line * 2;
        const bool solid = ((bits >> line) & 1u) != 0u;
        if (solid) {
            draw_oriented_segment(deg, radius, 3, color);
        } else {
            draw_oriented_segment(deg - 0.18f, radius, 1, color);
            draw_oriented_segment(deg + 0.18f, radius, 1, color);
        }
    }
}

static bool gate_line_active(const hd_gate_t *gates, size_t count, uint8_t gate, uint8_t line)
{
    for (size_t i = 0; i < count; ++i) {
        if (gates[i].gate == gate && gates[i].line == line) {
            return true;
        }
    }
    return false;
}

static void set_gate_state(hd_gate_state_t *states, uint8_t gate, bool design)
{
    for (int i = 0; i < 64; ++i) {
        if (states[i].gate == gate) {
            if (design) {
                states[i].design = true;
            } else {
                states[i].personality = true;
            }
            return;
        }
    }
}

static void draw_mandala_grid(const hd_palette_t *pal, uint32_t anim_ms)
{
    memset(&s_hd_relationship, 0, sizeof(s_hd_relationship));
    hd_gate_t natal_personality[HD_TRANSIT_BODY_COUNT] = {};
    hd_gate_t natal_design[HD_TRANSIT_BODY_COUNT] = {};
    hd_gate_t realtime[HD_TRANSIT_BODY_COUNT] = {};
    const hd_gate_t *design = natal_design;
    size_t design_count = 0;
    const hd_gate_t *personality = realtime;
    size_t personality_count = 0;
    bool natal_ok = false;
    bool connection_ok = false;
    if (s_hd_mode == HD_MODE_NATAL) {
        personality = natal_personality;
        natal_ok = build_natal_gates(natal_personality,
                                     sizeof(natal_personality) / sizeof(natal_personality[0]),
                                     &personality_count,
                                     natal_design,
                                     sizeof(natal_design) / sizeof(natal_design[0]),
                                     &design_count);
    } else if (s_hd_mode == HD_MODE_CONNECTION) {
        connection_ok = build_connection_context(&s_hd_relationship);
        if (connection_ok) {
            design = s_hd_relationship.person_a;
            design_count = s_hd_relationship.person_a_count;
            personality = s_hd_relationship.person_b;
            personality_count = s_hd_relationship.person_b_count;
        }
    }
    if (!natal_ok && !connection_ok && s_hd_mode == HD_MODE_TRANSIT) {
        design = NULL;
        design_count = 0;
        personality = realtime;
        personality_count = build_realtime_transits(anim_ms, realtime, sizeof(realtime) / sizeof(realtime[0]));
    } else if (!natal_ok && !connection_ok) {
        design = NULL;
        design_count = 0;
        personality = realtime;
        personality_count = build_realtime_transits(anim_ms, realtime, sizeof(realtime) / sizeof(realtime[0]));
    }
    hd_gate_state_t states[64] = {0};
    for (int i = 0; i < 64; ++i) {
        states[i].gate = k_mandala_gate_order[i];
    }
    for (size_t i = 0; i < design_count; ++i) {
        set_gate_state(states, design[i].gate, true);
    }
    for (size_t i = 0; i < personality_count; ++i) {
        set_gate_state(states, personality[i].gate, false);
    }

    const uint16_t line_grid = c(190, 190, 184);
    const uint16_t major_grid = c(18, 18, 18);
    const uint16_t gold = pal->accent;

    for (int q = 0; q < 4; ++q) {
        const float start = HD_QUARTER_START_DEG + (float)q * 90.0f;
        fill_annular_arc(start, start + 90.0f, HD_ZODIAC_OUTER_R, HD_QUARTER_OUTER_R, quarter_color(q), 24);
        draw_ring_arc(start, start + 90.0f, HD_QUARTER_OUTER_R, major_grid);
        draw_ring_arc(start, start + 90.0f, HD_ZODIAC_OUTER_R, major_grid);
    }

    for (int sign = 0; sign < 12; ++sign) {
        const float start = HD_ZODIAC_START_DEG + (float)sign * 30.0f;
        const float end = start + 30.0f;
        fill_annular_arc(start + 0.4f, end - 0.4f, HD_HEX_OUTER_R, HD_ZODIAC_OUTER_R, zodiac_color(sign), 6);
        int x;
        int y;
        polar_point(start + 15.0f, 198, &x, &y);
        draw_zodiac_glyph(sign, x, y, c(250, 248, 238));
        draw_radial_line(start, HD_HEX_OUTER_R, HD_ZODIAC_OUTER_R, major_grid);
    }

    for (int slot = 0; slot < 64; ++slot) {
        const float gate_start = mandala_gate_start_deg(slot);
        const float gate_step = 360.0f / 64.0f;
        const float gate_end = gate_start + gate_step;
        const uint8_t gate = k_mandala_gate_order[slot];
        const bool active_gate = states[slot].design || states[slot].personality;
        const uint16_t sector_col = mandala_sector_color(slot);
        const uint16_t center_fill = active_gate ? blend565(pal->bg, sector_col, 0.30f)
                                                 : blend565(pal->bg, sector_col, 0.11f);
        const uint16_t line_fill = c(250, 247, 239);
        fill_annular_sector(gate_start + 0.08f, gate_end - 0.08f, 0, HD_PIE_OUTER_R, center_fill);
        for (int line = 1; line <= 6; ++line) {
            const float line_start = gate_start + (float)(line - 1) * (gate_step / 6.0f);
            const float line_end = gate_start + (float)line * (gate_step / 6.0f);
            const bool d = gate_line_active(design, design_count, gate, (uint8_t)line);
            const bool p = gate_line_active(personality, personality_count, gate, (uint8_t)line);
            fill_annular_sector(line_start + 0.02f, line_end - 0.02f, HD_PIE_OUTER_R, HD_LINE_OUTER_R, line_fill);
            if (d && p) {
                fill_annular_sector(line_start + 0.04f, line_end - 0.04f, HD_PIE_OUTER_R, HD_LINE_MID_R,
                                    pal->personality);
                fill_annular_sector(line_start + 0.04f, line_end - 0.04f, HD_LINE_MID_R, HD_LINE_OUTER_R,
                                    pal->design);
            } else if (d) {
                fill_annular_sector(line_start + 0.04f, line_end - 0.04f, HD_PIE_OUTER_R, HD_LINE_OUTER_R,
                                    pal->design);
            } else if (p) {
                fill_annular_sector(line_start + 0.04f, line_end - 0.04f, HD_PIE_OUTER_R, HD_LINE_OUTER_R,
                                    pal->personality);
            }
        }
        fill_annular_sector(gate_start + 0.08f, gate_end - 0.08f, HD_LINE_OUTER_R, HD_HEX_OUTER_R, c(246, 241, 230));
        draw_radial_line(gate_start, 0, HD_HEX_OUTER_R, (slot % 16) == 0 ? major_grid : line_grid);
        for (int line_div = 1; line_div < 6; ++line_div) {
            const float div_deg = gate_start + (float)line_div * (gate_step / 6.0f);
            draw_radial_line(div_deg, HD_PIE_OUTER_R, HD_LINE_OUTER_R, c(132, 128, 118));
        }
        draw_hexagram_mark(gate, gate_start + gate_step * 0.5f, c(80, 72, 56));
        if ((slot % 4) == 0) {
            draw_radial_line(gate_start, HD_PIE_OUTER_R, HD_HEX_OUTER_R, gold);
        }

        if (active_gate) {
            const uint16_t wedge = activation_color((size_t)slot);
            fill_annular_sector(gate_start + 0.18f, gate_end - 0.18f, 0, HD_PIE_OUTER_R, blend565(pal->bg, wedge, 0.46f));
        }

        int gx;
        int gy;
        polar_point(gate_start + gate_step * 0.5f, HD_PIE_OUTER_R - 7, &gx, &gy);
        draw_tiny_gate_number(gate, gx, gy, active_gate ? c(17, 17, 17) : c(92, 86, 72));

        int planet_stack = 0;
        const float planet_deg = gate_start + gate_step * 0.5f;
        for (int line = 1; line <= 6; ++line) {
            for (size_t i = 0; i < design_count; ++i) {
                if (design[i].gate == gate && design[i].line == line) {
                    int px;
                    int py;
                    int r = HD_PIE_OUTER_R - 34 - planet_stack * 13;
                    if (r < 58) {
                        r = 58;
                    }
                    polar_point(planet_deg, r, &px, &py);
                    draw_planet_glyph(design[i].body, px, py, pal->design, pal->bg);
                    ++planet_stack;
                }
            }
            for (size_t i = 0; i < personality_count; ++i) {
                if (personality[i].gate == gate && personality[i].line == line) {
                    int px;
                    int py;
                    int r = HD_PIE_OUTER_R - 34 - planet_stack * 13;
                    if (r < 58) {
                        r = 58;
                    }
                    polar_point(planet_deg, r, &px, &py);
                    draw_planet_glyph(personality[i].body, px, py, pal->personality, pal->bg);
                    ++planet_stack;
                }
            }
        }
    }

    faculty175_display_draw_circle(HD_CX, HD_CY, HD_QUARTER_OUTER_R, major_grid);
    faculty175_display_draw_circle(HD_CX, HD_CY, HD_ZODIAC_OUTER_R, major_grid);
    faculty175_display_draw_circle(HD_CX, HD_CY, HD_HEX_OUTER_R, major_grid);
    faculty175_display_draw_circle(HD_CX, HD_CY, HD_LINE_OUTER_R, major_grid);
    faculty175_display_draw_circle(HD_CX, HD_CY, HD_PIE_OUTER_R, line_grid);
    draw_radial_line(mandala_gate_start_deg(64), HD_PIE_OUTER_R, HD_QUARTER_OUTER_R, major_grid);

    const char *mode = s_hd_mode == HD_MODE_CONNECTION && connection_ok ? "CONNECT" :
                       (s_hd_mode == HD_MODE_NATAL && natal_ok ? "NATAL" : "TRANSIT");
    faculty175_display_draw_text(mode, HD_CX - (int)strlen(mode) * 3, 58, pal->subtext);
}

bool faculty175_face_human_design_action(uint32_t seed_ms)
{
    (void)seed_ms;
    if (s_hd_mode == HD_MODE_TRANSIT) {
        s_hd_mode = HD_MODE_NATAL;
    } else if (s_hd_mode == HD_MODE_NATAL) {
        s_hd_mode = HD_MODE_CONNECTION;
    } else {
        s_hd_mode = HD_MODE_TRANSIT;
    }
    return true;
}

void faculty175_face_human_design_draw(uint32_t anim_ms)
{
    (void)anim_ms;
    const hd_palette_t pal = {
        .bg = c(250, 247, 239),
        .text = c(17, 17, 17),
        .subtext = c(110, 99, 72),
        .dim = c(204, 204, 198),
        .ring = c(204, 204, 198),
        .inner_ring = c(190, 190, 184),
        .accent = c(184, 153, 63),
        .design = c(230, 52, 55),
        .personality = c(18, 18, 18),
        .center_defined_mix = c(205, 142, 78),
        .center_undefined = c(230, 230, 226),
        .center_rim = c(17, 17, 17),
        .center_text = c(17, 17, 17),
        .channel_dark = c(84, 84, 80),
        .gate_fill = c(250, 247, 239),
        .gate_border = c(128, 128, 122),
        .gate_text = c(17, 17, 17),
    };

    faculty175_display_fill_rgb565(pal.bg);
    draw_mandala_grid(&pal, anim_ms);

    draw_body_aura(&pal);
    draw_body_silhouette(&pal);
    draw_channels(&pal);
    draw_channel_overlay(&pal, &s_hd_relationship);
    for (int i = 0; i < HD_CENTER_COUNT; ++i) {
        draw_body_center(i, &pal);
    }

    faculty175_display_flush();
}
