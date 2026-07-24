#include <stdbool.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "astrolabe_time.h"
#include "faculty175_board.h"
#include "faculty175_charts.h"
#include "faculty175_ephemeris.h"

static uint16_t s_fb[FACULTY175_LCD_W * FACULTY175_LCD_H];

void faculty175_face_human_design_draw(uint32_t anim_ms);
bool faculty175_face_human_design_action(uint32_t seed_ms);

bool astrolabe_time_valid(void) { return true; }
time_t astrolabe_time_now(void) { return 1783519200; }

bool faculty175_ephemeris_fetch_human_design_epoch(time_t utc_epoch, faculty175_hd_positions_t *out)
{
    static const double base[FACULTY175_HD_BODY_COUNT] = {
        280.5, 100.5, 218.3, 296.1, 334.2, 54.7, 72.0, 312.0, 41.0, 350.0, 298.0, 23.0, 203.0,
    };
    static const double rate[FACULTY175_HD_BODY_COUNT] = {
        0.985647, 0.985647, 13.176358, 4.092334, 1.602130, 0.524021,
        0.083085, 0.033444, 0.011728, 0.005981, 0.003964, -0.052953, -0.052953,
    };
    if (out == NULL) {
        return false;
    }
    const double days = (double)(utc_epoch - 946728000) / 86400.0;
    memset(out, 0, sizeof(*out));
    for (int i = 0; i < FACULTY175_HD_BODY_COUNT; ++i) {
        double lon = base[i] + days * rate[i];
        lon = fmod(lon, 360.0);
        if (lon < 0.0) {
            lon += 360.0;
        }
        out->lon[i] = lon;
    }
    out->lon[FACULTY175_HD_BODY_SOUTH_NODE] =
        fmod(out->lon[FACULTY175_HD_BODY_TRUE_NODE] + 180.0, 360.0);
    out->lon[FACULTY175_HD_BODY_EARTH] = fmod(out->lon[FACULTY175_HD_BODY_SUN] + 180.0, 360.0);
    out->ok = true;
    out->from_network = false;
    return true;
}

const char *faculty175_ephemeris_hd_body_label(faculty175_hd_body_t body)
{
    static const char *const labels[FACULTY175_HD_BODY_COUNT] = {
        "SUN", "EAR", "MOO", "MER", "VEN", "MAR", "JUP", "SAT", "URA", "NEP", "PLU", "NNO", "SNO",
    };
    return body >= 0 && body < FACULTY175_HD_BODY_COUNT ? labels[body] : "?";
}

bool faculty175_charts_active(faculty175_birth_chart_t *out)
{
    if (out == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    snprintf(out->name, sizeof(out->name), "Sim Natal");
    out->role = FACULTY175_CHART_ROLE_SELF;
    out->year = 1972;
    out->month = 5;
    out->day = 6;
    out->hour = 12;
    out->minute = 0;
    out->tz_offset_sec = -4 * 3600;
    out->lat_deg = 30.4383f;
    out->lon_deg = -84.2807f;
    out->valid = true;
    return true;
}

void faculty175_charts_ensure_family_seed(void) {}

bool faculty175_charts_primary(faculty175_birth_chart_t *out)
{
    if (out == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    snprintf(out->name, sizeof(out->name), "Daniel");
    out->role = FACULTY175_CHART_ROLE_SELF;
    out->year = 1972;
    out->month = 5;
    out->day = 6;
    out->hour = 12;
    out->minute = 0;
    out->tz_offset_sec = -4 * 3600;
    out->lat_deg = 30.4383f;
    out->lon_deg = -84.2807f;
    out->valid = true;
    return true;
}

bool faculty175_charts_birth_to_utc(const faculty175_birth_chart_t *birth, time_t *utc_out)
{
    if (utc_out == NULL) {
        return false;
    }
    if (birth != NULL && birth->year == 1983) {
        *utc_out = 432748800;
    } else if (birth != NULL && birth->year == 1972) {
        *utc_out = 74232000;
    } else {
        *utc_out = 74232000;
    }
    return true;
}

static void put_px(int x, int y, uint16_t color)
{
    if ((unsigned)x < FACULTY175_LCD_W && (unsigned)y < FACULTY175_LCD_H) {
        s_fb[y * FACULTY175_LCD_W + x] = color;
    }
}

uint16_t faculty175_display_pack_rgb888(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((uint16_t)(r & 0xf8) << 8) | ((uint16_t)(g & 0xfc) << 3) | ((uint16_t)b >> 3));
}

uint16_t faculty175_display_rgb888(uint8_t r, uint8_t g, uint8_t b)
{
    return faculty175_display_pack_rgb888(r, g, b);
}

uint16_t faculty175_display_fb_from_logical565(uint16_t logical565)
{
    return logical565;
}

uint32_t faculty175_display_bkgd_u32(void)
{
    return 0;
}

void faculty175_display_fill_rgb565(uint16_t color)
{
    for (size_t i = 0; i < sizeof(s_fb) / sizeof(s_fb[0]); ++i) {
        s_fb[i] = color;
    }
}

void faculty175_display_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    if (w <= 0 || h <= 0) {
        return;
    }
    const int x0 = x < 0 ? 0 : x;
    const int y0 = y < 0 ? 0 : y;
    const int x1 = x + w > FACULTY175_LCD_W ? FACULTY175_LCD_W : x + w;
    const int y1 = y + h > FACULTY175_LCD_H ? FACULTY175_LCD_H : y + h;
    for (int yy = y0; yy < y1; ++yy) {
        for (int xx = x0; xx < x1; ++xx) {
            put_px(xx, yy, color);
        }
    }
}

