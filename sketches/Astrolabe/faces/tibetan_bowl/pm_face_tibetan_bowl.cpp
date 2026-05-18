#include "faces/tibetan_bowl/pm_face_tibetan_bowl.h"

#include <Arduino_GFX_Library.h>
#include <cmath>
#include <cstring>

#include "faces/shared/pm_face_draw.h"
#include "pin_config.h"
#include "pm_display.h"
#include "pm_speaker.h"
#include "pm_touch.h"

static constexpr int kCx = pm_face_lcd_cx;
static constexpr int kCy = pm_face_lcd_cy;
static constexpr float kPi = 3.14159265f;
static constexpr float kTwoPi = kPi * 2.f;
static constexpr float kMinExciteRad = 0.11f;
static constexpr float kRimSwipeBlockRad = 0.45f;

struct BowlPreset {
  const char *name;
  float fund_hz;
  float harm2;
  float harm3;
  uint32_t decay_ms;
};

static const BowlPreset kPresets[] = {
    {"Cup", 523.f, 0.38f, 0.22f, 2200u},
    {"Medium", 392.f, 0.45f, 0.28f, 3400u},
    {"Low", 294.f, 0.52f, 0.32f, 4800u},
    {"Temple", 220.f, 0.58f, 0.36f, 6200u},
};

static int s_index = 1;
static float s_ripple = 0.f;
static float s_finger_ang = 0.f;
static float s_strike_ang = 0.f;
static float s_last_ang = 0.f;
static float s_arc_since_excite = 0.f;
static float s_stroke_arc = 0.f;
static uint32_t s_ripple_start = 0;
static uint32_t s_last_anim_ms = 0;
static uint32_t s_last_touch_ms = 0;
static bool s_ripple_active = false;
static bool s_touch_down = false;
static bool s_rim_stroke = false;
static bool s_block_rim_swipe = false;
static bool s_have_ang = false;

static void rim_radii(int *r_inner, int *r_outer) {
  const int R = (LCD_WIDTH < LCD_HEIGHT ? LCD_WIDTH : LCD_HEIGHT) / 2;
  *r_outer = R - 9;
  *r_inner = *r_outer - 5;
}

static uint16_t bowl_gold(float dim) {
  const float d = dim < 0.f ? 0.f : (dim > 1.f ? 1.f : dim);
  return pm_gfx->color565(static_cast<uint8_t>(200 * d), static_cast<uint8_t>(165 * d),
                          static_cast<uint8_t>(70 * d));
}

static float unwrap_delta(float prev, float cur) {
  float d = cur - prev;
  while (d > kPi) {
    d -= kTwoPi;
  }
  while (d < -kPi) {
    d += kTwoPi;
  }
  return d;
}

static bool rim_sample(int16_t x, int16_t y, float *out_angle, float *out_intensity) {
  int r_in = 0;
  int r_out = 0;
  rim_radii(&r_in, &r_out);
  const int dx = static_cast<int>(x) - kCx;
  const int dy = static_cast<int>(y) - kCy;
  const float r = sqrtf(static_cast<float>(dx * dx + dy * dy));
  const float r_mid = static_cast<float>(r_in + r_out) * 0.5f;
  const float half_band = static_cast<float>(r_out - r_in) * 0.5f + 14.f;
  if (r < static_cast<float>(r_in) - 14.f || r > static_cast<float>(r_out) + 16.f) {
    return false;
  }
  *out_angle = atan2f(static_cast<float>(dy), static_cast<float>(dx));
  float intensity = 1.f - fabsf(r - r_mid) / half_band;
  if (intensity < 0.25f) {
    intensity = 0.25f;
  } else if (intensity > 1.f) {
    intensity = 1.f;
  }
  *out_intensity = intensity;
  return true;
}

