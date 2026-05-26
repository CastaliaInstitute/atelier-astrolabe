#include "faces/biometrics/pm_face_biometrics.h"

#include <Arduino.h>
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

constexpr int kEnsoSteps = 224;
constexpr int kEnsoBrushChunk = 32;

uint32_t s_last_tick_ms = 0;
uint32_t s_brush_seed = 0x5e570u;
int s_drawn_step = 0;
bool s_canvas_prepared = false;
float s_pulse = 0.f;
int s_lens = 0;

constexpr const char *kLensLabels[] = {"ATTENTION", "READINESS", "EEG", "HRV"};

float lens_value(const PmBiometricsEstimate &e) {
  switch (s_lens) {
    case 1:
      return e.readiness;
    case 2:
      return e.eeg_focus;
    case 3:
      return e.hrv_balance;
    default:
      return e.attention;
  }
}

float hash01(int n) {
  uint32_t x = static_cast<uint32_t>(n) * 747796405u + 2891336453u;
  x = ((x >> ((x >> 28u) + 4u)) ^ x) * 277803737u;
  x = (x >> 22u) ^ x;
  return static_cast<float>(x & 0xffffu) / 65535.f;
}

uint16_t shade_color(float attention, float shade) {
  if (shade < 0.18f) shade = 0.18f;
  if (shade > 1.f) shade = 1.f;
  if (attention < 0.5f) {
    const float t = attention * 2.f;
    const uint8_t r = static_cast<uint8_t>((255.f * (1.f - t) + 238.f * t) * shade);
    const uint8_t g = static_cast<uint8_t>((118.f * (1.f - t) + 198.f * t) * shade);
    const uint8_t b = static_cast<uint8_t>((146.f * (1.f - t) + 108.f * t) * shade);
    return pm_gfx->color565(r, g, b);
  }
  const float t = (attention - 0.5f) * 2.f;
  const uint8_t r = static_cast<uint8_t>((238.f * (1.f - t) + 94.f * t) * shade);
  const uint8_t g = static_cast<uint8_t>((198.f * (1.f - t) + 224.f * t) * shade);
  const uint8_t b = static_cast<uint8_t>((108.f * (1.f - t) + 202.f * t) * shade);
  return pm_gfx->color565(r, g, b);
}

void stroke_segment(int x0, int y0, int x1, int y1, float nx, float ny, int half_w, uint16_t col) {
  for (int w = -half_w; w <= half_w; ++w) {
    const int ox = static_cast<int>(lrintf(nx * static_cast<float>(w)));
    const int oy = static_cast<int>(lrintf(ny * static_cast<float>(w)));
    pm_gfx->drawLine(x0 + ox, y0 + oy, x1 + ox, y1 + oy, col);
  }
}

float brush_pressure(float t) {
  if (t < 0.f) t = 0.f;
  if (t > 1.f) t = 1.f;
  const float hand_weight = expf(-1.55f * t);
  const float belly = sinf(t * 3.14159f) * 0.10f;
  return 0.12f + hand_weight * 0.86f + belly;
}

float bristle_ink(float t, float initial_ink, float spend_rate, float bristle_seed) {
  if (t < 0.f) t = 0.f;
  if (t > 1.f) t = 1.f;
  const float loaded_start = 0.52f * expf(-5.2f * t);
  const float tooth = 0.035f * sinf(t * 57.3f + bristle_seed * 6.28318f);
  const float late_reserve = 0.10f * expf(-2.8f * (1.f - t));
  const float late_scrape = 0.045f * expf(-18.f * (1.f - t)) * sinf(t * 143.6f + bristle_seed * 9.7f);
  float ink = initial_ink * expf(-spend_rate * t) + loaded_start + late_reserve + tooth + late_scrape;
  if (ink < 0.f) ink = 0.f;
  if (ink > 1.f) ink = 1.f;
  return ink;
}

float enso_clockwise_angle(float deg_clockwise_from_top) {
  return (deg_clockwise_from_top - 90.f) * 3.14159265f / 180.f;
}

