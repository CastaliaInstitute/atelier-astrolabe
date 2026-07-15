#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "faculty175_board.h"
#include "faculty175_faces.h"
#include "faculty175_ring.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define IRONMAN_AUDIO_SAMPLES 96
#define IRONMAN_RING_STALE_MS 15000u

__attribute__((weak)) bool faculty175_motion_pitch_roll(float *pitch_deg, float *roll_deg)
{
    (void)pitch_deg;
    (void)roll_deg;
    return false;
}

static float s_breath;
static float s_audio;
static float s_motion;
static float s_pitch;
static float s_roll;

static uint16_t rgb(uint8_t r, uint8_t g, uint8_t b)
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

static uint64_t anim_to_ms(uint32_t anim_ms)
{
    return (uint64_t)anim_ms;
}

static float read_audio_level(void)
{
    if (!faculty175_board_audio_ready()) {
        const int32_t boot_peak = faculty175_board_mic_probe_peak();
        return clampf((float)boot_peak / 3000.0f, 0.0f, 1.0f);
    }

    int16_t samples[IRONMAN_AUDIO_SAMPLES];
    size_t got = 0;
    if (faculty175_audio_read(samples, IRONMAN_AUDIO_SAMPLES, &got, 0) != ESP_OK || got == 0) {
        return s_audio * 0.92f;
    }

    uint32_t peak = 0;
    uint64_t sum_sq = 0;
    for (size_t i = 0; i < got; ++i) {
        const int32_t v = samples[i];
        const uint32_t mag = (uint32_t)(v < 0 ? -v : v);
        if (mag > peak) {
            peak = mag;
        }
        sum_sq += (uint64_t)(v * v);
    }
    const float rms = sqrtf((float)sum_sq / (float)got);
    const float peak_level = clampf((float)peak / 9000.0f, 0.0f, 1.0f);
    const float rms_level = clampf(rms / 3800.0f, 0.0f, 1.0f);
    return (peak_level * 0.45f) + (rms_level * 0.55f);
}

static float read_motion_breath(void)
{
    float pitch = 0.0f;
    float roll = 0.0f;
    if (!faculty175_motion_pitch_roll(&pitch, &roll)) {
        return s_motion * 0.95f;
    }

    const float delta = fabsf(pitch - s_pitch) + fabsf(roll - s_roll);
    s_pitch = (s_pitch * 0.76f) + (pitch * 0.24f);
    s_roll = (s_roll * 0.76f) + (roll * 0.24f);
    return clampf(delta / 12.0f, 0.0f, 1.0f);
}

static float update_breath(uint32_t anim_ms)
{
    const float idle = (sinf(((float)anim_ms / 4200.0f) * 2.0f * (float)M_PI) + 1.0f) * 0.5f;
    s_audio = (s_audio * 0.82f) + (read_audio_level() * 0.18f);
    s_motion = (s_motion * 0.84f) + (read_motion_breath() * 0.16f);
    const float target = clampf((idle * 0.36f) + (s_audio * 0.44f) + (s_motion * 0.30f), 0.0f, 1.0f);
    s_breath = (s_breath * 0.86f) + (target * 0.14f);
    return s_breath;
}

static void line(int x0, int y0, int x1, int y1, uint16_t color)
{
    faculty175_display_draw_line(x0, y0, x1, y1, color);
}

static void draw_arc_reactor(int cx, int cy, float breath, float pulse, uint16_t core, uint16_t dim)
{
    const int r = 142 + (int)(breath * 18.0f) + (int)(pulse * 11.0f);
    const int inner = 50 + (int)(pulse * 9.0f);
    faculty175_display_draw_circle(cx, cy, r + 58, rgb(25, 84, 96));
    faculty175_display_draw_circle(cx, cy, r + 47, dim);
    faculty175_display_draw_circle(cx, cy, r + 31, core);
    faculty175_display_draw_circle(cx, cy, r + 20, core);
    faculty175_display_draw_circle(cx, cy, r + 8, rgb(184, 252, 255));
    faculty175_display_draw_circle(cx, cy, r - 30, dim);
    for (int i = 0; i < 36; ++i) {
        const float a = ((float)i / 36.0f) * 2.0f * (float)M_PI + ((float)breath * 0.08f);
        const int x0 = cx + (int)lrintf(cosf(a) * (float)(inner + 10));
        const int y0 = cy + (int)lrintf(sinf(a) * (float)(inner + 10));
        const int x1 = cx + (int)lrintf(cosf(a) * (float)(r - 18));
        const int y1 = cy + (int)lrintf(sinf(a) * (float)(r - 18));
        line(x0, y0, x1, y1, (i % 2) == 0 ? core : rgb(184, 252, 255));
    }
    for (int i = 0; i < 12; ++i) {
        const float a = ((float)i / 12.0f) * 2.0f * (float)M_PI + ((float)pulse * 0.2f);
        const int x0 = cx + (int)lrintf(cosf(a) * (float)(r + 22));
        const int y0 = cy + (int)lrintf(sinf(a) * (float)(r + 22));
        const int x1 = cx + (int)lrintf(cosf(a) * (float)(r + 56));
        const int y1 = cy + (int)lrintf(sinf(a) * (float)(r + 56));
        line(x0, y0, x1, y1, (i % 2) == 0 ? core : dim);
    }
    faculty175_display_fill_circle(cx, cy, inner, rgb(232, 255, 255));
    faculty175_display_fill_circle(cx, cy, inner / 2, rgb(255, 255, 255));
    faculty175_display_draw_circle(cx, cy, inner + 8, core);
}

void faculty175_face_ironman_draw(uint32_t anim_ms)
{
    const int cx = FACULTY175_LCD_W / 2;
    const int cy = FACULTY175_LCD_H / 2;
    const float breath = update_breath(anim_ms);

    faculty175_ring_vitals_t vitals = {};
    const bool have_ring = faculty175_ring_latest_vitals(&vitals);
    const uint64_t age = have_ring && vitals.updated_ms <= anim_to_ms(anim_ms) ? anim_to_ms(anim_ms) - vitals.updated_ms : 0;
    const bool ring_fresh = have_ring && vitals.heart_rate_valid && age <= IRONMAN_RING_STALE_MS;
    const uint16_t bpm = ring_fresh ? vitals.heart_rate_bpm : 72;
    const float beat_period_ms = 60000.0f / (float)bpm;
    const float beat_phase = fmodf((float)anim_ms, beat_period_ms) / beat_period_ms;
    const float pulse = expf(-beat_phase * 9.0f);

    const uint16_t bg = rgb(6, 8, 12);
    const uint16_t hud_dim = rgb(22, 88, 104);
    const uint16_t eye = rgb(170, 248, 255);

    faculty175_display_fill_rgb565(bg);
    faculty175_display_draw_circle(cx, cy, 226, rgb(42, 48, 56));
    faculty175_display_draw_circle(cx, cy, 214 + (int)(pulse * 6.0f), hud_dim);
    faculty175_display_draw_circle(cx, cy, 196, rgb(42, 32, 26));

    draw_arc_reactor(cx, cy, breath, pulse, eye, hud_dim);
    faculty175_display_flush();
}
