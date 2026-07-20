#pragma once

#include <cstdint>

struct PmColmiR02AccelSample {
  float x_g = 0.f;
  float y_g = 0.f;
  float z_g = 0.f;
  int16_t raw_x = 0;
  int16_t raw_y = 0;
  int16_t raw_z = 0;
  uint32_t age_ms = 0;
};

struct PmColmiR02Wellness {
  bool battery_valid = false;
  uint8_t battery_percent = 0;
  bool charging = false;

  bool heart_rate_valid = false;
  uint8_t heart_rate_bpm = 0;

  bool spo2_valid = false;
  uint8_t spo2_percent = 0;

  bool hrv_valid = false;
  uint8_t hrv_ms = 0;

  bool stress_valid = false;
  uint8_t stress = 0;

  bool sleep_valid = false;
  uint16_t sleep_total_min = 0;
  uint16_t sleep_light_min = 0;
  uint16_t sleep_deep_min = 0;
  uint16_t sleep_rem_min = 0;
  uint16_t sleep_awake_min = 0;

  uint32_t age_ms = 0;
};

bool pm_colmi_r02_begin(void);
void pm_colmi_r02_stop(void);
void pm_colmi_r02_tick(uint32_t now_ms);
bool pm_colmi_r02_ready(void);
bool pm_colmi_r02_streaming(void);
bool pm_colmi_r02_accel_g(PmColmiR02AccelSample *out);
bool pm_colmi_r02_wellness(PmColmiR02Wellness *out);
const char *pm_colmi_r02_status_label(void);
