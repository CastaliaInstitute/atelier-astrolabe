#include "faculty175_breath.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"

#define BREATH_CALIBRATION_MS 8000u
#define BREATH_MOTION_HOLD_MS 1500u
#define BREATH_AXIS_RESELECT_MS 10000u
#define BREATH_MIN_INTERVAL_MS 1500u
#define BREATH_MAX_INTERVAL_MS 30000u
#define BREATH_STALE_MS 26000u
#define BREATH_MOTION_RATE_DPS 18.0f
#define BREATH_PHASE_TREND_PER_S 0.040f
#define BREATH_PHASE_HOLD_PER_S 0.018f
#define BREATH_PHASE_TREND_CONFIRM_MS 300u
#define BREATH_PHASE_HOLD_CONFIRM_MS 700u

typedef struct {
    faculty175_breath_status_t public;
    bool initialized;
    bool crossing_armed;
    uint8_t settle_samples;
    uint32_t started_ms;
    uint32_t last_sample_ms;
    uint32_t last_axis_select_ms;
    uint32_t last_breath_ms;
    uint32_t motion_until_ms;
    float last_pitch;
    float last_roll;
    float pitch_baseline;
    float roll_baseline;
    float pitch_filtered;
    float roll_filtered;
    float pitch_energy;
    float roll_energy;
    float amplitude_ema;
    float phase_last_waveform;
    float phase_velocity_ema;
    faculty175_breath_phase_t phase_candidate;
    uint32_t phase_candidate_since_ms;
    uint32_t detected_phase_started_ms;
    float guide_dot_ema;
    float guide_actual_energy_ema;
    float guide_target_energy_ema;
} breath_estimator_t;

static breath_estimator_t s_breath;
static portMUX_TYPE s_breath_lock = portMUX_INITIALIZER_UNLOCKED;
static float s_stream_hz;
static uint32_t s_last_stream_ms;

static float clampf(float value, float lo, float hi)
{
    if (value < lo) {
        return lo;
    }
    if (value > hi) {
        return hi;
    }
    return value;
}

static bool time_before(uint32_t now_ms, uint32_t deadline_ms)
{
    return (int32_t)(now_ms - deadline_ms) < 0;
}

static void select_axis(uint32_t now_ms)
{
    const char current = s_breath.public.axis;
    if (current != 'P' && current != 'R') {
        s_breath.public.axis = s_breath.pitch_energy >= s_breath.roll_energy ? 'P' : 'R';
    } else if (current == 'P' && s_breath.roll_energy > s_breath.pitch_energy * 1.35f) {
        s_breath.public.axis = 'R';
    } else if (current == 'R' && s_breath.pitch_energy > s_breath.roll_energy * 1.35f) {
        s_breath.public.axis = 'P';
    }
    s_breath.last_axis_select_ms = now_ms;
}

static void reset_detected_phase(uint32_t now_ms)
{
    s_breath.public.detected_phase = FACULTY175_BREATH_PHASE_UNKNOWN;
    s_breath.public.detected_phase_ms = 0;
    s_breath.public.phase_velocity = 0.0f;
    s_breath.public.phase_confidence = 0.0f;
    s_breath.phase_candidate = FACULTY175_BREATH_PHASE_UNKNOWN;
    s_breath.phase_candidate_since_ms = now_ms;
    s_breath.detected_phase_started_ms = now_ms;
    s_breath.phase_last_waveform = s_breath.public.waveform;
    s_breath.phase_velocity_ema = 0.0f;
}

