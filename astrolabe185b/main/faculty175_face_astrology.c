#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#include "faculty175_board.h"
#include "faculty175_charts.h"

#define ASTRO_BODY_COUNT 7

typedef struct {
    const char *label;
    float base_lon;
    float deg_per_day;
    uint8_t r;
    uint8_t g;
    uint8_t b;
} astro_body_t;

typedef struct {
    bool server_valid;
    float lon[ASTRO_BODY_COUNT];
} astro_ephemeris_t;

static const char *const k_signs[12] = {"Ar", "Ta", "Ge", "Cn", "Le", "Vi", "Li", "Sc", "Sg", "Cp", "Aq", "Pi"};
static const astro_body_t k_bodies[ASTRO_BODY_COUNT] = {
    {"Su", 280.5f, 0.9856f, 255, 210, 90},
    {"Mo", 218.3f, 13.1764f, 210, 218, 235},
    {"Me", 296.1f, 4.0923f, 178, 182, 196},
    {"Ve", 334.2f, 1.6021f, 255, 190, 140},
    {"Ma", 54.7f, 0.5240f, 230, 90, 70},
    {"Ju", 72.0f, 0.0831f, 220, 180, 120},
    {"Sa", 312.0f, 0.0335f, 190, 170, 140},
};

static uint16_t c(uint8_t r, uint8_t g, uint8_t b)
{
    return faculty175_display_rgb888(r, g, b);
}

static float wrap360(float v)
{
    while (v < 0.0f) {
        v += 360.0f;
    }
    while (v >= 360.0f) {
        v -= 360.0f;
    }
    return v;
}

static void radial_line(int cx, int cy, float angle, int r0, int r1, uint16_t color)
{
    const int x0 = cx + (int)lrintf(cosf(angle) * (float)r0);
    const int y0 = cy + (int)lrintf(sinf(angle) * (float)r0);
    const int x1 = cx + (int)lrintf(cosf(angle) * (float)r1);
    const int y1 = cy + (int)lrintf(sinf(angle) * (float)r1);
    faculty175_display_draw_line(x0, y0, x1, y1, color);
}

static void thick_line(int x0, int y0, int x1, int y1, uint16_t color)
{
    faculty175_display_draw_line(x0, y0, x1, y1, color);
    faculty175_display_draw_line(x0 + 1, y0, x1 + 1, y1, color);
    faculty175_display_draw_line(x0, y0 + 1, x1, y1 + 1, color);
}

static void arc(int cx, int cy, int r, float a0, float a1, uint16_t color)
{
    int px = cx + (int)lrintf(cosf(a0) * (float)r);
    int py = cy + (int)lrintf(sinf(a0) * (float)r);
    for (int i = 1; i <= 10; ++i) {
        const float t = a0 + (a1 - a0) * ((float)i / 10.0f);
        const int x = cx + (int)lrintf(cosf(t) * (float)r);
        const int y = cy + (int)lrintf(sinf(t) * (float)r);
        faculty175_display_draw_line(px, py, x, y, color);
        px = x;
        py = y;
    }
}

