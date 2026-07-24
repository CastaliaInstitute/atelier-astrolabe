#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_attr.h"
#include "faculty175_board.h"
#include "faculty175_breath.h"
#include "faculty175_faces.h"
#include "faculty175_motion.h"
#include "faculty175_power_metrics.h"
#include "faculty175_ring.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define IRONMAN_RING_STALE_MS 15000u
#define ARC_GUIDE_INHALE_MS 4000u
#define ARC_GUIDE_HOLD_MS 7000u
#define ARC_GUIDE_EXHALE_MS 8000u
#define ARC_GUIDE_DRAW_GAP_MS 1500u
#define ARC_GUIDE_TONE_CHUNK_FRAMES 512u
#define ARC_GUIDE_TONE_LOW_HZ 440.0f
#define ARC_GUIDE_TONE_HIGH_HZ 880.0f
#define ARC_GUIDE_TONE_AMPLITUDE 4200.0f
#define ARC_GUIDE_TONE_VOLUME 68u
#define ARC_GUIDE_TREND_THRESHOLD_PER_S 0.035f
#define ARC_GUIDE_EXHALE_MIN_MS 3000u

#ifndef ASTROLABE_CLAW_VARIANT
#define ASTROLABE_CLAW_VARIANT 0
#endif

static bool s_guide_active;
static volatile bool s_guide_session_running;
static volatile uint32_t s_guide_started_ms;
static volatile uint32_t s_guide_last_draw_ms;
static TaskHandle_t s_guide_tone_task;
static faculty175_breath_guide_phase_t s_guide_phase;
static uint32_t s_guide_phase_started_ms;
static uint32_t s_guide_cycle;
static float s_guide_expansion;
static float s_guide_last_waveform;
static float s_guide_waveform_velocity;
static uint32_t s_guide_last_breath_ms;
EXT_RAM_BSS_ATTR static int16_t s_guide_tone_pcm[ARC_GUIDE_TONE_CHUNK_FRAMES * 2u];

static uint16_t rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return faculty175_display_rgb888(r, g, b);
}

static uint64_t anim_to_ms(uint32_t anim_ms)
{
    return (uint64_t)anim_ms;
}

static faculty175_breath_status_t update_breath(uint32_t anim_ms)
{
    float pitch = 0.0f;
    float roll = 0.0f;
    const bool valid = faculty175_motion_pitch_roll(&pitch, &roll);
    faculty175_breath_update(anim_ms, valid, pitch, roll);
    faculty175_breath_status_t status = {};
    faculty175_breath_status(&status);
    return status;
}

static void line(int x0, int y0, int x1, int y1, uint16_t color)
{
    faculty175_display_draw_line(x0, y0, x1, y1, color);
}

static float clamp01(float value)
{
    if (value < 0.0f) {
        return 0.0f;
    }
    if (value > 1.0f) {
        return 1.0f;
    }
    return value;
}

static float eased_expansion(float progress)
{
    const float t = clamp01(progress);
    return 0.5f - 0.5f * cosf((float)M_PI * t);
}

