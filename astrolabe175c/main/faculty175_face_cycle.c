#include <math.h>
#include <stdio.h>

#include "esp_timer.h"

#include "faculty175_board.h"
#include "faculty175_cycle_health.h"
#include "faculty175_ring.h"

#define TAU_F 6.28318530718f

static uint16_t c(uint8_t r, uint8_t g, uint8_t b)
{
    return faculty175_display_rgb888(r, g, b);
}

static void centered(const char *text, int y, uint16_t color)
{
    faculty175_display_draw_centered_text(text, y, color);
}

static uint16_t phase_color(faculty175_cycle_phase_t phase)
{
    switch (phase) {
        case FACULTY175_CYCLE_PHASE_MENSTRUATION: return c(232, 80, 116);
        case FACULTY175_CYCLE_PHASE_FOLLICULAR: return c(134, 205, 170);
        case FACULTY175_CYCLE_PHASE_OVULATION: return c(255, 202, 88);
        case FACULTY175_CYCLE_PHASE_LUTEAL: return c(166, 138, 224);
        default: return c(150, 162, 188);
    }
}

static void draw_cycle_dial(int cx, int cy, const faculty175_cycle_health_status_t *status)
{
    const uint8_t length = status->cycle_length == 0 ? 28 : status->cycle_length;
    for (uint8_t i = 0; i < length; ++i) {
        const float angle = ((float)i / (float)length) * TAU_F - 1.57079632679f;
        const int x = cx + (int)lrintf(cosf(angle) * 175.0f);
        const int y = cy + (int)lrintf(sinf(angle) * 175.0f);
        faculty175_cycle_phase_t phase = FACULTY175_CYCLE_PHASE_LUTEAL;
        const uint8_t day = i + 1u;
        if (day <= status->period_length) phase = FACULTY175_CYCLE_PHASE_MENSTRUATION;
        else if (day == (uint8_t)(length - 13u)) phase = FACULTY175_CYCLE_PHASE_OVULATION;
        else if (day < (uint8_t)(length - 13u)) phase = FACULTY175_CYCLE_PHASE_FOLLICULAR;
        const bool current = status->configured && day == status->day;
        faculty175_display_fill_circle(x, y, current ? 7 : 4, phase_color(phase));
        if (current) faculty175_display_draw_circle(x, y, 10, c(255, 246, 236));
    }
}

void faculty175_face_cycle_draw(uint32_t anim_ms)
{
    (void)anim_ms;
    const int cx = FACULTY175_LCD_W / 2;
    const int cy = FACULTY175_LCD_H / 2 - 4;
    faculty175_cycle_health_status_t cycle = {};
    (void)faculty175_cycle_health_status(&cycle);
    faculty175_ring_vitals_t vitals = {};
    const bool have_vitals = faculty175_ring_latest_vitals(&vitals);
    const uint64_t now_ms = (uint64_t)esp_timer_get_time() / 1000u;
    const bool fresh = have_vitals && vitals.updated_ms <= now_ms && now_ms - vitals.updated_ms <= 15u * 60u * 1000u;

    faculty175_display_fill_rgb565(c(9, 5, 17));
    faculty175_display_draw_circle(cx, cy, 212, c(58, 35, 70));
    faculty175_display_draw_circle(cx, cy, 191, c(38, 25, 57));
    draw_cycle_dial(cx, cy, &cycle);
    faculty175_display_fill_circle(cx, cy, 108, c(20, 12, 32));
    faculty175_display_draw_circle(cx, cy, 108, phase_color(cycle.phase));

    char line[64];
    centered("CYCLE", 92, c(210, 196, 232));
    if (cycle.configured) {
        snprintf(line, sizeof(line), "DAY %02u / %02u", cycle.day, cycle.cycle_length);
        centered(line, 136, c(255, 244, 250));
        centered(faculty175_cycle_health_phase_label(cycle.phase), 168, phase_color(cycle.phase));
        snprintf(line, sizeof(line), "start %s", cycle.start_date);
        centered(line, 196, c(168, 150, 190));
    } else {
        centered("SET YOUR START DATE", 142, c(255, 224, 238));
        centered("cycle start YYYY-MM-DD", 172, c(174, 150, 190));
        centered("estimate only", 198, c(136, 120, 154));
    }

    if (fresh) {
        snprintf(line, sizeof(line), "RING  HR %u   HRV %u   SpO2 %u",
                 vitals.heart_rate_valid ? vitals.heart_rate_bpm : 0,
                 vitals.hrv_valid ? vitals.hrv_ms : 0,
                 vitals.spo2_valid ? vitals.spo2_percent : 0);
        centered(line, 310, c(192, 224, 222));
    } else {
        centered("RING BIOMETRICS WAITING", 310, c(132, 158, 174));
    }
    centered("TAP LEFT: START   TAP RIGHT: STOP", 340, c(166, 140, 180));
    centered("CYCLE ESTIMATE · NOT MEDICAL ADVICE", 364, c(126, 108, 144));
    faculty175_display_flush();
}