static bool excite_bowl(float ang, float intensity, float ang_vel) {
  const BowlPreset &p = kPresets[s_index];
  if (pm_speaker_is_playing()) {
    return false;
  }
  PmBowlStrike strike = {};
  strike.fund_hz = p.fund_hz;
  strike.harm2 = p.harm2;
  strike.harm3 = p.harm3;
  const float vel_boost = ang_vel < 0.5f ? 0.5f : (ang_vel > 6.f ? 6.f : ang_vel);
  strike.decay_ms =
      static_cast<uint32_t>(static_cast<float>(p.decay_ms) * (1.05f - 0.08f * vel_boost));
  if (strike.decay_ms < 900u) {
    strike.decay_ms = 900u;
  }
  strike.amplitude = 0.3f + 0.55f * intensity + 0.05f * vel_boost;
  if (strike.amplitude > 1.f) {
    strike.amplitude = 1.f;
  }
  strike.pan = sinf(ang);
  if (!pm_speaker_play_bowl_strike_begin(strike)) {
    return false;
  }
  s_strike_ang = ang;
  s_ripple_active = true;
  s_ripple_start = millis();
  s_ripple = 0.f;
  return true;
}

static void draw_bowl_graphic(void) {
  const uint16_t rim = bowl_gold(1.f);
  const uint16_t body = bowl_gold(0.55f);
  const uint16_t inner = pm_gfx->color565(18, 14, 8);
  const int cy = kCy + 6;

  pm_gfx->fillEllipse(kCx, cy + 18, 92, 22, body);
  pm_gfx->fillEllipse(kCx, cy, 78, 46, body);
  pm_gfx->fillEllipse(kCx, cy - 6, 62, 34, inner);
  pm_gfx->drawEllipse(kCx, cy, 80, 48, rim);
  pm_gfx->drawEllipse(kCx, cy - 2, 70, 40, rim);
  pm_gfx->drawFastHLine(kCx - 48, cy + 20, 96, bowl_gold(0.35f));
}

static void draw_strike_ripples(void) {
  if (!s_ripple_active && !pm_speaker_is_playing()) {
    return;
  }
  int r_in = 0;
  int r_out = 0;
  rim_radii(&r_in, &r_out);
  const int r_mid = (r_in + r_out) / 2;
  for (int i = 0; i < 4; ++i) {
    const float phase = s_ripple + static_cast<float>(i) * 0.28f;
    const float t = phase - floorf(phase);
    const int spread = static_cast<int>(t * 28.f);
    const int r = r_mid + spread;
    const float alpha = 1.f - t;
    if (alpha <= 0.04f) {
      continue;
    }
    const uint16_t c = bowl_gold(0.12f + 0.5f * alpha);
    const int ox = static_cast<int>(cosf(s_strike_ang) * static_cast<float>(r));
    const int oy = static_cast<int>(sinf(s_strike_ang) * static_cast<float>(r));
    pm_gfx->drawCircle(kCx + ox, kCy + oy, 6 + i * 2, c);
    pm_gfx->drawCircle(kCx + ox, kCy + oy, 7 + i * 2, c);
  }
}

static void draw_rim_finger(void) {
  if (!s_touch_down) {
    return;
  }
  int r_in = 0;
  int r_out = 0;
  rim_radii(&r_in, &r_out);
  const int r_mid = (r_in + r_out) / 2;
  const int fx = kCx + static_cast<int>(cosf(s_finger_ang) * static_cast<float>(r_mid));
  const int fy = kCy + static_cast<int>(sinf(s_finger_ang) * static_cast<float>(r_mid));
  pm_gfx->fillCircle(fx, fy, 7, bowl_gold(0.9f));
  pm_gfx->drawCircle(fx, fy, 9, pm_gfx->color565(255, 240, 200));
}

