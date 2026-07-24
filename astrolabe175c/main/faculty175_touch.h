#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    bool down;
    int16_t x;
    int16_t y;
    uint32_t updated_ms;
} faculty175_touch_state_t;

typedef struct {
    bool ready;
    bool int_active;
    bool active;
    bool down;
    int32_t init_err;
    uint32_t irq_count;
    uint32_t read_count;
    uint32_t read_errors;
    uint32_t invalid_frames;
    uint32_t rejected_events;
    uint32_t valid_points;
    uint32_t last_frame_ms;
    uint32_t updated_ms;
    uint8_t last_points;
    uint8_t last_event;
    uint8_t last_data0;
    uint8_t last_data6;
    int16_t raw_x;
    int16_t raw_y;
    int16_t x;
    int16_t y;
} faculty175_touch_diagnostics_t;

/** Init CST9217 on the shared I2C bus (no-op when touch IC absent). */
esp_err_t faculty175_touch_init(void);

bool faculty175_touch_ready(void);
bool faculty175_touch_int_active(void);

/** Sample up to `max_pts` touch points; returns count (0 when none). */
uint8_t faculty175_touch_sample(int16_t *xs, int16_t *ys, uint8_t max_pts);

void faculty175_touch_state_update(bool down, int16_t x, int16_t y, uint32_t now_ms);
faculty175_touch_state_t faculty175_touch_state_get(void);
void faculty175_touch_diagnostics_get(faculty175_touch_diagnostics_t *out);
