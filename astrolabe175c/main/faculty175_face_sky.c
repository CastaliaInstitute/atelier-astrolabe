#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "faculty175_board.h"

typedef struct {
    const char *id;
    float ra;
    float dec;
    float mag;
} sky_star_t;

typedef struct {
    uint8_t a;
    uint8_t b;
} sky_seg_t;

static const sky_star_t k_stars[] = {
    {"Sirius", 101.287f, -16.716f, -1.46f}, {"Canopus", 95.988f, -52.696f, -0.74f},
    {"Arcturus", 213.915f, 19.182f, -0.05f}, {"Vega", 279.235f, 38.784f, 0.03f},
    {"Capella", 79.172f, 45.998f, 0.08f}, {"Rigel", 78.634f, -8.202f, 0.13f},
    {"Procyon", 114.825f, 5.225f, 0.34f}, {"Betelgeuse", 88.793f, 7.407f, 0.42f},
    {"Hadar", 210.956f, -60.373f, 0.61f}, {"Altair", 297.695f, 8.868f, 0.76f},
    {"Acrux", 186.65f, -63.099f, 0.76f}, {"Aldebaran", 68.98f, 16.509f, 0.85f},
    {"Antares", 247.352f, -26.432f, 0.96f}, {"Spica", 201.298f, -11.161f, 0.97f},
    {"Pollux", 116.329f, 28.026f, 1.14f}, {"Regulus", 152.093f, 11.967f, 1.35f},
    {"Adhara", 104.656f, -28.972f, 1.5f}, {"Castor", 113.649f, 31.888f, 1.57f},
    {"Bellatrix", 81.283f, 6.35f, 1.64f}, {"Elnath", 81.573f, 28.607f, 1.65f},
    {"Miaplacidus", 138.3f, -69.717f, 1.67f}, {"Alnilam", 84.053f, -1.202f, 1.69f},
    {"Alnitak", 85.19f, -1.943f, 1.74f}, {"Dubhe", 165.932f, 61.751f, 1.81f},
    {"Wezen", 111.024f, -26.393f, 1.83f}, {"Alkaid", 206.885f, 49.313f, 1.85f},
    {"Sargas", 264.395f, -42.998f, 1.86f}, {"Avior", 125.628f, -59.509f, 1.86f},
    {"Atria", 252.166f, -69.028f, 1.91f}, {"Mirzam", 95.675f, -17.956f, 1.98f},
    {"Polaris", 37.954f, 89.264f, 1.98f}, {"Gacrux", 187.791f, -57.113f, 2.06f},
    {"Saiph", 86.939f, -9.67f, 2.07f}, {"Kochab", 222.676f, 74.155f, 2.07f},
    {"Algieba", 154.993f, 19.842f, 2.08f}, {"Denebola", 177.265f, 14.572f, 2.14f},
    {"Mizar", 200.981f, 54.925f, 2.23f}, {"Merak", 165.46f, 56.382f, 2.34f},
    {"Phecda", 178.457f, 53.695f, 2.41f},
};

static const sky_seg_t k_segments[] = {
    {7, 18}, {18, 22}, {7, 22}, {22, 21}, {21, 5}, {5, 32},
    {23, 37}, {37, 38}, {38, 36}, {36, 25}, {23, 36},
    {30, 33}, {15, 34}, {34, 35}, {17, 14}, {11, 19},
    {0, 24}, {24, 16}, {16, 29}, {12, 26}, {10, 31},
    {31, 28}, {28, 20}, {20, 27}, {27, 28}, {8, 20}, {8, 10},
};

static uint16_t c(uint8_t r, uint8_t g, uint8_t b)
{
    return faculty175_display_rgb888(r, g, b);
}

static bool project_star(const sky_star_t *s, float sidereal_deg, int *x, int *y)
{
    const int cx = FACULTY175_LCD_W / 2;
    const int cy = FACULTY175_LCD_H / 2;
    const float hour = (sidereal_deg - s->ra) * 0.0174532925f;
    const float dec = s->dec * 0.0174532925f;
    const float alt_proxy = sinf(dec) * 0.35f + cosf(dec) * cosf(hour) * 0.65f;
    if (alt_proxy < -0.18f) {
        return false;
    }
    const float rr = (1.0f - alt_proxy) * 150.0f;
    const float az = atan2f(sinf(hour), cosf(hour) * sinf(dec) + 0.24f);
    *x = cx + (int)lrintf(sinf(az) * rr);
    *y = cy - (int)lrintf(cosf(az) * rr);
    const int dx = *x - cx;
    const int dy = *y - cy;
    return dx * dx + dy * dy < 178 * 178;
}

void faculty175_face_sky_draw(uint32_t anim_ms)
{
    const int cx = FACULTY175_LCD_W / 2;
    const int cy = FACULTY175_LCD_H / 2;
    const int shown_min = (int)((anim_ms / 1000u) % 1440u);
    const float sidereal = fmodf((float)shown_min * 0.25f + 110.0f, 360.0f);
    const uint16_t grid = c(22, 34, 58);
    const uint16_t line_col = c(70, 92, 138);
    const uint16_t star_col = c(220, 228, 255);

    faculty175_display_fill_rgb565(c(1, 3, 13));
    faculty175_display_draw_circle(cx, cy, 214, c(20, 28, 48));
    faculty175_display_draw_circle(cx, cy, 178, grid);
    faculty175_display_draw_circle(cx, cy, 118, grid);
    faculty175_display_draw_line(cx, 56, cx, 408, grid);
    faculty175_display_draw_line(56, cy, 408, cy, grid);

    int sx[sizeof(k_stars) / sizeof(k_stars[0])];
    int sy[sizeof(k_stars) / sizeof(k_stars[0])];
    bool vis[sizeof(k_stars) / sizeof(k_stars[0])];
    for (size_t i = 0; i < sizeof(k_stars) / sizeof(k_stars[0]); ++i) {
        vis[i] = project_star(&k_stars[i], sidereal, &sx[i], &sy[i]);
    }
    for (size_t i = 0; i < sizeof(k_segments) / sizeof(k_segments[0]); ++i) {
        if (vis[k_segments[i].a] && vis[k_segments[i].b]) {
            faculty175_display_draw_line(sx[k_segments[i].a], sy[k_segments[i].a],
                                         sx[k_segments[i].b], sy[k_segments[i].b], line_col);
        }
    }
    for (size_t i = 0; i < sizeof(k_stars) / sizeof(k_stars[0]); ++i) {
        if (!vis[i]) {
            continue;
        }
        const int r = k_stars[i].mag < 0.5f ? 3 : (k_stars[i].mag < 1.8f ? 2 : 1);
        faculty175_display_fill_circle(sx[i], sy[i], r, star_col);
    }

    char line[40];
    snprintf(line, sizeof(line), "%02d:%02d SKY", shown_min / 60, shown_min % 60);
    faculty175_display_draw_centered_text("SKY", 30, c(210, 220, 245));
    faculty175_display_draw_centered_text("CONSTELLATIONS", 50, c(134, 158, 210));
    faculty175_display_draw_centered_text(line, 398, c(160, 180, 220));
    faculty175_display_flush();
}
