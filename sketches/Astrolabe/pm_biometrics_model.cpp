#include "pm_biometrics_model.h"

#include <Arduino.h>
#include <WiFi.h>
#include <cmath>
#include <cstdio>

#include "pm_audio_analyzer.h"
#include "pm_motion.h"
#include "pm_presence.h"
#include "pm_wifi_ntp.h"

namespace {

PmBiometricsEstimate s_est;
float s_last_ax = 0.f;
float s_last_ay = 0.f;
float s_last_az = 1.f;
bool s_have_accel = false;

float clamp01(float v) {
  if (v < 0.f) return 0.f;
  if (v > 1.f) return 1.f;
  return v;
}

float ema(float prev, float next, float a) {
  return prev * (1.f - a) + next * a;
}

float wifi_quality(int rssi) {
  if (rssi <= -95) return 0.f;
  if (rssi >= -45) return 1.f;
  return static_cast<float>(rssi + 95) / 50.f;
}

float ble_quality(size_t peers, bool ready) {
  if (!ready && peers == 0) return 0.f;
  return clamp01((static_cast<float>(peers) + (ready ? 1.f : 0.f)) / 6.f);
}

}  // namespace

void pm_biometrics_model_tick(uint32_t now_ms) {
  pm_motion_tick(now_ms);
  pm_presence_tick(now_ms);

  s_est.wifi = pm_wifi_connected();
  s_est.wifi_rssi_dbm = s_est.wifi ? WiFi.RSSI() : -127;
  s_est.ble_ready = pm_presence_ble_is_ready();
  s_est.ble_peers = pm_presence_peer_count();

  float ax = 0.f;
  float ay = 0.f;
  float az = 0.f;
  s_est.imu = pm_motion_accel_g(&ax, &ay, &az);
  float jerk = 0.f;
  float posture = 0.f;
  if (s_est.imu) {
    if (s_have_accel) {
      const float dx = ax - s_last_ax;
      const float dy = ay - s_last_ay;
      const float dz = az - s_last_az;
      jerk = sqrtf(dx * dx + dy * dy + dz * dz);
    }
    s_last_ax = ax;
    s_last_ay = ay;
    s_last_az = az;
    s_have_accel = true;
    const float mag = sqrtf(ax * ax + ay * ay + az * az);
    posture = 1.f - clamp01(fabsf(mag - 1.f) * 1.8f);
  } else {
    s_have_accel = false;
  }

  const float level = pm_audio_analyzer_get_level();
  PmAudioPitch pitch = {};
  pm_audio_analyzer_get_pitch(&pitch);
  s_est.audio = level > 0.01f || pitch.valid;

  const float wifi_q = wifi_quality(s_est.wifi_rssi_dbm);
  const float ble_q = ble_quality(s_est.ble_peers, s_est.ble_ready);
  const float audio_breath = clamp01(level * 1.7f + (pitch.valid ? pitch.confidence * 0.18f : 0.f));
  const float movement = clamp01(jerk * 2.8f);
  const float stillness = s_est.imu ? (1.f - movement) * posture : 0.35f;

  s_est.presence = ema(s_est.presence, clamp01(wifi_q * 0.45f + ble_q * 0.55f), 0.16f);
  s_est.motion = ema(s_est.motion, movement, 0.28f);
  s_est.breath = ema(s_est.breath, audio_breath, 0.22f);
  s_est.grounding = ema(s_est.grounding, clamp01(stillness * 0.7f + wifi_q * 0.15f + ble_q * 0.15f), 0.18f);
  s_est.arousal = ema(s_est.arousal, clamp01(movement * 0.54f + audio_breath * 0.34f + (1.f - stillness) * 0.12f),
                      0.2f);
  s_est.coherence = ema(s_est.coherence,
                        clamp01((1.f - fabsf(s_est.breath - s_est.motion)) * 0.42f + s_est.grounding * 0.35f +
                                s_est.presence * 0.23f),
                        0.14f);

  float q = 0.f;
  q += s_est.wifi ? 0.22f : 0.f;
  q += (s_est.ble_ready || s_est.ble_peers > 0) ? 0.22f : 0.f;
  q += s_est.imu ? 0.28f : 0.f;
  q += s_est.audio ? 0.28f : 0.f;
  s_est.signal_quality = ema(s_est.signal_quality, q, 0.18f);
}

void pm_biometrics_model_estimate(PmBiometricsEstimate *out) {
  if (!out) return;
  *out = s_est;
}

void pm_biometrics_model_format(const PmBiometricsEstimate *e, char *out, size_t cap) {
  if (!e || !out || cap == 0) return;
  snprintf(out, cap,
           "presence %.0f%%, motion %.0f%%, breath %.0f%%, coherence %.0f%%, arousal %.0f%%, grounding %.0f%%, "
           "signal quality %.0f%%; WiFi %s %d dBm; BLE %u peer%s %s; IMU %s; audio %s",
           static_cast<double>(e->presence * 100.f), static_cast<double>(e->motion * 100.f),
           static_cast<double>(e->breath * 100.f), static_cast<double>(e->coherence * 100.f),
           static_cast<double>(e->arousal * 100.f), static_cast<double>(e->grounding * 100.f),
           static_cast<double>(e->signal_quality * 100.f), e->wifi ? "on" : "off", e->wifi_rssi_dbm,
           static_cast<unsigned>(e->ble_peers), e->ble_peers == 1 ? "" : "s",
           e->ble_ready ? "ready" : "not ready", e->imu ? "ready" : "missing", e->audio ? "active" : "quiet");
}
