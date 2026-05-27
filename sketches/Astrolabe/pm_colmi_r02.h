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

bool pm_colmi_r02_begin(void);
void pm_colmi_r02_stop(void);
void pm_colmi_r02_tick(uint32_t now_ms);
bool pm_colmi_r02_ready(void);
bool pm_colmi_r02_streaming(void);
bool pm_colmi_r02_accel_g(PmColmiR02AccelSample *out);
const char *pm_colmi_r02_status_label(void);
