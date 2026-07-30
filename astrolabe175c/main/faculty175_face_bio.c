#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "esp_err.h"
#include "esp_timer.h"

#include "faculty175_ble.h"
#include "faculty175_board.h"
#include "faculty175_ring.h"

#define TAU_F 6.28318530718f
#define SELF_RING_STALE_MS (5ULL * 60ULL * 1000ULL)
#define RING_IMAGE_SIZE 180

extern const uint8_t _binary_colmi_r09_face_rgb565_start[]
    asm("_binary_colmi_r09_face_rgb565_start");
extern const uint8_t _binary_colmi_r09_face_a8_start[]
    asm("_binary_colmi_r09_face_a8_start");

static uint16_t rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return faculty175_display_rgb888(r, g, b);
}

static int clamp_i(int value, int lo, int hi)
{
    if (value < lo) {
        return lo;
    }
    if (value > hi) {
        return hi;
    }
    return value;
}

static uint64_t now_ms(void)
{
    return (uint64_t)(esp_timer_get_time() / 1000LL);
}

static void centered(const char *text, int y, uint16_t color)
{
    faculty175_display_draw_centered_text(text, y, color);
}

static bool draw_ring_product(int cx, int cy)
{
    faculty175_display_blit_rgb565_masked(
                                          (const uint16_t *)_binary_colmi_r09_face_rgb565_start,
                                          _binary_colmi_r09_face_a8_start,
                                          cx - RING_IMAGE_SIZE / 2,
                                          cy - RING_IMAGE_SIZE / 2,
                                          RING_IMAGE_SIZE,
                                          RING_IMAGE_SIZE);
    return true;
}

static void draw_arc_fraction(int cx, int cy, int radius, float fraction, uint16_t color, int width)
{
    if (fraction < 0.0f) {
        fraction = 0.0f;
    } else if (fraction > 1.0f) {
        fraction = 1.0f;
    }
    const int steps = (int)fmaxf(12.0f, (float)radius * TAU_F * fraction);
    for (int i = 0; i <= steps; ++i) {
        const float p = (float)i / (float)steps * fraction;
        const float a = p * TAU_F - 1.57079632679f;
        const int x = cx + (int)lrintf(cosf(a) * (float)radius);
        const int y = cy + (int)lrintf(sinf(a) * (float)radius);
        faculty175_display_fill_circle(x, y, width / 2, color);
    }
}

static void draw_metric_arc(int cx, int cy, int radius, uint8_t value, uint16_t guide, uint16_t color)
{
    faculty175_display_draw_circle(cx, cy, radius, guide);
    draw_arc_fraction(cx, cy, radius, (float)value / 100.0f, color, 4);
}

static uint16_t load_color(uint8_t score)
{
    if (score >= 76) {
        return rgb(255, 116, 106);
    }
    if (score >= 58) {
        return rgb(255, 196, 104);
    }
    return rgb(116, 224, 152);
}

static uint8_t self_stress_score(const faculty175_ring_vitals_t *vitals)
{
    if (vitals == NULL) {
        return 0;
    }
    int stress = 0;
    bool have_signal = false;
    if (vitals->hrv_valid) {
        stress += 82 - (int)vitals->hrv_ms;
        have_signal = true;
    }
    if (vitals->heart_rate_valid && vitals->heart_rate_bpm > 78) {
        stress += (int)vitals->heart_rate_bpm - 78;
        have_signal = true;
    }
    if (vitals->spo2_valid && vitals->spo2_percent > 0 && vitals->spo2_percent < 94) {
        stress += 12;
        have_signal = true;
    }
    return have_signal ? (uint8_t)clamp_i(stress, 0, 100) : 0;
}

static const char *self_cue(uint8_t score, const faculty175_ring_vitals_t *vitals, bool fresh)
{
    if (!fresh) {
        return "ring waiting";
    }
    if (vitals != NULL && vitals->spo2_valid && vitals->spo2_percent > 0 && vitals->spo2_percent < 94) {
        return "low-spo2";
    }
    if (score >= 76) {
        return "high load";
    }
    if (score >= 58) {
        return "settle";
    }
    return "steady";
}

static void draw_swipe_indicators(int cx, int cy, faculty175_ble_ring_event_t last_event, bool near)
{
    const uint16_t dim = rgb(52, 64, 82);
    const uint16_t on = near ? rgb(116, 224, 152) : rgb(255, 196, 104);
    const uint16_t left = last_event == FACULTY175_BLE_RING_EVENT_SWIPE_PREVIOUS ? on : dim;
    const uint16_t right = last_event == FACULTY175_BLE_RING_EVENT_SWIPE_NEXT ? on : dim;
    const uint16_t up = last_event == FACULTY175_BLE_RING_EVENT_SWIPE_UP ? on : dim;
    const uint16_t down = last_event == FACULTY175_BLE_RING_EVENT_SWIPE_DOWN ? on : dim;

    faculty175_display_draw_line(cx - 42, cy, cx - 22, cy - 14, left);
    faculty175_display_draw_line(cx - 42, cy, cx - 22, cy + 14, left);
    faculty175_display_draw_line(cx + 42, cy, cx + 22, cy - 14, right);
    faculty175_display_draw_line(cx + 42, cy, cx + 22, cy + 14, right);
    faculty175_display_draw_line(cx, cy - 42, cx - 14, cy - 22, up);
    faculty175_display_draw_line(cx, cy - 42, cx + 14, cy - 22, up);
    faculty175_display_draw_line(cx, cy + 42, cx - 14, cy + 22, down);
    faculty175_display_draw_line(cx, cy + 42, cx + 14, cy + 22, down);
    faculty175_display_fill_circle(cx, cy, near ? 8 : 5, near ? on : dim);
}