void draw_start_blot(int cx, int cy, float attention) {
  constexpr float kStartDeg = 0.f;
  const float a = enso_clockwise_angle(kStartDeg);
  const int bx = cx + static_cast<int>(lrintf(cosf(a) * 163.f));
  const int by = cy + static_cast<int>(lrintf(sinf(a) * 163.f));
  for (int i = 0; i < 10; ++i) {
    const float lane = (static_cast<float>(i) - 4.5f) / 4.5f;
    const float tangential = (hash01(static_cast<int>(s_brush_seed >> 4) + i * 149) - 0.5f) * 14.f;
    const float radial = lane * (13.f + hash01(static_cast<int>(s_brush_seed >> 6) + i * 181) * 7.f);
    const int x0 = bx - 42 + static_cast<int>(lrintf(tangential));
    const int x1 = bx + 12 + static_cast<int>(lrintf(tangential * 0.35f));
    const int y = by + static_cast<int>(lrintf(radial));
    const int half_w = 7 + static_cast<int>(hash01(static_cast<int>(s_brush_seed >> 7) + i * 97) * 9.f);
    const float shade = 0.64f + hash01(static_cast<int>(s_brush_seed >> 9) + i * 83) * 0.36f;
    stroke_segment(x0, y, x1, y + static_cast<int>(lrintf(lane * 2.f)), 0.f, 1.f, half_w,
                   shade_color(attention, shade));
  }
}

void draw_procedural_enso(int cx, int cy, float attention, int start_step, int end_step) {
  constexpr int kBristles = 12;
  constexpr int kPasses = 3;
  constexpr float kStartDeg = 0.f;
  constexpr float kEndDeg = 330.f;
  if (start_step < 0) start_step = 0;
  if (end_step > kEnsoSteps) end_step = kEnsoSteps;
  if (end_step <= start_step) return;
  const float unsettled = 1.f - attention;
  const float breath = (sinf(s_pulse * 6.28318f) + 1.f) * 0.5f;
  if (start_step == 0) {
    draw_start_blot(cx, cy, attention);
  }

  for (int pass = 0; pass < kPasses; ++pass) {
    const float pass_gain = pass == 0 ? 0.58f : (pass == 1 ? 0.82f : 1.f);
    const float pass_spread = pass == 0 ? 1.55f : (pass == 1 ? 1.05f : 0.62f);
    constexpr int pass_step = 1;
    for (int b = 0; b < kBristles; ++b) {
      const float bristle = (static_cast<float>(b) - (kBristles - 1) * 0.5f) / ((kBristles - 1) * 0.5f);
      const float bristle_seed = hash01(static_cast<int>(s_brush_seed) + pass * 541 + b * 97 + s_lens * 311);
      const float initial_ink = 0.62f + hash01(static_cast<int>(s_brush_seed) + pass * 409 + b * 131) * 0.56f;
      const float spend_rate = 2.65f + hash01(static_cast<int>(s_brush_seed >> 2) + pass * 733 + b * 173) * 6.8f;
      const float endpoint =
          fminf(0.985f, fmaxf(0.80f, 0.985f - (spend_rate - 2.65f) * 0.028f +
                                         hash01(static_cast<int>(s_brush_seed >> 5) + pass * 269 + b * 199) * 0.08f));
      int i0 = start_step;
      if (pass_step > 1 && (i0 % pass_step) != 0) {
        i0 += pass_step - (i0 % pass_step);
      }
      for (int i = i0; i < end_step - 1; i += pass_step) {
        const int i1 = i + pass_step < kEnsoSteps ? i + pass_step : kEnsoSteps;
        const float t0 = static_cast<float>(i) / static_cast<float>(kEnsoSteps);
        const float t1 = static_cast<float>(i1) / static_cast<float>(kEnsoSteps);
        const float ink0 = bristle_ink(t0, initial_ink, spend_rate, bristle_seed);
        const float ink1 = bristle_ink(t1, initial_ink, spend_rate, bristle_seed);
        const float ink = (ink0 + ink1) * 0.5f;
        const float start_wetness = expf(-5.6f * t0);
        const float tail_dryness = t0 * t0 * t0;
        const float end_fade = fminf(1.f, fmaxf(0.f, (endpoint - t0) * 8.f));
        if (t0 > endpoint || ink <= 0.018f) {
          continue;
        }
        const float pressure0 = brush_pressure(t0);
        const float pressure1 = brush_pressure(t1);
        const float wobble0 =
            sinf(t0 * 31.416f + bristle_seed * 6.28318f + s_pulse * 3.14159f) * (1.3f + unsettled * 7.f);
        const float wobble1 =
            sinf(t1 * 31.416f + bristle_seed * 6.28318f + s_pulse * 3.14159f) * (1.3f + unsettled * 7.f);
        const float start_fan0 = bristle * 10.f * expf(-18.f * t0);
        const float start_fan1 = bristle * 10.f * expf(-18.f * t1);
        const float deg0 = kStartDeg + (kEndDeg - kStartDeg) * t0 + wobble0 + start_fan0;
        const float deg1 = kStartDeg + (kEndDeg - kStartDeg) * t1 + wobble1 + start_fan1;
        const float fiber0 = sinf(t0 * 74.2f + bristle_seed * 6.28318f) * (0.8f + pass * 0.65f);
        const float fiber1 = sinf(t1 * 74.2f + bristle_seed * 6.28318f) * (0.8f + pass * 0.65f);
        const float r0 = 163.f + bristle * (16.f + pressure0 * 13.f) * pass_spread + fiber0 +
                         sinf(t0 * 43.982f + bristle_seed * 6.28318f) * (1.8f + unsettled * 5.f);
        const float r1 = 163.f + bristle * (16.f + pressure1 * 13.f) * pass_spread + fiber1 +
                         sinf(t1 * 43.982f + bristle_seed * 6.28318f) * (1.8f + unsettled * 5.f);
        const float a0 = enso_clockwise_angle(deg0);
        const float a1 = enso_clockwise_angle(deg1);
        const int x0 = cx + static_cast<int>(lrintf(cosf(a0) * r0));
        const int y0 = cy + static_cast<int>(lrintf(sinf(a0) * r0));
        const int x1 = cx + static_cast<int>(lrintf(cosf(a1) * r1));
        const int y1 = cy + static_cast<int>(lrintf(sinf(a1) * r1));
        const float tx = static_cast<float>(x1 - x0);
        const float ty = static_cast<float>(y1 - y0);
        const float mag = sqrtf(tx * tx + ty * ty);
        const float nx = mag > 0.01f ? -ty / mag : 0.f;
        const float ny = mag > 0.01f ? tx / mag : 1.f;
        const float dry_tail_gain = (1.f - tail_dryness * 0.78f) * end_fade;
        const float pigment = 0.06f + ink * 0.94f;
        const float shade =
            (0.10f + pressure0 * (0.18f + start_wetness * 0.22f) + pigment * 0.62f + breath * 0.03f) *
            dry_tail_gain *
            pass_gain;
        int half_w =
            pass == 0
                ? static_cast<int>(
                      lrintf((pressure0 * ink * (10.5f + start_wetness * 10.0f) + attention * ink * 1.5f) *
                             dry_tail_gain))
                : static_cast<int>(
                      lrintf((pressure0 * ink * (3.2f + start_wetness * 3.6f) + attention * ink * 0.7f) *
                             dry_tail_gain));
        if (half_w < 0) {
          half_w = 0;
        }
        stroke_segment(x0, y0, x1, y1, nx, ny, half_w, shade_color(attention, shade));
      }
      if ((b & 3) == 3) {
        yield();
      }
    }
  }

}

}  // namespace