static void guide_tone_task(void *arg)
{
    (void)arg;
    float phase = 0.0f;
    float current_hz = ARC_GUIDE_TONE_LOW_HZ;
    float current_gain = 0.0f;
    memset(s_guide_tone_pcm, 0, sizeof(s_guide_tone_pcm));
    if (faculty175_audio_write_pcm(s_guide_tone_pcm, ARC_GUIDE_TONE_CHUNK_FRAMES * 2u, 500) == ESP_OK) {
        faculty175_audio_set_speaker_volume(ARC_GUIDE_TONE_VOLUME);
        vTaskDelay(pdMS_TO_TICKS(24));
    }
    while (s_guide_session_running) {
        const uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        if ((uint32_t)(now_ms - s_guide_last_draw_ms) > ARC_GUIDE_DRAW_GAP_MS) {
            s_guide_session_running = false;
            break;
        }
        faculty175_breath_status_t breath = {};
        faculty175_breath_status(&breath);
        const float expansion = clamp01((breath.guide_target + 1.0f) * 0.5f);
        const float target_gain = (breath.guide_phase == FACULTY175_BREATH_GUIDE_INHALE ||
                                   breath.guide_phase == FACULTY175_BREATH_GUIDE_EXHALE)
                                      ? 1.0f
                                      : 0.0f;
        const float target_hz = ARC_GUIDE_TONE_LOW_HZ +
                                expansion * (ARC_GUIDE_TONE_HIGH_HZ - ARC_GUIDE_TONE_LOW_HZ);
        for (uint32_t i = 0; i < ARC_GUIDE_TONE_CHUNK_FRAMES; ++i) {
            current_hz += (target_hz - current_hz) * 0.006f;
            current_gain += (target_gain - current_gain) * 0.004f;
            const float step = 2.0f * (float)M_PI * current_hz / (float)FACULTY175_AUDIO_RATE;
            const float sample_elapsed_ms = (float)(now_ms - s_guide_started_ms) +
                                            ((float)i * 1000.0f / (float)FACULTY175_AUDIO_RATE);
            const float attack = clamp01(sample_elapsed_ms / 250.0f);
            const int16_t sample = (int16_t)lrintf(sinf(phase) * ARC_GUIDE_TONE_AMPLITUDE *
                                                   attack * current_gain);
            s_guide_tone_pcm[i * 2u] = sample;
            s_guide_tone_pcm[i * 2u + 1u] = sample;
            phase += step;
            if (phase >= 2.0f * (float)M_PI) {
                phase -= 2.0f * (float)M_PI;
            }
        }
        if (faculty175_audio_write_pcm(s_guide_tone_pcm, ARC_GUIDE_TONE_CHUNK_FRAMES * 2u, 500) != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
    faculty175_audio_set_speaker_mute(true);
    (void)faculty175_audio_reset_speaker(500);
    s_guide_tone_task = NULL;
    vTaskDelete(NULL);
}

static float guided_breath(uint32_t anim_ms,
                           bool session_running,
                           const faculty175_breath_status_t *breath,
                           faculty175_breath_guide_phase_t *phase,
                           uint32_t *phase_ms_out,
                           uint32_t *cycle_out)
{
    *phase = FACULTY175_BREATH_GUIDE_NONE;
    *phase_ms_out = 0u;
    *cycle_out = 0u;
    if (!session_running) {
        s_guide_active = false;
        s_guide_last_draw_ms = anim_ms;
        return 0.0f;
    }

    const bool returning_to_face = s_guide_last_draw_ms != 0u &&
                                   (uint32_t)(anim_ms - s_guide_last_draw_ms) > ARC_GUIDE_DRAW_GAP_MS;
    if (returning_to_face) {
        s_guide_session_running = false;
        s_guide_active = false;
        s_guide_last_draw_ms = anim_ms;
        return 0.0f;
    }
    if (!s_guide_active) {
        s_guide_active = true;
        s_guide_started_ms = anim_ms;
        s_guide_phase = FACULTY175_BREATH_GUIDE_INHALE;
        s_guide_phase_started_ms = anim_ms;
        s_guide_cycle = 1u;
        s_guide_expansion = 0.0f;
        s_guide_last_waveform = breath->waveform;
        s_guide_waveform_velocity = 0.0f;
        s_guide_last_breath_ms = anim_ms;
    }
    s_guide_last_draw_ms = anim_ms;

    const uint32_t breath_dt_ms = (uint32_t)(anim_ms - s_guide_last_breath_ms);
    if (breath->state == FACULTY175_BREATH_TRACKING && breath_dt_ms >= 20u && breath_dt_ms <= 500u) {
        const float instant_velocity = (breath->waveform - s_guide_last_waveform) * 1000.0f /
                                       (float)breath_dt_ms;
        s_guide_waveform_velocity += (instant_velocity - s_guide_waveform_velocity) * 0.18f;
    } else if (breath->state != FACULTY175_BREATH_TRACKING) {
        s_guide_waveform_velocity *= 0.9f;
    }
    s_guide_last_waveform = breath->waveform;
    s_guide_last_breath_ms = anim_ms;

    const bool trend_reliable = breath->state == FACULTY175_BREATH_TRACKING &&
                                breath->confidence >= 0.20f;
    const bool actual_inhaling = trend_reliable &&
                                 s_guide_waveform_velocity > ARC_GUIDE_TREND_THRESHOLD_PER_S;
    const bool actual_exhaling = trend_reliable &&
                                 s_guide_waveform_velocity < -ARC_GUIDE_TREND_THRESHOLD_PER_S;
    uint32_t phase_ms = (uint32_t)(anim_ms - s_guide_phase_started_ms);

    if (s_guide_phase == FACULTY175_BREATH_GUIDE_INHALE &&
        phase_ms >= ARC_GUIDE_INHALE_MS && !actual_inhaling) {
        s_guide_phase = FACULTY175_BREATH_GUIDE_HOLD;
        s_guide_phase_started_ms = anim_ms;
        phase_ms = 0u;
        s_guide_expansion = 1.0f;
    } else if (s_guide_phase == FACULTY175_BREATH_GUIDE_HOLD &&
               phase_ms >= ARC_GUIDE_HOLD_MS && !actual_inhaling) {
        s_guide_phase = FACULTY175_BREATH_GUIDE_EXHALE;
        s_guide_phase_started_ms = anim_ms;
        phase_ms = 0u;
        s_guide_expansion = 1.0f;
    } else if (s_guide_phase == FACULTY175_BREATH_GUIDE_EXHALE &&
               ((phase_ms >= ARC_GUIDE_EXHALE_MS && !actual_exhaling) ||
                (phase_ms >= ARC_GUIDE_EXHALE_MIN_MS && actual_inhaling))) {
        s_guide_phase = FACULTY175_BREATH_GUIDE_INHALE;
        s_guide_phase_started_ms = anim_ms;
        phase_ms = 0u;
        s_guide_cycle++;
        s_guide_expansion = 0.0f;
    }

    *phase = s_guide_phase;
    *phase_ms_out = phase_ms;
    *cycle_out = s_guide_cycle;
    if (s_guide_phase == FACULTY175_BREATH_GUIDE_INHALE) {
        s_guide_expansion = eased_expansion((float)phase_ms / (float)ARC_GUIDE_INHALE_MS);
    } else if (s_guide_phase == FACULTY175_BREATH_GUIDE_HOLD) {
        s_guide_expansion = 1.0f;
    } else {
        s_guide_expansion = 1.0f - eased_expansion((float)phase_ms / (float)ARC_GUIDE_EXHALE_MS);
    }
    return s_guide_expansion * 2.0f - 1.0f;
}

bool faculty175_face_ironman_action(uint32_t now_ms)
{
    s_guide_session_running = true;
    s_guide_active = false;
    s_guide_started_ms = now_ms;
    s_guide_last_draw_ms = now_ms;
    s_guide_phase = FACULTY175_BREATH_GUIDE_NONE;
    if (s_guide_tone_task == NULL && faculty175_board_audio_ready()) {
        if (xTaskCreate(guide_tone_task, "breath_tone", 6144, NULL, 8, &s_guide_tone_task) != pdPASS) {
            s_guide_tone_task = NULL;
        }
    }
    return true;
}

static void draw_arc_reactor(int cx,
                             int cy,
                             float breath,
                             float pulse,
                             float battery_fraction,
                             uint16_t core,
                             uint16_t dim)
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
    if (battery_fraction < 0.0f) battery_fraction = 0.0f;
    if (battery_fraction > 1.0f) battery_fraction = 1.0f;
    const uint8_t center_level = (uint8_t)lrintf(255.0f * battery_fraction);
    const uint8_t center_red = (uint8_t)lrintf(232.0f * battery_fraction);
    faculty175_display_fill_circle(cx, cy, inner, rgb(center_red, center_level, center_level));
    faculty175_display_fill_circle(cx, cy, inner / 2, rgb(center_level, center_level, center_level));
    faculty175_display_draw_circle(cx, cy, inner + 8, core);
}

static void draw_claw_power_gauge(int cx, int cy, float fraction, uint16_t active, uint16_t dim)
{
    const int segments = 24;
    const float start = -2.55f;
    const float span = 1.95f;
    if (fraction < 0.0f) fraction = 0.0f;
    if (fraction > 1.0f) fraction = 1.0f;
    for (int i = 0; i < segments; ++i) {
        const float a = start + span * ((float)i / (float)(segments - 1));
        const int r0 = 178;
        const int r1 = (i % 4) == 0 ? 194 : 188;
        const int x0 = cx + (int)lrintf(cosf(a) * (float)r0);
        const int y0 = cy + (int)lrintf(sinf(a) * (float)r0);
        const int x1 = cx + (int)lrintf(cosf(a) * (float)r1);
        const int y1 = cy + (int)lrintf(sinf(a) * (float)r1);
        line(x0, y0, x1, y1, ((float)i / (float)segments) <= fraction ? active : dim);
    }
}

static void draw_alpheus_shrimp(int cx, int cy, uint16_t outline, uint16_t glow)
{
    /* Compact vector mark: the shrimp travels right, with its oversized claw
     * lifted above the body. It stays native so the web simulator and device
     * share the same face geometry until the source logo is available. */
    const int bx = cx - 40;
    const int by = cy + 10;
    faculty175_display_draw_circle(bx, by, 43, glow);
    faculty175_display_draw_circle(bx + 35, by - 18, 26, outline);
    line(bx - 48, by + 2, bx - 82, by - 18, outline);
    line(bx - 46, by + 18, bx - 78, by + 38, outline);
    line(bx - 24, by + 28, bx - 46, by + 58, outline);
    line(bx - 4, by + 33, bx - 12, by + 64, outline);
    line(bx + 18, by + 22, bx + 28, by + 54, outline);
    line(bx + 28, by - 34, bx + 54, by - 72, outline);
    line(bx + 54, by - 72, bx + 96, by - 86, outline);
    line(bx + 96, by - 86, bx + 113, by - 72, outline);
    line(bx + 96, by - 86, bx + 108, by - 103, outline);
    line(bx + 48, by - 64, bx + 74, by - 48, outline);
    line(bx + 74, by - 48, bx + 105, by - 52, outline);
    line(bx + 49, by - 4, bx + 78, by + 12, outline);
    line(bx + 78, by + 12, bx + 103, by + 7, outline);
    faculty175_display_draw_circle(bx + 50, by - 25, 4, outline);
}

void faculty175_face_ironman_draw(uint32_t anim_ms)
{
    const int cx = FACULTY175_LCD_W / 2;
    const int cy = FACULTY175_LCD_H / 2;
    const faculty175_breath_status_t breath = update_breath(anim_ms);
    faculty175_breath_guide_phase_t guide_phase = FACULTY175_BREATH_GUIDE_NONE;
    uint32_t guide_phase_ms = 0u;
    uint32_t guide_cycle = 0u;
    const float guide_waveform = guided_breath(anim_ms,
                                               s_guide_session_running,
                                               &breath,
                                               &guide_phase,
                                               &guide_phase_ms,
                                               &guide_cycle);
    faculty175_breath_guide_update(guide_phase,
                                   guide_phase_ms,
                                   guide_cycle,
                                   guide_waveform);
    faculty175_breath_stream_maybe_emit(anim_ms);

    faculty175_ring_vitals_t vitals = {};
    const bool have_ring = faculty175_ring_latest_vitals(&vitals);
    const uint64_t age = have_ring && vitals.updated_ms <= anim_to_ms(anim_ms) ? anim_to_ms(anim_ms) - vitals.updated_ms : 0;
    const bool ring_fresh = have_ring && vitals.heart_rate_valid && age <= IRONMAN_RING_STALE_MS;
    const uint16_t bpm = ring_fresh ? vitals.heart_rate_bpm : 72;
    const float beat_period_ms = 60000.0f / (float)bpm;
    const float beat_phase = fmodf((float)anim_ms, beat_period_ms) / beat_period_ms;
    const float pulse = expf(-beat_phase * 9.0f);

    const uint16_t bg =
#if ASTROLABE_CLAW_VARIANT
        rgb(8, 5, 10);
#else
        rgb(6, 8, 12);
#endif
    const uint16_t hud_dim =
#if ASTROLABE_CLAW_VARIANT
        rgb(30, 78, 104);
#else
        rgb(22, 88, 104);
#endif
    uint16_t eye = rgb(170, 248, 255);
#if ASTROLABE_CLAW_VARIANT
    eye = rgb(255, 105, 48);
#endif
    if (breath.state == FACULTY175_BREATH_MOTION) {
        eye = rgb(255, 184, 72);
    } else if (breath.state == FACULTY175_BREATH_SENSOR_MISSING) {
        eye = rgb(255, 76, 64);
    }
    faculty175_power_metrics_t power = {};
    faculty175_power_metrics_status(&power);
    const float battery_fraction = power.pmu.battery_percent >= 0 && power.pmu.battery_percent <= 100
                                       ? (float)power.pmu.battery_percent / 100.0f
                                       : 0.35f;

    faculty175_display_fill_rgb565(bg);
    faculty175_display_draw_circle(cx, cy, 226, rgb(42, 48, 56));
    faculty175_display_draw_circle(cx, cy, 214 + (int)(pulse * 6.0f), hud_dim);
    faculty175_display_draw_circle(cx, cy, 196, rgb(42, 32, 26));

#if ASTROLABE_CLAW_VARIANT
    faculty175_display_draw_centered_text("ALPHEUS // CORE DIAL", 22, eye);
    draw_claw_power_gauge(cx, cy, battery_fraction, eye, hud_dim);
    faculty175_display_draw_centered_text("POWER", 56, hud_dim);
    draw_alpheus_shrimp(cx,
                        cy,
#if ASTROLABE_CLAW_VARIANT
                        rgb(0, 220, 235),
                        rgb(128, 20, 38));
#else
                        eye,
                        rgb(18, 54, 70));
#endif
#endif

    draw_arc_reactor(cx,
                     cy,
                     s_guide_session_running ? guide_waveform : breath.waveform,
                     pulse,
                     battery_fraction,
                     eye,
                     hud_dim);

    char status_line[40];
    if (breath.state == FACULTY175_BREATH_CALIBRATING) {
        const unsigned remaining = (8000u - breath.calibration_ms + 999u) / 1000u;
        snprintf(status_line, sizeof(status_line), "CALIBRATE %us", remaining);
    } else if (breath.state == FACULTY175_BREATH_MOTION) {
        snprintf(status_line, sizeof(status_line), "HOLD STILL");
    } else if (breath.state == FACULTY175_BREATH_SENSOR_MISSING) {
        snprintf(status_line, sizeof(status_line), "IMU MISSING");
    } else if (breath.rate_bpm > 0.0f) {
        snprintf(status_line,
                 sizeof(status_line),
                 "BREATH %.1f/M %u%% %c",
                 (double)breath.rate_bpm,
                 (unsigned)lrintf(breath.confidence * 100.0f),
                 breath.axis);
    } else {
        snprintf(status_line, sizeof(status_line), "FINDING BREATH %c", breath.axis);
    }
    const int text_x = cx - ((int)strlen(status_line) * 3);
    faculty175_display_draw_text(status_line, text_x, FACULTY175_LCD_H - 34, eye);
    faculty175_display_flush();
}
