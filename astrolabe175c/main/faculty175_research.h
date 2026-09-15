#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef enum {
    FACULTY175_RESEARCH_OFF = 0,
    FACULTY175_RESEARCH_READY,
    FACULTY175_RESEARCH_PENDING,
    FACULTY175_RESEARCH_EXPORTED,
    FACULTY175_RESEARCH_PAUSED,
    FACULTY175_RESEARCH_ERROR,
} faculty175_research_state_t;

typedef struct {
    bool consent_enabled;
    bool pending;
    faculty175_research_state_t state;
    char consent_version[24];
    char last_mood[16];
    char last_feedback_face[16];
    char last_feedback_rating[12];
    uint16_t local_feedback_count;
} faculty175_research_status_t;

void faculty175_research_init(void);
esp_err_t faculty175_research_set_consent(bool enabled, const char *consent_version);
bool faculty175_research_consent_enabled(void);
esp_err_t faculty175_research_record_mood(const char *mood, uint8_t arousal, uint8_t valence);
esp_err_t faculty175_research_record_feedback(const char *face,
                                              const char *rating,
                                              const char *reading_date);
/** Erase local face feedback, its pending export, and its resonance counters. */
esp_err_t faculty175_research_clear_local_feedback(void);
esp_err_t faculty175_research_resonance_json(char *out, size_t cap);
void faculty175_research_poll(void);
void faculty175_research_status(faculty175_research_status_t *out);
const char *faculty175_research_state_label(faculty175_research_state_t state);
