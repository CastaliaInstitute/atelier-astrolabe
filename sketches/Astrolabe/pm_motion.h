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
/** True when a 6DOF part was detected and gyro samples are available. */
bool pm_motion_has_6dof(void);