void faculty175_display_draw_rgb565(const uint16_t *pixels, int x, int y, int w, int h)
{
    if (pixels == NULL || w <= 0 || h <= 0) {
        return;
    }
    for (int yy = 0; yy < h; ++yy) {
        for (int xx = 0; xx < w; ++xx) {
            put_px(x + xx, y + yy, pixels[yy * w + xx]);
        }
    }
}

void faculty175_display_draw_pixel(int x, int y, uint16_t color)
{
    put_px(x, y, color);
}

void faculty175_display_draw_line(int x0, int y0, int x1, int y1, uint16_t color)
{
    int dx = abs(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (true) {
        put_px(x0, y0, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void faculty175_display_draw_circle(int cx, int cy, int r, uint16_t color)
{
    if (r <= 0) {
        return;
    }
    int x = -r;
    int y = 0;
    int err = 2 - 2 * r;
    do {
        put_px(cx - x, cy + y, color);
        put_px(cx - y, cy - x, color);
        put_px(cx + x, cy - y, color);
        put_px(cx + y, cy + x, color);
        const int e = err;
        if (e <= y) {
            err += ++y * 2 + 1;
        }
        if (e > x || err > y) {
            err += ++x * 2 + 1;
        }
    } while (x < 0);
}

void faculty175_display_fill_circle(int cx, int cy, int r, uint16_t color)
{
    if (r <= 0) {
        return;
    }
    const int rr = r * r;
    for (int y = -r; y <= r; ++y) {
        for (int x = -r; x <= r; ++x) {
            if (x * x + y * y <= rr) {
                put_px(cx + x, cy + y, color);
            }
        }
    }
}

static uint8_t glyph_bits(char ch, int row)
{
    uint8_t v = (uint8_t)ch;
    v ^= (uint8_t)(row * 37u);
    v ^= (uint8_t)(v >> 3);
    return (uint8_t)(0x11u | (v & 0x1eu));
}

void faculty175_display_draw_text(const char *text, int x, int y, uint16_t color)
{
    if (text == NULL) {
        return;
    }
    for (int i = 0; text[i] != '\0'; ++i) {
        if (text[i] == ' ') {
            continue;
        }
        for (int row = 0; row < 7; ++row) {
            const uint8_t bits = glyph_bits(text[i], row);
            for (int col = 0; col < 5; ++col) {
                if ((bits & (1u << (4 - col))) != 0) {
                    put_px(x + i * 6 + col, y + row, color);
                }
            }
        }
    }
}

void faculty175_display_draw_centered_text(const char *text, int y, uint16_t color)
{
    const int w = text != NULL ? (int)strlen(text) * 6 : 0;
    faculty175_display_draw_text(text, (FACULTY175_LCD_W - w) / 2, y, color);
}

void faculty175_display_draw_bezel_label(const char *text, bool bottom, int radius, uint32_t scroll_ms, uint16_t color)
{
    (void)radius;
    (void)scroll_ms;
    faculty175_display_draw_centered_text(text, bottom ? 424 : 28, color);
}

void faculty175_display_flush(void) {}
void faculty175_display_flush_rect(int x, int y, int w, int h) { (void)x; (void)y; (void)w; (void)h; }
void faculty175_display_flush_suspended_set(bool suspended) { (void)suspended; }
size_t faculty175_display_frame_pixel_count(void) { return sizeof(s_fb) / sizeof(s_fb[0]); }

bool faculty175_display_frame_copy(uint16_t *out, size_t pixel_count)
{
    if (out == NULL || pixel_count < faculty175_display_frame_pixel_count()) {
        return false;
    }
    memcpy(out, s_fb, sizeof(s_fb));
    return true;
}

void faculty175_display_frame_compose_carousel(const uint16_t *from, const uint16_t *to, int shift_px) { (void)from; (void)to; (void)shift_px; }
void faculty175_display_frame_compose_vertical(const uint16_t *from, const uint16_t *to, int shift_px) { (void)from; (void)to; (void)shift_px; }
void faculty175_display_frame_compose_radial(const uint16_t *from, const uint16_t *to, int radius_px) { (void)from; (void)to; (void)radius_px; }
void faculty175_display_frame_compose_nav_preview(const uint16_t *center, const uint16_t *left, const uint16_t *right, const uint16_t *up, const uint16_t *down) { (void)center; (void)left; (void)right; (void)up; (void)down; }
void faculty175_display_blit_rgb565_masked(const uint16_t *pixels, const uint8_t *opaque, int x, int y, int w, int h) { (void)opaque; faculty175_display_draw_rgb565(pixels, x, y, w, h); }
void faculty175_display_draw_status(faculty175_ui_state_t state, const char *faculty_name, const char *detail, uint32_t anim_ms, const uint8_t *waveform, const uint8_t *waveform_stream, size_t waveform_len) { (void)state; (void)faculty_name; (void)detail; (void)anim_ms; (void)waveform; (void)waveform_stream; (void)waveform_len; }
void faculty175_display_draw_pocketwatch(const char *detail, uint32_t anim_ms, bool boot_mode) { (void)detail; (void)anim_ms; (void)boot_mode; }
void faculty175_display_boot_progress(const char *detail, uint8_t step, uint8_t total, bool active) { (void)detail; (void)step; (void)total; (void)active; }
void faculty175_display_waveform_update(const uint8_t *waveform, const uint8_t *waveform_stream, size_t waveform_len, bool visible) { (void)waveform; (void)waveform_stream; (void)waveform_len; (void)visible; }
void faculty175_display_nav_mode_set(bool enabled) { (void)enabled; }
void faculty175_display_touch_visual_update(int16_t x, int16_t y, bool down, uint32_t now_ms) { (void)x; (void)y; (void)down; (void)now_ms; }
void faculty175_display_lock(void) {}
void faculty175_display_unlock(void) {}
size_t faculty175_display_bmp_size(void) { return 0; }
int faculty175_display_write_bmp(FILE *out) { (void)out; return 0; }
size_t faculty175_display_bmp565_size(void) { return 0; }
esp_err_t faculty175_display_write_bmp565(faculty175_display_write_cb_t write_cb, void *ctx) { (void)write_cb; (void)ctx; return ESP_FAIL; }

int main(int argc, char **argv)
{
    const char *out_path = argc > 1 ? argv[1] : "human-design.ppm";
    if (argc > 2 && strcmp(argv[2], "natal") == 0) {
        (void)faculty175_face_human_design_action(0);
    } else if (argc > 2 && strcmp(argv[2], "connection") == 0) {
        (void)faculty175_face_human_design_action(0);
        (void)faculty175_face_human_design_action(0);
    }
    faculty175_face_human_design_draw(0);

    FILE *out = fopen(out_path, "wb");
    if (out == NULL) {
        perror(out_path);
        return 1;
    }
    fprintf(out, "P6\n%d %d\n255\n", FACULTY175_LCD_W, FACULTY175_LCD_H);
    for (size_t i = 0; i < sizeof(s_fb) / sizeof(s_fb[0]); ++i) {
        const uint16_t p = s_fb[i];
        const uint8_t rgb[3] = {
            (uint8_t)(((p >> 11) & 0x1f) * 255 / 31),
            (uint8_t)(((p >> 5) & 0x3f) * 255 / 63),
            (uint8_t)((p & 0x1f) * 255 / 31),
        };
        fwrite(rgb, 1, sizeof(rgb), out);
    }
    fclose(out);
    return 0;
}