void pm_face_biometrics_on_enter(void) {
  s_last_tick_ms = 0;
  pm_face_biometrics_reveal();
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

void pm_face_biometrics_reveal(void) {
  s_brush_seed = millis() ^ (static_cast<uint32_t>(s_lens + 1) * 0x9e3779b9u) ^ (s_brush_seed << 6) ^
                 (s_brush_seed >> 2);
  s_drawn_step = 0;
  s_canvas_prepared = false;
}

const char *pm_face_biometrics_cycle_lens(void) {
  s_lens = (s_lens + 1) % 4;
  pm_face_biometrics_reveal();
  return kLensLabels[s_lens];
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
  return !s_canvas_prepared || s_drawn_step < kEnsoSteps;
}

void pm_face_biometrics_draw(void) {
  if (s_last_tick_ms == 0) {
    (void)pm_face_biometrics_anim_tick(millis());
  }

  PmBiometricsEstimate e = {};
  pm_biometrics_model_estimate(&e);

  if (!s_canvas_prepared) {
    pm_gfx->fillScreen(pm_gfx->color565(4, 5, 6));
    s_canvas_prepared = true;
  }
  const int cx = pm_face_lcd_cx;
  const int cy = pm_face_lcd_cy;
  const float selected_value = lens_value(e);
  if (s_drawn_step < kEnsoSteps) {
    const int next_step = s_drawn_step + kEnsoBrushChunk > kEnsoSteps ? kEnsoSteps : s_drawn_step + kEnsoBrushChunk;
    draw_procedural_enso(cx, cy, selected_value, s_drawn_step, next_step);
    s_drawn_step = next_step;
  }
}

void pm_face_biometrics_format_prompt_state(char *out, size_t cap) {
  PmBiometricsEstimate e = {};
  pm_biometrics_model_estimate(&e);
  pm_biometrics_model_format(&e, out, cap);
}