static void update_detected_phase(uint32_t now_ms, float dt)
{
    const float instant_velocity = (s_breath.public.waveform - s_breath.phase_last_waveform) / dt;
    const float alpha = dt / (0.45f + dt);
    s_breath.phase_velocity_ema += (instant_velocity - s_breath.phase_velocity_ema) * alpha;
    s_breath.phase_last_waveform = s_breath.public.waveform;
    s_breath.public.phase_velocity = s_breath.phase_velocity_ema;

    faculty175_breath_phase_t candidate = s_breath.phase_candidate;
    const float velocity = s_breath.phase_velocity_ema;
    if (velocity > BREATH_PHASE_TREND_PER_S) {
        candidate = FACULTY175_BREATH_PHASE_INHALE;
    } else if (velocity < -BREATH_PHASE_TREND_PER_S) {
        candidate = FACULTY175_BREATH_PHASE_EXHALE;
    } else if (fabsf(velocity) < BREATH_PHASE_HOLD_PER_S) {
        candidate = FACULTY175_BREATH_PHASE_HOLD;
    }

    if (candidate != s_breath.phase_candidate) {
        s_breath.phase_candidate = candidate;
        s_breath.phase_candidate_since_ms = now_ms;
    }
    const uint32_t confirm_ms = candidate == FACULTY175_BREATH_PHASE_HOLD
                                        ? BREATH_PHASE_HOLD_CONFIRM_MS
                                        : BREATH_PHASE_TREND_CONFIRM_MS;
    if (candidate != FACULTY175_BREATH_PHASE_UNKNOWN &&
        candidate != s_breath.public.detected_phase &&
        now_ms - s_breath.phase_candidate_since_ms >= confirm_ms) {
        s_breath.public.detected_phase = candidate;
        s_breath.detected_phase_started_ms = now_ms;
    }
    s_breath.public.detected_phase_ms = now_ms - s_breath.detected_phase_started_ms;

    float trend_confidence = 0.0f;
    if (s_breath.public.detected_phase == FACULTY175_BREATH_PHASE_HOLD) {
        trend_confidence = 1.0f - clampf(fabsf(velocity) / BREATH_PHASE_TREND_PER_S, 0.0f, 1.0f);
    } else if (s_breath.public.detected_phase != FACULTY175_BREATH_PHASE_UNKNOWN) {
        trend_confidence = clampf((fabsf(velocity) - BREATH_PHASE_HOLD_PER_S) /
                                      (BREATH_PHASE_TREND_PER_S * 3.0f),
                                  0.0f,
                                  1.0f);
    }
    s_breath.public.phase_confidence = trend_confidence * (0.35f + 0.65f * s_breath.public.confidence);
}

void faculty175_breath_reset(void)
{
    portENTER_CRITICAL(&s_breath_lock);
    memset(&s_breath, 0, sizeof(s_breath));
    s_breath.public.state = FACULTY175_BREATH_SENSOR_MISSING;
    s_breath.public.waveform = 0.5f;
    s_breath.public.axis = '-';
    reset_detected_phase(0);
    portEXIT_CRITICAL(&s_breath_lock);
}

