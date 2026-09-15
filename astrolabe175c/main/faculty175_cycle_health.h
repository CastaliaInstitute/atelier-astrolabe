#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef enum {
    FACULTY175_CYCLE_PHASE_UNKNOWN = 0,
    FACULTY175_CYCLE_PHASE_MENSTRUATION,
    FACULTY175_CYCLE_PHASE_FOLLICULAR,
    FACULTY175_CYCLE_PHASE_OVULATION,
    FACULTY175_CYCLE_PHASE_LUTEAL,
} faculty175_cycle_phase_t;

typedef struct {
    bool configured;
    bool time_valid;
    uint8_t day; /* one-based cycle day */
    uint8_t cycle_length;
    uint8_t period_length;
    faculty175_cycle_phase_t phase;
    char start_date[11]; /* YYYY-MM-DD */
} faculty175_cycle_health_status_t;

esp_err_t faculty175_cycle_health_status(faculty175_cycle_health_status_t *out);
const char *faculty175_cycle_health_phase_label(faculty175_cycle_phase_t phase);
esp_err_t faculty175_cycle_health_set_start(const char *yyyy_mm_dd);
esp_err_t faculty175_cycle_health_set_lengths(uint8_t cycle_length, uint8_t period_length);
esp_err_t faculty175_cycle_health_mark_bleeding_started_today(void);
esp_err_t faculty175_cycle_health_mark_bleeding_stopped_today(void);
/** Serial configuration: cycle status|start YYYY-MM-DD|length 21..45|period 1..14|clear */
bool faculty175_cycle_health_handle(const char *line);
