#pragma once

#include <Arduino.h>
#include <stdint.h>

bool pm_touch_begin();

/** Sample capacitive touch: returns point count 0..max_pts; fills xs/ys in screen coordinates. */
uint8_t pm_touch_sample(int16_t *xs, int16_t *ys, uint8_t max_pts);

/** QA / automation: override the next touch samples until cleared. */
void pm_touch_inject_set(int16_t x, int16_t y);
void pm_touch_inject_clear(void);
bool pm_touch_inject_active(void);