void faculty175_breath_update(uint32_t now_ms, bool sample_valid, float pitch_deg, float roll_deg)
{
    portENTER_CRITICAL(&s_breath_lock);
    s_breath.public.updated_ms = now_ms;

    if (!sample_valid) {
        s_breath.public.state = FACULTY175_BREATH_SENSOR_MISSING;
        s_breath.public.waveform = 0.5f;
        reset_detected_phase(now_ms);
        portEXIT_CRITICAL(&s_breath_lock);
        return;
    }

    if (!s_breath.initialized) {
        s_breath.initialized = true;
        s_breath.started_ms = now_ms;
        s_breath.last_sample_ms = now_ms;
        s_breath.last_axis_select_ms = now_ms;
        s_breath.last_pitch = pitch_deg;
        s_breath.last_roll = roll_deg;
        s_breath.pitch_baseline = pitch_deg;
        s_breath.roll_baseline = roll_deg;
        s_breath.public.state = FACULTY175_BREATH_CALIBRATING;
        s_breath.public.waveform = 0.5f;
        s_breath.public.pitch_deg = pitch_deg;
        s_breath.public.roll_deg = roll_deg;
        reset_detected_phase(now_ms);
        portEXIT_CRITICAL(&s_breath_lock);
        return;
    }

    /* The QMI8658 can return an all-zero frame immediately after enable.
     * Continuously rebase during a short settling window so that frame cannot
     * poison the slow baseline and inflate the respiratory amplitude. */
    if (s_breath.settle_samples < 8u) {
        s_breath.settle_samples++;
        s_breath.last_sample_ms = now_ms;
        s_breath.last_pitch = pitch_deg;
        s_breath.last_roll = roll_deg;
        s_breath.pitch_baseline = pitch_deg;
        s_breath.roll_baseline = roll_deg;
        s_breath.pitch_filtered = 0.0f;
        s_breath.roll_filtered = 0.0f;
        s_breath.pitch_energy = 0.0f;
        s_breath.roll_energy = 0.0f;
        s_breath.amplitude_ema = 0.0f;
        s_breath.public.pitch_deg = pitch_deg;
        s_breath.public.roll_deg = roll_deg;
        s_breath.public.signal_deg = 0.0f;
        s_breath.public.amplitude_deg = 0.0f;
        s_breath.public.waveform = 0.5f;
        s_breath.public.state = FACULTY175_BREATH_CALIBRATING;
        reset_detected_phase(now_ms);
        s_breath.public.samples++;
        s_breath.public.calibration_ms = 0;
        if (s_breath.settle_samples == 8u) {
            s_breath.started_ms = now_ms;
        }
        portEXIT_CRITICAL(&s_breath_lock);
        return;
    }

    const uint32_t elapsed_ms = now_ms - s_breath.started_ms;
    const uint32_t delta_ms = now_ms - s_breath.last_sample_ms;
    const float dt = clampf((float)delta_ms / 1000.0f, 0.01f, 0.25f);
    s_breath.last_sample_ms = now_ms;
    s_breath.public.samples++;
    s_breath.public.calibration_ms = elapsed_ms < BREATH_CALIBRATION_MS ? elapsed_ms : BREATH_CALIBRATION_MS;

    const float movement_rate = hypotf(pitch_deg - s_breath.last_pitch,
                                       roll_deg - s_breath.last_roll) / dt;
    s_breath.public.pitch_deg = pitch_deg;
    s_breath.public.roll_deg = roll_deg;
    s_breath.public.motion_rate_dps = movement_rate;
    s_breath.last_pitch = pitch_deg;
    s_breath.last_roll = roll_deg;
    if (movement_rate > BREATH_MOTION_RATE_DPS) {
        s_breath.motion_until_ms = now_ms + BREATH_MOTION_HOLD_MS;
        s_breath.crossing_armed = false;
    }

    const float baseline_alpha = dt / (10.0f + dt);
    s_breath.pitch_baseline += (pitch_deg - s_breath.pitch_baseline) * baseline_alpha;
    s_breath.roll_baseline += (roll_deg - s_breath.roll_baseline) * baseline_alpha;

    const float filter_alpha = dt / (0.35f + dt);
    s_breath.pitch_filtered += ((pitch_deg - s_breath.pitch_baseline) - s_breath.pitch_filtered) * filter_alpha;
    s_breath.roll_filtered += ((roll_deg - s_breath.roll_baseline) - s_breath.roll_filtered) * filter_alpha;

    const float energy_alpha = dt / (6.0f + dt);
    s_breath.pitch_energy += ((s_breath.pitch_filtered * s_breath.pitch_filtered) - s_breath.pitch_energy) * energy_alpha;
    s_breath.roll_energy += ((s_breath.roll_filtered * s_breath.roll_filtered) - s_breath.roll_energy) * energy_alpha;

    if (elapsed_ms >= BREATH_CALIBRATION_MS &&
        (s_breath.public.axis == '-' || now_ms - s_breath.last_axis_select_ms >= BREATH_AXIS_RESELECT_MS)) {
        select_axis(now_ms);
    }

    const float signal = s_breath.public.axis == 'R' ? s_breath.roll_filtered : s_breath.pitch_filtered;
    s_breath.public.signal_deg = signal;
    const float amplitude_alpha = dt / (8.0f + dt);
    s_breath.amplitude_ema += (fabsf(signal) - s_breath.amplitude_ema) * amplitude_alpha;
    const float amplitude = s_breath.amplitude_ema * 1.57f;
    s_breath.public.amplitude_deg = amplitude;
    const float scale = fmaxf(amplitude, 0.06f);
    s_breath.public.waveform = clampf(0.5f + (signal / (2.0f * scale)), 0.0f, 1.0f);

    if (elapsed_ms < BREATH_CALIBRATION_MS) {
        s_breath.public.state = FACULTY175_BREATH_CALIBRATING;
        s_breath.public.confidence = 0.0f;
        reset_detected_phase(now_ms);
        portEXIT_CRITICAL(&s_breath_lock);
        return;
    }

    if (time_before(now_ms, s_breath.motion_until_ms)) {
        s_breath.public.state = FACULTY175_BREATH_MOTION;
        s_breath.public.waveform = 0.5f;
        s_breath.public.confidence *= 0.92f;
        reset_detected_phase(now_ms);
        portEXIT_CRITICAL(&s_breath_lock);
        return;
    }

    s_breath.public.state = FACULTY175_BREATH_TRACKING;
    const float threshold = fmaxf(amplitude * 0.24f, 0.025f);
    if (signal < -threshold) {
        s_breath.crossing_armed = true;
    } else if (s_breath.crossing_armed && signal > threshold) {
        s_breath.crossing_armed = false;
        if (s_breath.last_breath_ms != 0) {
            const uint32_t interval_ms = now_ms - s_breath.last_breath_ms;
            if (interval_ms >= BREATH_MIN_INTERVAL_MS && interval_ms <= BREATH_MAX_INTERVAL_MS) {
                const float instant_bpm = 60000.0f / (float)interval_ms;
                s_breath.public.rate_bpm = s_breath.public.rate_bpm > 0.0f
                                                  ? (s_breath.public.rate_bpm * 0.72f) + (instant_bpm * 0.28f)
                                                  : instant_bpm;
                s_breath.public.breaths++;
            }
        }
        s_breath.last_breath_ms = now_ms;
    }

    const float amplitude_confidence = clampf((amplitude - 0.025f) / 0.18f, 0.0f, 1.0f);
    const float cycle_confidence = clampf((float)s_breath.public.breaths / 4.0f, 0.0f, 1.0f);
    s_breath.public.confidence = (amplitude_confidence * 0.55f) + (cycle_confidence * 0.45f);
    if (s_breath.last_breath_ms == 0 || now_ms - s_breath.last_breath_ms > BREATH_STALE_MS) {
        s_breath.public.confidence *= 0.35f;
    }
    update_detected_phase(now_ms, dt);

    portEXIT_CRITICAL(&s_breath_lock);
}

