#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    FACULTY175_BREATH_SENSOR_MISSING = 0,
    FACULTY175_BREATH_CALIBRATING,
    FACULTY175_BREATH_TRACKING,
    FACULTY175_BREATH_MOTION,
} faculty175_breath_state_t;

typedef enum {
    FACULTY175_BREATH_GUIDE_NONE = 0,
    FACULTY175_BREATH_GUIDE_INHALE,
    FACULTY175_BREATH_GUIDE_HOLD,
    FACULTY175_BREATH_GUIDE_EXHALE,
} faculty175_breath_guide_phase_t;

typedef enum {
    FACULTY175_BREATH_PHASE_UNKNOWN = 0,
    FACULTY175_BREATH_PHASE_INHALE,
    FACULTY175_BREATH_PHASE_HOLD,
    FACULTY175_BREATH_PHASE_EXHALE,
} faculty175_breath_phase_t;

typedef struct {
    faculty175_breath_state_t state;
    float waveform;
    float rate_bpm;
    float confidence;
    float amplitude_deg;
    float pitch_deg;
    float roll_deg;
    float signal_deg;
    float motion_rate_dps;
    float guide_target;
    float guide_alignment;
    float phase_velocity;
    float phase_confidence;
    faculty175_breath_phase_t detected_phase;
    uint32_t detected_phase_ms;
    faculty175_breath_guide_phase_t guide_phase;
    uint32_t guide_phase_ms;
    uint32_t guide_cycle;
    char axis;
    uint32_t samples;
    uint32_t breaths;
    uint32_t calibration_ms;
    uint32_t updated_ms;
} faculty175_breath_status_t;

void faculty175_breath_reset(void);
void faculty175_breath_update(uint32_t now_ms, bool sample_valid, float pitch_deg, float roll_deg);
void faculty175_breath_guide_update(faculty175_breath_guide_phase_t phase,
                                    uint32_t phase_ms,
                                    uint32_t cycle,
                                    float target);
void faculty175_breath_status(faculty175_breath_status_t *out);
const char *faculty175_breath_state_name(faculty175_breath_state_t state);
const char *faculty175_breath_phase_name(faculty175_breath_phase_t phase);
const char *faculty175_breath_guide_phase_name(faculty175_breath_guide_phase_t phase);
bool faculty175_breath_stream_set(float hz);
float faculty175_breath_stream_hz(void);
void faculty175_breath_stream_maybe_emit(uint32_t now_ms);
