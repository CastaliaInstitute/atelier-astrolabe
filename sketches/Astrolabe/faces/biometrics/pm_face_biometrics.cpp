#include "faces/biometrics/pm_face_biometrics.h"

#include <Arduino_GFX_Library.h>
#include <cmath>
#include <cstdio>

#include "faces/shared/pm_face_draw.h"
#include "pm_audio_analyzer.h"
#include "pm_biometrics_model.h"
#include "pm_display.h"
#include "pm_presence.h"
#include "pm_wifi_ntp.h"

namespace {

uint32_t s_last_tick_ms = 0;
float s_pulse = 0.f;

uint16_t mix(uint8_t r, uint8_t g, uint8_t b, float gain) {
  if (gain < 0.f) gain = 0.f;
  if (gain > 1.f) gain = 1.f;
  return pm_gfx->color565(static_cast<uint8_t>(r * gain), static_cast<uint8_t>(g * gain),
                          static_cast<uint8_t>(b * gain));
}

void draw_metric(const char *label, float value, int x, int y, uint16_t color) {
  constexpr int w = 96;
  constexpr int h = 8;
  pm_gfx->drawRect(x, y, w, h, pm_gfx->color565(40, 48, 58));
  pm_gfx->fillRect(x + 1, y + 1, static_cast<int>((w - 2) * value), h - 2, color);
  pm_gfx->setTextSize(1, 1);
  pm_gfx->setTextColor(pm_gfx->color565(160, 172, 184));
  pm_gfx->setCursor(x, y - 14);
  pm_gfx->print(label);
}

void draw_sensor_node(int cx, int cy, float angle, const char *label, bool active, uint16_t col) {
  const int r = 150;
  const int x = cx + static_cast<int>(lrintf(cosf(angle) * r));
  const int y = cy + static_cast<int>(lrintf(sinf(angle) * r));
  pm_gfx->drawLine(cx, cy, x, y, active ? col : pm_gfx->color565(34, 40, 48));
  pm_gfx->fillCircle(x, y, active ? 11 : 7, active ? col : pm_gfx->color565(42, 48, 56));
  pm_gfx->drawCircle(x, y, active ? 13 : 9, pm_gfx->color565(190, 205, 214));
  pm_gfx->setTextSize(1, 1);
  pm_gfx->setTextColor(active ? pm_gfx->color565(224, 234, 238) : pm_gfx->color565(96, 106, 116));
  int16_t x1, y1;
  uint16_t tw, th;
  pm_gfx->getTextBounds(label, 0, 0, &x1, &y1, &tw, &th);
  pm_gfx->setCursor(x - static_cast<int>(tw) / 2, y + 18);
  pm_gfx->print(label);
}

}  // namespace

void pm_face_biometrics_on_enter(void) {
  s_last_tick_ms = 0;
  pm_audio_analyzer_reset();
  (void)pm_audio_analyzer_mic_begin();
  if (!pm_presence_ble_begin()) {
    pm_wifi_pause_for_ble();
    if (!pm_presence_ble_begin()) {
      pm_presence_seed_demo_peers(millis());
    }
  }
  pm_presence_ble_set_radar_active(true);
}

void pm_face_biometrics_on_leave(void) {
  pm_audio_analyzer_mic_end();
  pm_presence_ble_set_radar_active(false);
  pm_presence_ble_end();
  pm_wifi_resume_after_ble();
}

void pm_face_biometrics_pause_ble_for_voice(void) {
  pm_presence_ble_set_radar_active(false);
  pm_presence_ble_end();
  pm_wifi_resume_after_ble();
}

bool pm_face_biometrics_anim_tick(uint32_t now_ms) {
  pm_audio_analyzer_tick();
  pm_biometrics_model_tick(now_ms);
  s_pulse = fmodf(static_cast<float>(now_ms) * 0.0016f, 1.f);
  s_last_tick_ms = now_ms;
  return true;
}

void pm_face_biometrics_draw(void) {
  if (s_last_tick_ms == 0) {
    (void)pm_face_biometrics_anim_tick(millis());
  }

  PmBiometricsEstimate e = {};
  pm_biometrics_model_estimate(&e);

  pm_gfx->fillScreen(pm_gfx->color565(9, 11, 14));
  const int cx = pm_face_lcd_cx;
  const int cy = pm_face_lcd_cy;
  const uint16_t teal = pm_gfx->color565(94, 224, 202);
  const uint16_t rose = pm_gfx->color565(255, 118, 146);
  const uint16_t gold = pm_gfx->color565(238, 198, 108);
  const uint16_t blue = pm_gfx->color565(114, 174, 255);
  const uint16_t dim = pm_gfx->color565(114, 126, 138);

  pm_gfx->drawCircle(cx, cy, 172, pm_gfx->color565(28, 34, 42));
  pm_gfx->drawCircle(cx, cy, 128, pm_gfx->color565(22, 28, 36));
  pm_gfx->drawCircle(cx, cy, 86, pm_gfx->color565(18, 24, 32));

  draw_sensor_node(cx, cy, -1.5708f, "WiFi", e.wifi, teal);
  draw_sensor_node(cx, cy, 0.f, "BLE", e.ble_ready || e.ble_peers > 0, blue);
  draw_sensor_node(cx, cy, 1.5708f, "IMU", e.imu, gold);
  draw_sensor_node(cx, cy, 3.14159f, "AUD", e.audio, rose);

  const int aura_r = 42 + static_cast<int>(lrintf(e.coherence * 34.f + sinf(s_pulse * 6.28318f) * 4.f));
  pm_gfx->fillCircle(cx, cy, aura_r + 12, mix(28, 80, 78, 0.32f + e.presence * 0.35f));
  pm_gfx->fillCircle(cx, cy, aura_r, mix(72, 205, 188, 0.5f + e.coherence * 0.45f));
  pm_gfx->fillCircle(cx - aura_r / 3, cy - aura_r / 3, aura_r / 4, pm_gfx->color565(226, 252, 246));
  pm_gfx->drawCircle(cx, cy, aura_r + 2, pm_gfx->color565(218, 238, 232));

  pm_face_draw_centered_line("BIOMETRICS", 36, teal, 2, 2);
  pm_face_draw_centered_line("sensor inference", 64, dim, 1, 1);

  char center[32];
  snprintf(center, sizeof(center), "coh %.0f%%", static_cast<double>(e.coherence * 100.f));
  pm_face_draw_centered_line(center, cy - 11, pm_gfx->color565(8, 15, 16), 1, 1);

  draw_metric("presence", e.presence, 62, 350, teal);
  draw_metric("breath", e.breath, 62, 386, rose);
  draw_metric("arousal", e.arousal, 306, 350, gold);
  draw_metric("ground", e.grounding, 306, 386, blue);

  char footer[72];
  snprintf(footer, sizeof(footer), "WiFi %d dBm  BLE %u  quality %.0f%%", e.wifi_rssi_dbm,
           static_cast<unsigned>(e.ble_peers), static_cast<double>(e.signal_quality * 100.f));
  pm_face_draw_centered_line(footer, 424, dim, 1, 1);
}

void pm_face_biometrics_format_prompt_state(char *out, size_t cap) {
  PmBiometricsEstimate e = {};
  pm_biometrics_model_estimate(&e);
  pm_biometrics_model_format(&e, out, cap);
}