void faculty175_breath_status(faculty175_breath_status_t *out)
{
    if (out == NULL) {
        return;
    }
    portENTER_CRITICAL(&s_breath_lock);
    *out = s_breath.public;
    portEXIT_CRITICAL(&s_breath_lock);
}

void faculty175_breath_guide_update(faculty175_breath_guide_phase_t phase,
                                    uint32_t phase_ms,
                                    uint32_t cycle,
                                    float target)
{
    portENTER_CRITICAL(&s_breath_lock);
    s_breath.public.guide_phase = phase;
    s_breath.public.guide_phase_ms = phase_ms;
    s_breath.public.guide_cycle = cycle;
    s_breath.public.guide_target = clampf(target, -1.0f, 1.0f);
    if (phase == FACULTY175_BREATH_GUIDE_NONE) {
        s_breath.guide_dot_ema = 0.0f;
        s_breath.guide_actual_energy_ema = 0.0f;
        s_breath.guide_target_energy_ema = 0.0f;
        s_breath.public.guide_alignment = 0.0f;
    } else if (s_breath.public.state == FACULTY175_BREATH_TRACKING) {
        const float actual = (s_breath.public.waveform * 2.0f) - 1.0f;
        const float alpha = 0.02f;
        s_breath.guide_dot_ema += ((actual * target) - s_breath.guide_dot_ema) * alpha;
        s_breath.guide_actual_energy_ema += ((actual * actual) - s_breath.guide_actual_energy_ema) * alpha;
        s_breath.guide_target_energy_ema += ((target * target) - s_breath.guide_target_energy_ema) * alpha;
        const float denom = sqrtf(s_breath.guide_actual_energy_ema * s_breath.guide_target_energy_ema);
        s_breath.public.guide_alignment = denom > 0.001f
                                                 ? clampf(fabsf(s_breath.guide_dot_ema) / denom, 0.0f, 1.0f)
                                                 : 0.0f;
    }
    portEXIT_CRITICAL(&s_breath_lock);
}