static void draw_zodiac_glyph(int sign, int x, int y, uint16_t color)
{
    switch (sign % 12) {
        case 0: /* Aries */
            arc(x - 8, y + 4, 10, 3.7f, 5.9f, color);
            arc(x + 8, y + 4, 10, 3.4f, 5.5f, color);
            thick_line(x, y + 1, x, y + 13, color);
            break;
        case 1: /* Taurus */
            faculty175_display_draw_circle(x, y + 5, 8, color);
            arc(x - 8, y - 4, 8, 2.9f, 5.1f, color);
            arc(x + 8, y - 4, 8, 4.0f, 6.4f, color);
            break;
        case 2: /* Gemini */
            thick_line(x - 8, y - 9, x + 8, y - 9, color);
            thick_line(x - 8, y + 11, x + 8, y + 11, color);
            thick_line(x - 6, y - 8, x - 6, y + 10, color);
            thick_line(x + 6, y - 8, x + 6, y + 10, color);
            break;
        case 3: /* Cancer */
            faculty175_display_draw_circle(x - 6, y - 2, 5, color);
            faculty175_display_draw_circle(x + 6, y + 8, 5, color);
            thick_line(x - 1, y - 4, x + 11, y - 4, color);
            thick_line(x - 11, y + 10, x + 1, y + 10, color);
            break;
        case 4: /* Leo */
            faculty175_display_draw_circle(x - 5, y + 2, 6, color);
            arc(x + 5, y - 2, 11, 3.2f, 6.7f, color);
            thick_line(x + 8, y + 8, x + 14, y + 12, color);
            break;
        case 5: /* Virgo */
            thick_line(x - 10, y - 8, x - 10, y + 11, color);
            thick_line(x - 4, y - 8, x - 4, y + 11, color);
            thick_line(x + 2, y - 8, x + 2, y + 11, color);
            arc(x + 8, y + 3, 8, 1.6f, 5.7f, color);
            thick_line(x + 7, y + 9, x + 14, y + 13, color);
            break;
        case 6: /* Libra */
            arc(x, y - 1, 8, 3.14f, 6.28f, color);
            thick_line(x - 14, y + 5, x + 14, y + 5, color);
            thick_line(x - 14, y + 12, x + 14, y + 12, color);
            break;
        case 7: /* Scorpio */
            thick_line(x - 10, y - 8, x - 10, y + 11, color);
            thick_line(x - 4, y - 8, x - 4, y + 11, color);
            thick_line(x + 2, y - 8, x + 2, y + 11, color);
            thick_line(x + 2, y + 11, x + 13, y + 11, color);
            thick_line(x + 13, y + 11, x + 9, y + 7, color);
            break;
        case 8: /* Sagittarius */
            thick_line(x - 8, y + 10, x + 10, y - 8, color);
            thick_line(x + 10, y - 8, x + 10, y + 1, color);
            thick_line(x + 10, y - 8, x + 1, y - 8, color);
            thick_line(x - 8, y - 1, x + 1, y + 8, color);
            break;
        case 9: /* Capricorn */
            thick_line(x - 10, y - 8, x - 10, y + 11, color);
            thick_line(x - 10, y - 8, x, y + 7, color);
            arc(x + 7, y + 4, 8, 2.6f, 7.1f, color);
            break;
        case 10: /* Aquarius */
            thick_line(x - 13, y - 2, x - 7, y - 7, color);
            thick_line(x - 7, y - 7, x, y - 2, color);
            thick_line(x, y - 2, x + 7, y - 7, color);
            thick_line(x + 7, y - 7, x + 13, y - 2, color);
            thick_line(x - 13, y + 8, x - 7, y + 3, color);
            thick_line(x - 7, y + 3, x, y + 8, color);
            thick_line(x, y + 8, x + 7, y + 3, color);
            thick_line(x + 7, y + 3, x + 13, y + 8, color);
            break;
        default: /* Pisces */
            arc(x - 7, y + 1, 10, -1.1f, 1.1f, color);
            arc(x + 7, y + 1, 10, 2.0f, 4.2f, color);
            thick_line(x - 13, y + 1, x + 13, y + 1, color);
            break;
    }
}

static void draw_planet_glyph(int body, int x, int y, uint16_t color)
{
    switch (body) {
        case 0: /* Sun */
            faculty175_display_draw_circle(x, y, 8, color);
            faculty175_display_fill_circle(x, y, 2, color);
            break;
        case 1: /* Moon */
            faculty175_display_draw_circle(x - 2, y, 8, color);
            arc(x + 3, y, 8, 1.2f, 5.2f, c(12, 12, 22));
            break;
        case 2: /* Mercury */
            arc(x, y - 10, 6, 0.1f, 3.0f, color);
            faculty175_display_draw_circle(x, y, 7, color);
            thick_line(x, y + 7, x, y + 16, color);
            thick_line(x - 6, y + 12, x + 6, y + 12, color);
            break;
        case 3: /* Venus */
            faculty175_display_draw_circle(x, y - 3, 8, color);
            thick_line(x, y + 5, x, y + 16, color);
            thick_line(x - 6, y + 11, x + 6, y + 11, color);
            break;
        case 4: /* Mars */
            faculty175_display_draw_circle(x - 3, y + 3, 7, color);
            thick_line(x + 2, y - 2, x + 12, y - 12, color);
            thick_line(x + 12, y - 12, x + 12, y - 4, color);
            thick_line(x + 12, y - 12, x + 4, y - 12, color);
            break;
        case 5: /* Jupiter */
            arc(x - 4, y - 3, 9, 4.7f, 7.9f, color);
            thick_line(x + 4, y - 11, x + 4, y + 13, color);
            thick_line(x - 10, y + 4, x + 12, y + 4, color);
            break;
        default: /* Saturn */
            thick_line(x - 4, y - 12, x - 4, y + 13, color);
            thick_line(x - 10, y - 5, x + 4, y - 5, color);
            arc(x + 3, y + 5, 8, 4.7f, 7.4f, color);
            break;
    }
}

static uint32_t days_since_j2000(uint32_t anim_ms)
{
    time_t now = time(NULL);
    if (now > 946728000) {
        return (uint32_t)((now - 946728000) / 86400);
    }
    return 9400u + anim_ms / 86400000u;
}

static void local_ephemeris(uint32_t anim_ms, astro_ephemeris_t *out)
{
    const float days = (float)days_since_j2000(anim_ms) + (float)(anim_ms % 86400000u) / 86400000.0f;
    out->server_valid = false;
    for (int i = 0; i < ASTRO_BODY_COUNT; ++i) {
        out->lon[i] = wrap360(k_bodies[i].base_lon + days * k_bodies[i].deg_per_day);
    }
}

