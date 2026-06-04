#include <math.h>
#include <stdbool.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "faculty175_almanac.h"
#include "faculty175_board.h"

static uint16_t rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return faculty175_display_rgb888(r, g, b);
}

static void line_polar(int cx, int cy, float a, int r0, int r1, uint16_t color)
{
    const int x0 = cx + (int)lrintf(cosf(a) * (float)r0);
    const int y0 = cy + (int)lrintf(sinf(a) * (float)r0);
    const int x1 = cx + (int)lrintf(cosf(a) * (float)r1);
    const int y1 = cy + (int)lrintf(sinf(a) * (float)r1);
    faculty175_display_draw_line(x0, y0, x1, y1, color);
}

static void centered(const char *text, int y, uint16_t color)
{
    faculty175_display_draw_centered_text(text, y, color);
}

static void draw_trimmed_centered(const char *text, int y, size_t max_chars, uint16_t color)
{
    if (text == NULL || text[0] == '\0') {
        return;
    }
    char buf[80];
    strlcpy(buf, text, sizeof(buf));
    for (char *p = buf; *p != '\0'; ++p) {
        *p = (char)toupper((unsigned char)*p);
    }
    if (strlen(buf) > max_chars && max_chars > 3) {
        buf[max_chars - 3] = '.';
        buf[max_chars - 2] = '.';
        buf[max_chars - 1] = '.';
        buf[max_chars] = '\0';
    }
    centered(buf, y, color);
}

void faculty175_face_almanac_draw(uint32_t anim_ms)
{
    const int cx = FACULTY175_LCD_W / 2;
    const int cy = FACULTY175_LCD_H / 2 + 2;
    const uint16_t bg = rgb(7, 9, 13);
    const uint16_t ink = rgb(222, 226, 214);
    const uint16_t dim = rgb(112, 132, 126);
    const uint16_t green = rgb(110, 194, 146);
    const uint16_t amber = rgb(220, 174, 94);
    const uint16_t blue = rgb(102, 158, 214);

    faculty175_display_fill_rgb565(bg);
    faculty175_display_draw_circle(cx, cy, 220, rgb(24, 34, 36));
    faculty175_display_draw_circle(cx, cy, 205, rgb(44, 64, 58));
    faculty175_display_draw_circle(cx, cy, 184, rgb(26, 38, 40));

    for (int i = 0; i < 48; ++i) {
        const float a = ((float)i / 48.0f) * 6.2831853f - 1.5707963f;
        const bool major = (i % 6) == 0;
        line_polar(cx, cy, a, major ? 188 : 194, 204, major ? amber : rgb(62, 82, 76));
    }

    for (int i = 0; i < 18; ++i) {
        const float drift = (float)(anim_ms % 9000u) / 9000.0f * 6.2831853f;
        const float a = ((float)i / 18.0f) * 6.2831853f + drift;
        const int r = 42 + (i * 17) % 128;
        const int x = cx + (int)lrintf(cosf(a) * (float)r);
        const int y = cy + (int)lrintf(sinf(a) * (float)r);
        faculty175_display_fill_circle(x, y, (i % 4) == 0 ? 2 : 1, rgb(86, 120, 126));
    }

    char date[24];
    char season[40];
    char moon[40];
    char sun[32];
    char event[64];
    char planting[72];
    char prompt[144];
    const bool cached = faculty175_almanac_cached_daily_ex(date,
                                                           sizeof(date),
                                                           season,
                                                           sizeof(season),
                                                           moon,
                                                           sizeof(moon),
                                                           sun,
                                                           sizeof(sun),
                                                           event,
                                                           sizeof(event),
                                                           planting,
                                                           sizeof(planting),
                                                           prompt,
                                                           sizeof(prompt));

    centered("ALMANAC", 70, amber);
    if (cached) {
        faculty175_display_fill_circle(cx, 132, 34, rgb(18, 29, 30));
        faculty175_display_draw_circle(cx, 132, 34, green);
        faculty175_display_fill_circle(cx + 10, 123, 21, rgb(7, 9, 13));
        draw_trimmed_centered(date, 178, 18, ink);
        draw_trimmed_centered(season, 204, 28, green);
        char sky[80];
        snprintf(sky, sizeof(sky), "%s  Sun %s", moon, sun[0] != '\0' ? sun : "-");
        draw_trimmed_centered(sky, 230, 40, blue);
        draw_trimmed_centered(event[0] != '\0' ? event : planting, 280, 48, amber);
        draw_trimmed_centered(planting[0] != '\0' ? planting : prompt, 306, 48, ink);
    } else {
        faculty175_display_fill_circle(cx, 140, 38, rgb(28, 22, 18));
        faculty175_display_draw_circle(cx, 140, 38, amber);
        centered("NO CACHE", 200, amber);
        draw_trimmed_centered(faculty175_almanac_last(), 232, 42, dim);
        centered("almanac fetch", 292, green);
    }

    char status[64];
    snprintf(status, sizeof(status), "%s %s", faculty175_almanac_state_name(), faculty175_almanac_active() ? "SYNC" : "");
    draw_trimmed_centered(status, 352, 24, faculty175_almanac_active() ? green : dim);
    faculty175_display_draw_bezel_label("ALMANAC", false, 216, anim_ms, amber);
    faculty175_display_draw_bezel_label(cached ? prompt : faculty175_almanac_manifest_url(), true, 216, anim_ms, dim);
    faculty175_display_flush();
}