const char *faculty175_breath_state_name(faculty175_breath_state_t state)
{
    switch (state) {
    case FACULTY175_BREATH_SENSOR_MISSING:
        return "sensor-missing";
    case FACULTY175_BREATH_CALIBRATING:
        return "calibrating";
    case FACULTY175_BREATH_TRACKING:
        return "tracking";
    case FACULTY175_BREATH_MOTION:
        return "motion";
    default:
        return "unknown";
    }
}

const char *faculty175_breath_phase_name(faculty175_breath_phase_t phase)
{
    switch (phase) {
    case FACULTY175_BREATH_PHASE_INHALE:
        return "inhale";
    case FACULTY175_BREATH_PHASE_HOLD:
        return "hold";
    case FACULTY175_BREATH_PHASE_EXHALE:
        return "exhale";
    default:
        return "uncertain";
    }
}

const char *faculty175_breath_guide_phase_name(faculty175_breath_guide_phase_t phase)
{
    switch (phase) {
    case FACULTY175_BREATH_GUIDE_INHALE:
        return "inhale";
    case FACULTY175_BREATH_GUIDE_HOLD:
        return "hold";
    case FACULTY175_BREATH_GUIDE_EXHALE:
        return "exhale";
    default:
        return "none";
    }
}

bool faculty175_breath_stream_set(float hz)
{
    if (!isfinite(hz) || hz < 0.0f || hz > 25.0f) {
        return false;
    }
    portENTER_CRITICAL(&s_breath_lock);
    s_stream_hz = hz;
    s_last_stream_ms = 0;
    portEXIT_CRITICAL(&s_breath_lock);
    return true;
}

float faculty175_breath_stream_hz(void)
{
    portENTER_CRITICAL(&s_breath_lock);
    const float hz = s_stream_hz;
    portEXIT_CRITICAL(&s_breath_lock);
    return hz;
}

void faculty175_breath_stream_maybe_emit(uint32_t now_ms)
{
    faculty175_breath_status_t status = {};
    float hz = 0.0f;
    bool emit = false;

    portENTER_CRITICAL(&s_breath_lock);
    hz = s_stream_hz;
    if (hz > 0.0f) {
        const uint32_t period_ms = (uint32_t)fmaxf(40.0f, 1000.0f / hz);
        if (s_last_stream_ms == 0 || now_ms - s_last_stream_ms >= period_ms) {
            s_last_stream_ms = now_ms;
            status = s_breath.public;
            emit = true;
        }
    }
    portEXIT_CRITICAL(&s_breath_lock);

    if (!emit) {
        return;
    }
    printf("breath_csv,%lu,%s,%.5f,%.5f,%.5f,%.5f,%.4f,%.2f,%.3f,%c,%.3f,%lu,%lu,%s,%lu,%.3f,%.4f,%s,%lu,%lu,%.4f,%.3f\n",
           (unsigned long)now_ms,
           faculty175_breath_state_name(status.state),
           (double)status.pitch_deg,
           (double)status.roll_deg,
           (double)status.signal_deg,
           (double)status.amplitude_deg,
           (double)status.waveform,
           (double)status.rate_bpm,
           (double)status.confidence,
           status.axis,
           (double)status.motion_rate_dps,
           (unsigned long)status.samples,
           (unsigned long)status.breaths,
           faculty175_breath_phase_name(status.detected_phase),
           (unsigned long)status.detected_phase_ms,
           (double)status.phase_confidence,
           (double)status.phase_velocity,
           faculty175_breath_guide_phase_name(status.guide_phase),
           (unsigned long)status.guide_phase_ms,
           (unsigned long)status.guide_cycle,
           (double)status.guide_target,
           (double)status.guide_alignment);
    fflush(stdout);
}
