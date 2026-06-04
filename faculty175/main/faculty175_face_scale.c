#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "astrolabe_time.h"
#include "faculty175_board.h"
#include "faculty175_face_scale_earth_texture.h"

typedef struct {
    const char *name;
    const char *scale;
    const char *note;
    double meters;
    uint8_t r;
    uint8_t g;
    uint8_t b;
} scale_gate_t;

static const scale_gate_t k_gates[] = {
    {"LOCAL", "10 m", "human room", 10.0, 92, 220, 170},
    {"EARTH", "12,742 km", "mean diameter", 12742000.0, 90, 170, 255},
    {"MOON", "384,400 km", "mean distance", 384400000.0, 210, 218, 235},
    {"SOLAR", "1 AU", "149,597,870 km", 149597870700.0, 255, 204, 106},
    {"STARS", "4.25 ly", "Proxima Centauri", 4.2465 * 9.4607304725808e15, 150, 210, 255},
    {"GALAXY", "105,700 ly", "Milky Way", 105700.0 * 9.4607304725808e15, 222, 178, 255},
};

static uint16_t c(uint8_t r, uint8_t g, uint8_t b)
{
    return faculty175_display_rgb888(r, g, b);
}

static float clampf(float v, float lo, float hi)
{
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

static void line_polar(int cx, int cy, float a, int r0, int r1, uint16_t color)
{
    const int x0 = cx + (int)lrintf(cosf(a) * (float)r0);
    const int y0 = cy + (int)lrintf(sinf(a) * (float)r0);
    const int x1 = cx + (int)lrintf(cosf(a) * (float)r1);
    const int y1 = cy + (int)lrintf(sinf(a) * (float)r1);
    faculty175_display_draw_line(x0, y0, x1, y1, color);
}

static void centered_at(const char *text, int cx, int y, uint16_t color)
{
    int len = 0;
    while (text != NULL && text[len] != '\0') {
        ++len;
    }
    if (len > 0) {
        faculty175_display_draw_text(text, cx - len * 3, y, color);
    }
}

static void draw_orbit(int cx, int cy, int r, uint16_t color)
{
    faculty175_display_draw_circle(cx, cy, r, color);
    faculty175_display_draw_circle(cx, cy, r + 1, c(12, 15, 26));
}

static float smoothstep(float t)
{
    t = clampf(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

static int zoom_radius(float zoom, int base, int min_r, int max_r)
{
    const float scale = 0.48f + zoom * 1.52f;
    int r = (int)lrintf((float)base * scale);
    if (r < min_r) {
        r = min_r;
    }
    if (r > max_r) {
        r = max_r;
    }
    return r;
}

static uint16_t shade_rgb565(uint16_t color, uint8_t shade)
{
    const uint8_t r = (uint8_t)(((color >> 11) & 0x1f) * 255 / 31);
    const uint8_t g = (uint8_t)(((color >> 5) & 0x3f) * 255 / 63);
    const uint8_t b = (uint8_t)((color & 0x1f) * 255 / 31);
    return c((uint8_t)(((unsigned)r * shade) / 255u),
             (uint8_t)(((unsigned)g * shade) / 255u),
             (uint8_t)(((unsigned)b * shade) / 255u));
}

static double utc_rotation_rad(uint32_t anim_ms)
{
    if (!astrolabe_time_valid()) {
        return (double)(anim_ms % 24000u) / 24000.0 * 6.283185307179586;
    }
    const time_t t = astrolabe_time_now();
    const double jd = ((double)t / 86400.0) + 2440587.5;
    const double d = jd - 2451545.0;
    double gmst_deg = 280.46061837 + 360.98564736629 * d;
    gmst_deg = fmod(gmst_deg, 360.0);
    if (gmst_deg < 0.0) {
        gmst_deg += 360.0;
    }
    return gmst_deg * 0.017453292519943295;
}

static void draw_earth_texture_globe(int cx, int cy, int radius, uint32_t anim_ms)
{
    const double rot = utc_rotation_rad(anim_ms);
    const double light_x = -0.42;
    const double light_y = -0.25;
    const double light_z = 0.88;
    for (int y = -radius; y <= radius; ++y) {
        for (int x = -radius; x <= radius; ++x) {
            const int r2 = x * x + y * y;
            if (r2 > radius * radius) {
                continue;
            }
            const double nx = (double)x / (double)radius;
            const double ny = (double)y / (double)radius;
            const double nz = sqrt(1.0 - nx * nx - ny * ny);
            double lon = atan2(nx, nz) + rot;
            while (lon < 0.0) {
                lon += 6.283185307179586;
            }
            while (lon >= 6.283185307179586) {
                lon -= 6.283185307179586;
            }
            const double lat = asin(-ny);
            int u = (int)floor((lon / 6.283185307179586) * (double)FACULTY175_EARTH_TEX_W);
            int v = (int)floor((0.5 - lat / 3.141592653589793) * (double)FACULTY175_EARTH_TEX_H);
            if (u < 0) {
                u = 0;
            } else if (u >= FACULTY175_EARTH_TEX_W) {
                u = FACULTY175_EARTH_TEX_W - 1;
            }
            if (v < 0) {
                v = 0;
            } else if (v >= FACULTY175_EARTH_TEX_H) {
                v = FACULTY175_EARTH_TEX_H - 1;
            }
            const double dot = nx * light_x + ny * light_y + nz * light_z;
            const uint8_t shade = (uint8_t)(105.0 + clampf((float)dot, 0.0f, 1.0f) * 150.0f);
            faculty175_display_draw_pixel(cx + x, cy + y,
                                          shade_rgb565(k_faculty175_earth_tex_rgb565[v * FACULTY175_EARTH_TEX_W + u],
                                                       shade));
        }
    }
    faculty175_display_draw_circle(cx, cy, radius, c(116, 188, 255));
    faculty175_display_draw_circle(cx, cy, radius + 1, c(24, 42, 68));
}

static void draw_earth_gate(int cx, int cy, uint32_t anim_ms, int radius)
{
    draw_earth_texture_globe(cx, cy, radius, anim_ms);
}

static void draw_solar_gate(int cx, int cy, uint32_t anim_ms, float zoom)
{
    faculty175_display_fill_circle(cx, cy, zoom_radius(zoom, 9, 5, 16), c(255, 210, 90));
    const int r[] = {
        zoom_radius(zoom, 34, 18, 62),
        zoom_radius(zoom, 56, 28, 96),
        zoom_radius(zoom, 82, 42, 140),
        zoom_radius(zoom, 112, 58, 184),
    };
    static const uint16_t col_seed[] = {0, 1, 2, 3};
    (void)col_seed;
    for (int i = 0; i < 4; ++i) {
        draw_orbit(cx, cy, r[i], c(36, 44, 62));
        const float a = ((float)((anim_ms / (80u + (uint32_t)i * 35u)) % 628u) / 100.0f) + (float)i;
        const int x = cx + (int)lrintf(cosf(a) * (float)r[i]);
        const int y = cy + (int)lrintf(sinf(a) * (float)r[i]);
        faculty175_display_fill_circle(x, y, i == 2 ? zoom_radius(zoom, 4, 3, 8) : zoom_radius(zoom, 3, 2, 6),
                                       c(120 + i * 30, 170 + i * 12, 220 - i * 30));
    }
}

static void draw_star_gate(int cx, int cy, uint32_t anim_ms, float zoom)
{
    const float spread = 0.52f + zoom * 1.16f;
    for (int i = 0; i < 34; ++i) {
        const uint32_t h = (uint32_t)i * 1103515245u + 12345u;
        const float a = (float)(h % 628u) / 100.0f;
        const int r = (int)lrintf((float)(18 + (int)((h >> 8) % 124u)) * spread);
        const int x = cx + (int)lrintf(cosf(a) * (float)r);
        const int y = cy + (int)lrintf(sinf(a) * (float)r);
        const uint8_t lift = (uint8_t)(120 + ((i * 17 + (int)(anim_ms / 120u)) % 90));
        faculty175_display_fill_circle(x, y, (i % 7) == 0 ? 2 : 1, c(lift, lift, 230));
    }
    faculty175_display_fill_circle(cx, cy, 5, c(255, 218, 128));
    faculty175_display_draw_circle(cx, cy, zoom_radius(zoom, 126, 62, 202), c(34, 44, 74));
}

static void draw_galaxy_gate(int cx, int cy, uint32_t anim_ms, float zoom)
{
    const float spin = (float)(anim_ms % 12000u) / 12000.0f * 6.2831853f;
    const float spread = 0.48f + zoom * 1.18f;
    for (int arm = 0; arm < 2; ++arm) {
        int px = cx;
        int py = cy;
        for (int i = 1; i < 86; ++i) {
            const float t = (float)i / 12.0f + spin + (float)arm * 3.1415926f;
            const float rr = (4.0f + (float)i * 1.45f) * spread;
            const int x = cx + (int)lrintf(cosf(t) * rr);
            const int y = cy + (int)lrintf(sinf(t) * rr * 0.56f);
            faculty175_display_draw_line(px, py, x, y, c(94, 76, 146));
            px = x;
            py = y;
        }
    }
    faculty175_display_fill_circle(cx, cy, zoom_radius(zoom, 12, 8, 20), c(240, 214, 255));
    const int sun_x = cx + zoom_radius(zoom, 58, 26, 92);
    faculty175_display_fill_circle(sun_x, cy - 8, zoom_radius(zoom, 4, 2, 7), c(255, 214, 120));
    centered_at("SUN", sun_x, cy + 2, c(255, 214, 120));
}

static void draw_zoom_rings(int cx, int cy, float zoom, uint16_t accent)
{
    const int burst = (int)lrintf(zoom * 20.0f);
    for (int r = 42 + burst; r <= 202; r += 30) {
        faculty175_display_draw_circle(cx, cy, r, r % 60 == 0 ? accent : c(24, 30, 46));
    }
    for (int i = 0; i < 24; ++i) {
        const float a = -1.5707963f + (float)i * 6.2831853f / 24.0f + zoom * 0.08f;
        const int r0 = 172 + (int)lrintf(zoom * 18.0f);
        const int r1 = 202 + (int)lrintf(zoom * 12.0f);
        line_polar(cx, cy, a, r0, r1, (i % 6) == 0 ? accent : c(38, 46, 66));
    }
}

static void draw_gate_scene(int gate, int cx, int cy, uint32_t anim_ms, float zoom)
{
    switch (gate) {
        case 0:
            faculty175_display_draw_circle(cx, cy, zoom_radius(zoom, 66, 34, 118), c(52, 74, 68));
            faculty175_display_draw_line(cx, cy - zoom_radius(zoom, 70, 36, 126), cx,
                                         cy + zoom_radius(zoom, 70, 36, 126), c(92, 220, 170));
            faculty175_display_draw_line(cx - zoom_radius(zoom, 70, 36, 126), cy,
                                         cx + zoom_radius(zoom, 70, 36, 126), cy, c(92, 220, 170));
            faculty175_display_fill_circle(cx, cy, zoom_radius(zoom, 8, 4, 16), c(255, 230, 140));
            centered_at("10 m", cx, cy + zoom_radius(zoom, 82, 42, 142), c(92, 220, 170));
            break;
        case 1:
        case 2:
            draw_earth_gate(cx, cy, anim_ms, gate == 1 ? zoom_radius(zoom, 48, 26, 104) : zoom_radius(zoom, 34, 20, 62));
            if (gate == 2) {
                const int moon_orbit = zoom_radius(zoom, 94, 54, 172);
                draw_orbit(cx, cy, moon_orbit, c(64, 70, 92));
                const float a = (float)(anim_ms % 7000u) / 7000.0f * 6.2831853f;
                faculty175_display_fill_circle(cx + (int)lrintf(cosf(a) * (float)moon_orbit),
                                               cy + (int)lrintf(sinf(a) * (float)moon_orbit),
                                               zoom_radius(zoom, 8, 5, 16), c(210, 218, 235));
            }
            break;
        case 3:
            draw_solar_gate(cx, cy, anim_ms, zoom);
            break;
        case 4:
            draw_star_gate(cx, cy, anim_ms, zoom);
            break;
        default:
            draw_galaxy_gate(cx, cy, anim_ms, zoom);
            break;
    }
}

static void format_time_line(char *out, size_t cap)
{
    if (out == NULL || cap == 0) {
        return;
    }
    if (!astrolabe_time_valid()) {
        snprintf(out, cap, "time syncing  texture NASA SVS");
        return;
    }
    char local[24] = {};
    char utc[24] = {};
    (void)astrolabe_time_format_local(local, sizeof(local));
    (void)astrolabe_time_format_utc(utc, sizeof(utc));
    const char *local_time = local;
    const char *utc_time = utc;
    if (strlen(local_time) >= 16) {
        local_time += 11;
    }
    if (strlen(utc_time) >= 16) {
        utc_time += 11;
    }
    snprintf(out, cap, "LOCAL %.8s  UTC %.8s", local_time, utc_time);
}

void faculty175_face_scale_draw(uint32_t anim_ms)
{
    const int cx = FACULTY175_LCD_W / 2;
    const int cy = FACULTY175_LCD_H / 2 + 2;
    const int gate_count = (int)(sizeof(k_gates) / sizeof(k_gates[0]));
    const uint32_t gate_ms = 5200;
    const uint32_t phase_ms = astrolabe_time_valid() ? (uint32_t)((astrolabe_time_now() % (gate_count * 7)) * 1000u) :
                                                       (anim_ms % (gate_ms * (uint32_t)gate_count));
    const uint32_t slot_ms = astrolabe_time_valid() ? 7000u : gate_ms;
    const uint32_t time_base = phase_ms / slot_ms;
    const int active = (int)(time_base % (uint32_t)gate_count);
    const scale_gate_t *g = &k_gates[active];
    const float raw_phase = (float)(phase_ms % slot_ms) / (float)slot_ms;
    const float zoom = 0.22f + smoothstep(sinf(raw_phase * 3.1415926f)) * 0.78f;

    faculty175_display_fill_rgb565(c(5, 7, 14));
    draw_zoom_rings(cx, cy, zoom, c(g->r, g->g, g->b));

    draw_gate_scene(active, cx, cy, anim_ms, zoom);

    const float log_min = 1.0f;
    const float log_max = 21.0f;
    for (int i = 0; i < gate_count; ++i) {
        const float log_m = (float)log10(k_gates[i].meters);
        const float t = clampf((log_m - log_min) / (log_max - log_min), 0.0f, 1.0f);
        const float a = -2.72f + t * 5.44f;
        const int x = cx + (int)lrintf(cosf(a) * 210.0f);
        const int y = cy + (int)lrintf(sinf(a) * 210.0f);
        const uint16_t col = i == active ? c(k_gates[i].r, k_gates[i].g, k_gates[i].b) : c(78, 88, 110);
        faculty175_display_fill_circle(x, y, i == active ? 5 : 3, col);
    }

    char time_line[80];
    format_time_line(time_line, sizeof(time_line));
    centered_at(time_line, cx, 372, c(164, 180, 206));
    centered_at(g->note, cx, 394, c(184, 196, 224));

    char top[64];
    snprintf(top, sizeof(top), "SCALE ATLAS  %s", g->name);
    char bottom[80];
    snprintf(bottom, sizeof(bottom), "%s  log10m %.1f", g->scale, log10(g->meters));
    faculty175_display_draw_bezel_label(top, false, 222, anim_ms, c(g->r, g->g, g->b));
    faculty175_display_draw_bezel_label(bottom, true, 222, anim_ms, c(184, 196, 224));
    faculty175_display_flush();
}
