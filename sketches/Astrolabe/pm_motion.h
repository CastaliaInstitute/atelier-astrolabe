#pragma once

#include <cstdint>

/** Optional QMI8658 (or compatible) gyro on shared I2C. */
bool pm_motion_begin(void);
void pm_motion_tick(uint32_t now_ms);

/** Integrated yaw in degrees [0, 360), clockwise from boot heading. */
float pm_motion_yaw_deg(void);
bool pm_motion_has_gyro(void);