void faculty175_face_bio_draw(uint32_t anim_ms)
{
    (void)anim_ms;
    const int cx = FACULTY175_LCD_W / 2;
    const int cy = FACULTY175_LCD_H / 2 + 2;
    const uint16_t bg = rgb(4, 7, 12);
    const uint16_t guide = rgb(34, 42, 58);

    faculty175_ring_vitals_t vitals = {};
    const uint64_t now = now_ms();
    const bool have_ring = faculty175_ring_latest_vitals(&vitals);
    const uint64_t age = have_ring && vitals.updated_ms <= now ? now - vitals.updated_ms : UINT64_MAX;
    const bool fresh = have_ring && age <= SELF_RING_STALE_MS;
    const uint8_t stress = fresh ? self_stress_score(&vitals) : 0;
    const uint8_t hrv_load = fresh && vitals.hrv_valid
                                 ? (uint8_t)clamp_i(100 - (int)vitals.hrv_ms, 0, 100)
                                 : 0;
    const uint8_t hr_load = fresh && vitals.heart_rate_valid
                                ? (uint8_t)clamp_i(((int)vitals.heart_rate_bpm - 45) * 100 / 115, 0, 100)
                                : 0;
    const uint8_t spo2_gap = fresh && vitals.spo2_valid
                                 ? (uint8_t)clamp_i((100 - (int)vitals.spo2_percent) * 10, 0, 100)
                                 : 0;
    const uint16_t accent = load_color(stress);

    uint16_t paired_id = 0;
    const bool paired = faculty175_ble_ring_paired(&paired_id);
    const bool near = faculty175_ble_lunasay_ring_near();
    const faculty175_ble_ring_event_t last_event = faculty175_ble_lunasay_ring_last_event();
    faculty175_ble_ring_telem_t ring_samples[1] = {};
    const size_t ring_count = faculty175_ble_ring_telemetry_snapshot(ring_samples, 1);
    const faculty175_ble_ring_telem_t *sample = ring_count > 0 ? &ring_samples[0] : NULL;

    faculty175_display_fill_rgb565(bg);
    faculty175_display_draw_circle(cx, cy, 222, rgb(22, 28, 42));
    faculty175_display_draw_circle(cx, cy, 204, rgb(18, 40, 54));
    draw_metric_arc(cx, cy, 198, stress, guide, accent);
    draw_metric_arc(cx, cy, 170, hrv_load, rgb(28, 36, 48), rgb(128, 176, 255));
    draw_metric_arc(cx, cy, 142, hr_load, rgb(30, 38, 52), rgb(255, 142, 124));
    draw_metric_arc(cx, cy, 116, spo2_gap, rgb(30, 36, 44), rgb(198, 150, 255));

    char line[96];
    centered("R09 RING", 102, rgb(218, 226, 240));
    centered(self_cue(stress, &vitals, fresh), 128, accent);
    (void)draw_ring_product(cx, cy);
    draw_swipe_indicators(cx, cy + 6, last_event, near);

    if (fresh) {
        snprintf(line,
                 sizeof(line),
                 "LOAD %u  HRV %s%u",
                 (unsigned)stress,
                 vitals.hrv_valid ? "" : "?",
                 vitals.hrv_valid ? (unsigned)vitals.hrv_ms : 0u);
        centered(line, 338, accent);
        snprintf(line,
                 sizeof(line),
                 "HR %s%u  SPO2 %s%u",
                 vitals.heart_rate_valid ? "" : "?",
                 vitals.heart_rate_valid ? (unsigned)vitals.heart_rate_bpm : 0u,
                 vitals.spo2_valid ? "" : "?",
                 vitals.spo2_valid ? (unsigned)vitals.spo2_percent : 0u);
        centered(line, 362, rgb(208, 216, 232));
    } else {
        centered(have_ring ? "last vitals stale" : "tap to read vitals", 340, rgb(168, 178, 198));
        centered(paired ? "paired ring link active" : "tap to pair nearest ring", 364, rgb(106, 120, 148));
    }

    snprintf(line,
             sizeof(line),
             "PAIR %s%04x  %s  EVT %s",
             paired ? "" : "?",
             (unsigned)paired_id,
             near ? "NEAR" : "SCAN",
             faculty175_ble_lunasay_ring_event_name(last_event));
    centered(line, 390, rgb(158, 172, 198));

    if (sample != NULL) {
        snprintf(line,
                 sizeof(line),
                 "RSSI %d d%+d  IMU %s",
                 (int)sample->rssi,
                 (int)sample->rssi_delta,
                 sample->local_imu_valid ? "yes" : "no");
        centered(line, 414, rgb(128, 176, 255));
        snprintf(line,
                 sizeof(line),
                 "P %d R %d  M%u %s",
                 (int)sample->pitch_deg,
                 (int)sample->roll_deg,
                 (unsigned)sample->motion_score,
                 sample->gesture);
        centered(line, 438, rgb(208, 216, 232));
    } else {
        centered("no ring advertisements yet", 418, rgb(96, 108, 132));
    }

    centered("LOAD HRV HR SPO2", 456, rgb(96, 108, 132));
    faculty175_display_flush();
}

bool faculty175_face_bio_action(uint32_t seed_ms)
{
    (void)seed_ms;
    uint16_t paired_id = 0;
    if (faculty175_ble_ring_paired(&paired_id)) {
        return faculty175_ble_request_ring_vitals() == ESP_OK;
    }

    faculty175_ble_ring_telem_t ring = {};
    if (faculty175_ble_ring_strongest(&ring)) {
        return faculty175_ble_ring_pair(ring.ring_id) == ESP_OK;
    }

    return faculty175_ble_scan_start(3000u) == ESP_OK;
}
