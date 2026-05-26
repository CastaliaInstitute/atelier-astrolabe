#pragma once

#include <cstddef>
#include <cstdint>

struct PmBiometricsEstimate {
  float presence = 0.f;
  float motion = 0.f;
  float breath = 0.f;
  float coherence = 0.f;
  float arousal = 0.f;
  float grounding = 0.f;
  float eeg_focus = 0.f;
  float hrv_balance = 0.f;
  float attention = 0.f;
  float readiness = 0.f;
  float signal_quality = 0.f;
  int wifi_rssi_dbm = -127;
  size_t ble_peers = 0;
  bool wifi = false;
  bool ble_ready = false;
  bool imu = false;
  bool audio = false;
};

void pm_biometrics_model_tick(uint32_t now_ms);
void pm_biometrics_model_estimate(PmBiometricsEstimate *out);
void pm_biometrics_model_format(const PmBiometricsEstimate *e, char *out, size_t cap);
