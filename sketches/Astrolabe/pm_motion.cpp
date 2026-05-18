#include "pm_motion.h"

#include <Arduino.h>
#include <Wire.h>
#include <cmath>

#include "pin_config.h"

#ifndef MYNAH_IMU_ADDR
#define MYNAH_IMU_ADDR 0x6B
#endif

/** QMI8658 WHO_AM_I */
#ifndef MYNAH_IMU_WHO_AM_I_REG
#define MYNAH_IMU_WHO_AM_I_REG 0x00
#endif
#ifndef MYNAH_IMU_WHO_AM_I_VAL
#define MYNAH_IMU_WHO_AM_I_VAL 0x05
#endif
#ifndef MYNAH_IMU_CTRL1_REG
#define MYNAH_IMU_CTRL1_REG 0x02
#endif
#ifndef MYNAH_IMU_CTRL2_REG
#define MYNAH_IMU_CTRL2_REG 0x03
#endif
#ifndef MYNAH_IMU_CTRL3_REG
#define MYNAH_IMU_CTRL3_REG 0x04
#endif
#ifndef MYNAH_IMU_GYRO_X_L_REG
#define MYNAH_IMU_GYRO_X_L_REG 0x35
#endif

static bool s_has_gyro = false;
static float s_yaw_deg = 0.f;
static uint32_t s_last_ms = 0;

static bool imu_read(uint8_t reg, uint8_t *out, size_t len) {
  Wire.beginTransmission(static_cast<uint8_t>(MYNAH_IMU_ADDR));
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  const size_t got = Wire.requestFrom(static_cast<uint8_t>(MYNAH_IMU_ADDR), len);
  if (got != len) {
    return false;
  }
  for (size_t i = 0; i < len; ++i) {
    out[i] = static_cast<uint8_t>(Wire.read());
  }
  return true;
}

static bool imu_write(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(static_cast<uint8_t>(MYNAH_IMU_ADDR));
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

bool pm_motion_begin(void) {
#ifdef ASTROLABE_QEMU
  return false;
#else
  uint8_t who = 0;
  if (!imu_read(MYNAH_IMU_WHO_AM_I_REG, &who, 1) || who != MYNAH_IMU_WHO_AM_I_VAL) {
    s_has_gyro = false;
    return false;
  }
  (void)imu_write(MYNAH_IMU_CTRL1_REG, 0x60);
  (void)imu_write(MYNAH_IMU_CTRL2_REG, 0x13);
  (void)imu_write(MYNAH_IMU_CTRL3_REG, 0x43);
  s_has_gyro = true;
  s_yaw_deg = 0.f;
  s_last_ms = millis();
  return true;
#endif
}

void pm_motion_tick(uint32_t now_ms) {
  if (!s_has_gyro) {
    return;
  }
  if (s_last_ms == 0) {
    s_last_ms = now_ms;
    return;
  }
  const float dt = static_cast<float>(now_ms - s_last_ms) * 0.001f;
  s_last_ms = now_ms;
  if (dt <= 0.f || dt > 0.5f) {
    return;
  }

  uint8_t raw[6] = {};
  if (!imu_read(MYNAH_IMU_GYRO_X_L_REG, raw, 6)) {
    return;
  }
  const int16_t gz = static_cast<int16_t>(static_cast<uint16_t>(raw[4]) | (static_cast<uint16_t>(raw[5]) << 8));
  /** QMI8658 ±512 dps scale → deg/s */
  const float gz_dps = static_cast<float>(gz) * (512.f / 32768.f);
  s_yaw_deg += gz_dps * dt;
  while (s_yaw_deg < 0.f) {
    s_yaw_deg += 360.f;
  }
  while (s_yaw_deg >= 360.f) {
    s_yaw_deg -= 360.f;
  }
}

float pm_motion_yaw_deg(void) { return s_yaw_deg; }

bool pm_motion_has_gyro(void) { return s_has_gyro; }
