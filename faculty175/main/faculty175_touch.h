#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/** Init CST9217 on the shared I2C bus (no-op when touch IC absent). */
esp_err_t faculty175_touch_init(void);

bool faculty175_touch_ready(void);
bool faculty175_touch_int_active(void);

/** Sample up to `max_pts` touch points; returns count (0 when none). */
uint8_t faculty175_touch_sample(int16_t *xs, int16_t *ys, uint8_t max_pts);