static void draw_body_label(const astro_body_t *body, int body_idx, float lon, int cx, int cy, int radius)
{
    const float a = 3.1415926f + lon * 0.0174532925f;
    const int x = cx + (int)lrintf(cosf(a) * (float)radius);
    const int y = cy + (int)lrintf(sinf(a) * (float)radius);
    const uint16_t col = c(body->r, body->g, body->b);
    faculty175_display_fill_circle(x, y, 17, c(12, 12, 22));
    faculty175_display_draw_circle(x, y, 18, col);
    draw_planet_glyph(body_idx, x, y, col);
}

static void draw_natal_body(int body_idx, double lon, int cx, int cy, int radius)
{
    const float a = 3.1415926f + (float)lon * 0.0174532925f;
    const int x = cx + (int)lrintf(cosf(a) * (float)radius);
    const int y = cy + (int)lrintf(sinf(a) * (float)radius);
    const uint16_t col = body_idx == 0 ? c(130, 220, 255) : c(92, 168, 220);
    faculty175_display_fill_circle(x, y, body_idx == 0 ? 5 : 3, col);
    faculty175_display_draw_circle(x, y, body_idx == 0 ? 8 : 6, c(24, 34, 50));
}

void faculty175_face_astrology_draw(uint32_t anim_ms)
{
    const int cx = FACULTY175_LCD_W / 2;
    const int cy = FACULTY175_LCD_H / 2 + 2;
    const int r_outer = 202;
    const int r_inner = 82;
    const int r_label = 172;
    const int r_planet = 132;
    const int r_natal = 94;
    const uint16_t bg = c(8, 9, 18);
    const uint16_t ring = c(54, 62, 84);
    const uint16_t spoke = c(78, 88, 108);
    astro_ephemeris_t eph = {};

    local_ephemeris(anim_ms, &eph);
    faculty175_charts_ensure_family_seed();
    faculty175_birth_chart_t natal = {};
    faculty175_chart_positions_t natal_pos = {};
    const bool has_natal = faculty175_charts_primary(&natal) &&
                           faculty175_charts_birth_positions(&natal, &natal_pos);

    faculty175_display_fill_rgb565(bg);
    faculty175_display_draw_circle(cx, cy, 216, c(28, 30, 46));
    faculty175_display_draw_circle(cx, cy, r_outer, ring);
    faculty175_display_draw_circle(cx, cy, r_inner, ring);
    faculty175_display_draw_circle(cx, cy, 48, c(44, 38, 58));

    for (int s = 0; s < 12; ++s) {
        const float a = -1.5707963f + ((float)s * 6.2831853f / 12.0f);
        radial_line(cx, cy, a, r_inner, r_outer, s % 3 == 0 ? c(118, 128, 158) : spoke);
        const float la = a + 6.2831853f / 24.0f;
        const int lx = cx + (int)lrintf(cosf(la) * (float)r_label);
        const int ly = cy + (int)lrintf(sinf(la) * (float)r_label);
        draw_zodiac_glyph(s, lx, ly, c(174, 182, 205));
    }

    for (int i = 0; i < ASTRO_BODY_COUNT; ++i) {
        draw_body_label(&k_bodies[i], i, eph.lon[i], cx, cy, r_planet - (i % 2) * 18);
    }
    if (has_natal) {
        faculty175_display_draw_circle(cx, cy, r_natal + 10, c(32, 66, 92));
        for (int i = 0; i < FACULTY175_CHART_BODY_COUNT; ++i) {
            draw_natal_body(i, natal_pos.lon[i], cx, cy, r_natal);
        }
    }

    faculty175_display_fill_circle(cx, cy, 36, c(18, 18, 30));
    faculty175_display_draw_circle(cx, cy, 36, c(180, 154, 88));
    faculty175_display_draw_bezel_label(has_natal ? "NATAL + TRANSIT" : "ASTROLOGY", false, 222, anim_ms,
                                        c(226, 222, 204));

    char line[96];
    if (has_natal) {
        snprintf(line, sizeof(line), "%s  n.Su %s  t.Su %s", natal.name,
                 faculty175_charts_zodiac_abbr(natal_pos.lon[0]), k_signs[(int)(eph.lon[0] / 30.0f) % 12]);
    } else {
        snprintf(line, sizeof(line), "%s %s  %s %s  %s %s", k_bodies[0].label,
                 k_signs[(int)(eph.lon[0] / 30.0f) % 12], k_bodies[1].label,
                 k_signs[(int)(eph.lon[1] / 30.0f) % 12], k_bodies[4].label,
                 k_signs[(int)(eph.lon[4] / 30.0f) % 12]);
    }
    faculty175_display_draw_bezel_label(line, true, 222, anim_ms, c(190, 198, 220));
    faculty175_display_draw_bezel_label(eph.server_valid ? "SERVER EPHEMERIS" : "LOCAL EPHEMERIS", false, 204,
                                        anim_ms, eph.server_valid ? c(116, 210, 190) : c(190, 170, 112));
    faculty175_display_flush();
}
