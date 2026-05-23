#pragma once

#include <cstdint>

/**
 * Optional 6DOF IMU (QMI8658-class: accel + gyro, no magnetometer) on shared I2C.
 * Radar bearing uses gyro integration only — relative heading, not compass north.
 */
bool pm_motion_begin(void);
void pm_motion_tick(uint32_t now_ms);

/** Integrated relative yaw in degrees [0, 360), clockwise since boot / last reset. */
float pm_motion_yaw_deg(void);
/** Set the current facing direction to relative north (0 degrees). */
void pm_motion_zero_yaw(void);
/** Normalized accelerometer vector in device axes. Returns false when the IMU is unavailable. */
bool pm_motion_accel_norm(float *x, float *y, float *z);
/** Raw accelerometer vector in g units. Useful for impact / force estimates. */
bool pm_motion_accel_g(float *x, float *y, float *z);
/** True when a 6DOF part was detected and gyro samples are available. */
bool pm_motion_has_6dof(void);