void pm_face_tibetan_bowl_draw(void) {
  const BowlPreset &p = kPresets[s_index];
  pm_gfx->fillScreen(pm_gfx->color565(6, 5, 4));

  draw_strike_ripples();
  draw_bowl_graphic();
  draw_rim_finger();

  pm_face_draw_centered_line(p.name, 52, bowl_gold(0.92f), 2, 2);

  char hint[28];
  snprintf(hint, sizeof(hint), "%.0f Hz", p.fund_hz);
  pm_face_draw_centered_line(hint, 400, bowl_gold(0.7f), 1, 2);
  pm_face_draw_centered_line("drag rim", 418, bowl_gold(0.45f), 1, 1);
}

int pm_face_tibetan_bowl_cycle(int delta) {
  pm_face_tibetan_bowl_stop();
  const int n = static_cast<int>(sizeof(kPresets) / sizeof(kPresets[0]));
  int v = s_index + delta;
  v = (v % n + n) % n;
  s_index = v;
  return s_index;
}

bool pm_face_tibetan_bowl_touch_tick(uint32_t now_ms) {
  int16_t xs[2];
  int16_t ys[2];
  const uint8_t n = pm_touch_sample(xs, ys, 2);
  bool repaint = false;

  if (n == 0) {
    if (s_touch_down) {
      if (s_rim_stroke && s_stroke_arc >= kRimSwipeBlockRad) {
        s_block_rim_swipe = true;
      }
      s_touch_down = false;
      s_have_ang = false;
      s_arc_since_excite = 0.f;
      repaint = true;
    }
    return repaint;
  }

  const int16_t x = xs[0];
  const int16_t y = ys[0];
  float ang = 0.f;
  float intensity = 0.f;
  if (!rim_sample(x, y, &ang, &intensity)) {
    if (s_touch_down) {
      s_touch_down = false;
      s_have_ang = false;
      repaint = true;
    }
    return repaint;
  }

  if (!s_touch_down) {
    s_touch_down = true;
    s_rim_stroke = true;
    s_stroke_arc = 0.f;
    s_arc_since_excite = 0.f;
    s_last_ang = ang;
    s_finger_ang = ang;
    s_have_ang = true;
    s_last_touch_ms = now_ms;
    return true;
  }

  s_finger_ang = ang;
  if (s_have_ang) {
    const float d_ang = unwrap_delta(s_last_ang, ang);
    const float abs_d = fabsf(d_ang);
    s_stroke_arc += abs_d;
    s_arc_since_excite += abs_d;
    s_last_ang = ang;

    const uint32_t dt = (now_ms > s_last_touch_ms) ? (now_ms - s_last_touch_ms) : 16u;
    s_last_touch_ms = now_ms;
    const float ang_vel = abs_d / (static_cast<float>(dt) * 0.001f);

    if (s_arc_since_excite >= kMinExciteRad) {
      if (excite_bowl(ang, intensity, ang_vel)) {
        s_arc_since_excite = 0.f;
        repaint = true;
      }
    }
  }

  return true;
}

bool pm_face_tibetan_bowl_consume_rim_swipe_block(void) {
  if (!s_block_rim_swipe) {
    return false;
  }
  s_block_rim_swipe = false;
  s_rim_stroke = false;
  s_stroke_arc = 0.f;
  return true;
}

bool pm_face_tibetan_bowl_anim_tick(uint32_t now_ms) {
  if (!s_ripple_active && !pm_speaker_is_playing() && !s_touch_down) {
    return false;
  }
  if (now_ms - s_last_anim_ms < 40u) {
    return s_ripple_active || pm_speaker_is_playing() || s_touch_down;
  }
  s_last_anim_ms = now_ms;
  s_ripple = static_cast<float>(now_ms - s_ripple_start) * 0.0014f;
  if (!pm_speaker_is_playing()) {
    s_ripple_active = false;
  }
  return s_ripple_active || pm_speaker_is_playing() || s_touch_down;
}

void pm_face_tibetan_bowl_stop(void) {
  s_ripple_active = false;
  s_touch_down = false;
  s_have_ang = false;
  s_block_rim_swipe = false;
  s_rim_stroke = false;
}

int pm_face_tibetan_bowl_index(void) { return s_index; }
