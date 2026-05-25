#include "faces/tibetan_bowl/pm_face_tibetan_bowl.h"

#include <Arduino_GFX_Library.h>
#include <cmath>
#include <cstring>

#include "faces/chakra/pm_chakra_glyphs.h"
#include "faces/shared/pm_face_draw.h"
#include "pin_config.h"
#include "pm_display.h"
#include "pm_speaker.h"
#include "pm_touch.h"

static constexpr int kCx = pm_face_lcd_cx;
static constexpr int kCy = pm_face_lcd_cy;
static constexpr float kPi = 3.14159265f;
static constexpr float kTwoPi = kPi * 2.f;
static constexpr float kTargetAngVel = 2.8f;
static constexpr float kRimSwipeBlockRad = 0.45f;
static constexpr float kCenterTouchRadius = static_cast<float>(pm_face_scale_i(72));
static constexpr float kCenterTouchReleaseRadius = static_cast<float>(pm_face_scale_i(104));
static constexpr int16_t kCenterTouchSlop = pm_face_scale_i(30);
static constexpr uint32_t kCenterHoldMs = 220u;
static constexpr int kChakraCount = 7;

struct BowlChakra {
  uint8_t r;
  uint8_t g;
  uint8_t b;
  float hz;
};

static const BowlChakra kChakras[kChakraCount] = {
    {220, 20, 30, 396.f},    {255, 110, 0, 417.f},  {255, 210, 0, 528.f},
    {30, 200, 80, 639.f},    {40, 120, 255, 741.f}, {90, 40, 200, 852.f},
    {200, 160, 255, 963.f},
};

static float s_brightness = 0.55f;
static float s_finger_ang = 0.f;
static float s_last_ang = 0.f;
static float s_stroke_arc = 0.f;
static float s_wave_phase = 0.f;
static float s_trail_ang[12] = {};
static float s_touch_r = 0.f;
static float s_touch_hz = kChakras[3].hz;
static float s_touch_quality = 0.f;
static int s_trail_len = 0;
static int s_chakra_idx = 3;
static uint32_t s_last_anim_ms = 0;
static uint32_t s_last_touch_ms = 0;
static uint32_t s_center_down_ms = 0;
static int16_t s_center_x0 = 0;
static int16_t s_center_y0 = 0;
static bool s_touch_down = false;
static bool s_center_touch = false;
static bool s_center_hold_sounding = false;
static bool s_rim_stroke = false;
static bool s_block_rim_swipe = false;
static bool s_have_ang = false;
static float s_ring_envelope = 0.f;
static uint32_t s_last_strike_ms = 0;

static void rim_radii(int *r_inner, int *r_out) {
  const int R = (LCD_WIDTH < LCD_HEIGHT ? LCD_WIDTH : LCD_HEIGHT) / 2;
  *r_out = R - 32;
  *r_inner = *r_out - 58;
}

static float bowl_rim_mid(void) {
  int r_in = 0;
  int r_out = 0;
  rim_radii(&r_in, &r_out);
  return static_cast<float>(r_in + r_out) * 0.5f;
}

static uint16_t chakra_color(int idx, float dim) {
  const float d = dim < 0.f ? 0.f : (dim > 1.f ? 1.f : dim);
  const BowlChakra &ch = kChakras[(idx + kChakraCount) % kChakraCount];
  return pm_gfx->color565(static_cast<uint8_t>(ch.r * d), static_cast<uint8_t>(ch.g * d),
                          static_cast<uint8_t>(ch.b * d));
}

static uint16_t bowl_color(float dim) {
  return chakra_color(s_chakra_idx, dim);
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

static float theta_norm(float theta) {
  float t = theta;
  while (t < 0.f) {
    t += kTwoPi;
  }
  while (t >= kTwoPi) {
    t -= kTwoPi;
  }
  return t;
}

static float frequency_from_angle(float theta) {
  const float t = theta_norm(theta) / kTwoPi * static_cast<float>(kChakraCount);
  const int i0 = static_cast<int>(floorf(t)) % kChakraCount;
  const int i1 = (i0 + 1) % kChakraCount;
  const float frac = t - floorf(t);
  const float smooth = frac * frac * (3.f - 2.f * frac);
  return kChakras[i0].hz * (1.f - smooth) + kChakras[i1].hz * smooth;
}

static int chakra_index_from_angle(float theta) {
  const float t = theta_norm(theta) / kTwoPi * static_cast<float>(kChakraCount);
  return static_cast<int>(floorf(t)) % kChakraCount;
}

static void touch_polar(int16_t x, int16_t y, float *r, float *theta) {
  const float dx = static_cast<float>(x - kCx);
  const float dy = static_cast<float>(y - kCy);
  *r = sqrtf(dx * dx + dy * dy);
  *theta = atan2f(dy, dx);
}

static float rim_quality(float r) {
  int r_in = 0;
  int r_out = 0;
  rim_radii(&r_in, &r_out);
  const float mid = static_cast<float>(r_in + r_out) * 0.5f;
  const float half = static_cast<float>(r_out - r_in) * 0.5f;
  const float err = fabsf(r - mid);
  const float q = 1.f - err / (half + static_cast<float>(pm_face_scale_i(18)));
  if (q < 0.f) {
    return 0.f;
  }
  if (q > 1.f) {
    return 1.f;
  }
  return q;
}

static void push_trail(float ang) {
  if (s_trail_len < static_cast<int>(sizeof(s_trail_ang) / sizeof(s_trail_ang[0]))) {
    s_trail_ang[s_trail_len++] = ang;
  } else {
    memmove(s_trail_ang, s_trail_ang + 1, sizeof(s_trail_ang) - sizeof(s_trail_ang[0]));
    s_trail_ang[s_trail_len - 1] = ang;
  }
}

static void draw_chakra_sectors(float energy) {
  int r_in = 0;
  int r_out = 0;
  rim_radii(&r_in, &r_out);
  for (int i = 0; i < kChakraCount; ++i) {
    const float start_deg = static_cast<float>(i) * (360.f / static_cast<float>(kChakraCount));
    const float end_deg = static_cast<float>(i + 1) * (360.f / static_cast<float>(kChakraCount));
    const float dim = (i == s_chakra_idx) ? (0.22f + 0.34f * energy) : 0.08f;
    pm_face_draw_annular_wedge(kCx, kCy, r_in - pm_face_scale_i(4), r_out + pm_face_scale_i(4), start_deg,
                               end_deg, chakra_color(i, dim));
  }
  if (s_touch_down && energy > 0.05f) {
    const int r_mid = static_cast<int>(bowl_rim_mid());
    const int fx = kCx + static_cast<int>(cosf(s_finger_ang) * static_cast<float>(r_mid));
    const int fy = kCy + static_cast<int>(sinf(s_finger_ang) * static_cast<float>(r_mid));
    pm_gfx->drawCircle(fx, fy, pm_face_scale_i(12), bowl_color(0.35f + 0.4f * energy));
  }
}

static float bowl_visual_energy(float audio_energy) {
  float v = audio_energy;
  if (s_ring_envelope > v) {
    v = s_ring_envelope;
  }
  if (v > 1.f) {
    v = 1.f;
  }
  return v;
}

static void bowl_center_strike(uint32_t now_ms) {
  if (now_ms - s_last_strike_ms < 90u) {
    return;
  }
  s_last_strike_ms = now_ms;
  s_ring_envelope = 1.f;
  s_wave_phase = 0.f;

  PmBowlVoiceCtrl strike = {};
  strike.target_hz = s_touch_hz;
  strike.center_strike = true;
  strike.excitation = 1.f;
  strike.brightness = s_brightness;
  pm_speaker_bowl_voice_push(strike);
}

static void bowl_center_hold(void) {
  PmBowlVoiceCtrl tone = {};
  tone.target_hz = kChakras[s_chakra_idx].hz;
  tone.excitation = 0.08f;
  tone.brightness = 0.1f;
  tone.rim_quality = 1.f;
  tone.finger_down = true;
  tone.pure_tone = true;
  pm_speaker_bowl_voice_push(tone);
}

static void clear_center_touch(void) {
  s_touch_down = false;
  s_center_touch = false;
  s_center_hold_sounding = false;
  s_have_ang = false;
  s_rim_stroke = false;
  s_block_rim_swipe = false;
  s_stroke_arc = 0.f;
  s_trail_len = 0;
  s_center_down_ms = 0;
  PmBowlVoiceCtrl off = {};
  off.finger_down = false;
  pm_speaker_bowl_voice_push(off);
}

static void draw_standing_wave(float energy) {
  if (energy < 0.03f && !s_touch_down && s_ring_envelope < 0.03f) {
    return;
  }
  int r_in = 0;
  int r_out = 0;
  rim_radii(&r_in, &r_out);
  const int r_mid = (r_in + r_out) / 2;
  const int modes = 2 + static_cast<int>(s_brightness * 6.f);
  const float amp = 5.f + 14.f * energy;
  const uint16_t col = bowl_color(0.25f + 0.55f * energy);

  for (int s = 0; s < 48; ++s) {
    const float a0 = static_cast<float>(s) * (kTwoPi / 48.f);
    const float a1 = static_cast<float>(s + 1) * (kTwoPi / 48.f);
    const float w0 = static_cast<float>(r_mid) + amp * sinf(static_cast<float>(modes) * a0 + s_wave_phase);
    const float w1 = static_cast<float>(r_mid) + amp * sinf(static_cast<float>(modes) * a1 + s_wave_phase);
    const int x0 = kCx + static_cast<int>(cosf(a0) * w0);
    const int y0 = kCy + static_cast<int>(sinf(a0) * w0);
    const int x1 = kCx + static_cast<int>(cosf(a1) * w1);
    const int y1 = kCy + static_cast<int>(sinf(a1) * w1);
    pm_gfx->drawLine(x0, y0, x1, y1, col);
  }
}

static void draw_trail(void) {
  int r_in = 0;
  int r_out = 0;
  rim_radii(&r_in, &r_out);
  const int r_mid = (r_in + r_out) / 2;
  for (int i = 0; i < s_trail_len; ++i) {
    const float alpha = static_cast<float>(i + 1) / static_cast<float>(s_trail_len + 1);
    const int x = kCx + static_cast<int>(cosf(s_trail_ang[i]) * static_cast<float>(r_mid));
    const int y = kCy + static_cast<int>(sinf(s_trail_ang[i]) * static_cast<float>(r_mid));
    pm_gfx->fillCircle(x, y, 4, bowl_color(0.2f + 0.65f * alpha));
  }
}

static void draw_bowl_graphic(float energy) {
  const uint16_t shadow = pm_gfx->color565(2, 2, 2);
  const uint16_t rim_hi = bowl_color(0.82f + 0.18f * energy);
  const uint16_t rim_mid = bowl_color(0.58f + 0.20f * energy);
  const uint16_t wall = bowl_color(0.34f + 0.18f * energy);
  const uint16_t well = bowl_color(0.10f + 0.04f * energy);
  const uint16_t disc_outer = bowl_color(0.24f + 0.16f * energy);
  const uint16_t disc_mid = bowl_color(0.34f + 0.18f * energy);
  const uint16_t disc_center = pm_gfx->color565(10, 9, 14);
  const int breath = pm_face_scale_i(static_cast<int>(3.f * sinf(s_wave_phase * 0.7f)));

  pm_gfx->fillCircle(kCx, kCy + pm_face_scale_i(6), pm_face_scale_i(178) + breath, shadow);
  pm_gfx->fillCircle(kCx, kCy, pm_face_scale_i(176) + breath, rim_mid);
  pm_gfx->fillCircle(kCx, kCy, pm_face_scale_i(166), wall);
  pm_gfx->fillCircle(kCx, kCy, pm_face_scale_i(138), bowl_color(0.20f));
  pm_gfx->fillCircle(kCx, kCy, pm_face_scale_i(116), bowl_color(0.14f));
  pm_gfx->fillCircle(kCx, kCy, pm_face_scale_i(88), well);
  pm_gfx->fillCircle(kCx, kCy, pm_face_scale_i(58), disc_outer);
  pm_gfx->fillCircle(kCx, kCy, pm_face_scale_i(44), disc_mid);
  pm_gfx->fillCircle(kCx, kCy, pm_face_scale_i(28), disc_center);

  pm_gfx->drawCircle(kCx, kCy, pm_face_scale_i(176) + breath, rim_hi);
  pm_gfx->drawCircle(kCx, kCy, pm_face_scale_i(168), bowl_color(0.95f));
  pm_gfx->drawCircle(kCx, kCy, pm_face_scale_i(138), bowl_color(0.36f));
  pm_gfx->drawCircle(kCx, kCy, pm_face_scale_i(88), bowl_color(0.28f));
  pm_gfx->drawCircle(kCx, kCy, pm_face_scale_i(58), bowl_color(0.62f));
  pm_gfx->drawCircle(kCx, kCy, pm_face_scale_i(44), bowl_color(0.48f));

  pm_chakra_draw_glyph(pm_gfx, kCx, kCy, s_chakra_idx, bowl_color(0.98f), energy > 0.04f || s_touch_down);

  if (energy > 0.05f) {
    const int ring_a = pm_face_scale_i(74) + pm_face_scale_i(static_cast<int>(22.f * energy));
    const int ring_b = pm_face_scale_i(112) + pm_face_scale_i(static_cast<int>(18.f * energy));
    const int ring_c = pm_face_scale_i(148) + pm_face_scale_i(static_cast<int>(10.f * energy * s_ring_envelope));
    pm_gfx->drawCircle(kCx, kCy, ring_a, bowl_color(0.15f + 0.45f * energy));
    pm_gfx->drawCircle(kCx, kCy, ring_b, bowl_color(0.12f + 0.35f * energy));
    if (s_ring_envelope > 0.08f) {
      pm_gfx->drawCircle(kCx, kCy, ring_c, bowl_color(0.08f + 0.28f * s_ring_envelope));
    }
  }
}

static void draw_rim_finger(void) {
  if (!s_touch_down) {
    return;
  }
  const int r_mid = static_cast<int>(bowl_rim_mid());
  const int fx = kCx + static_cast<int>(cosf(s_finger_ang) * static_cast<float>(r_mid));
  const int fy = kCy + static_cast<int>(sinf(s_finger_ang) * static_cast<float>(r_mid));
  pm_gfx->fillCircle(fx, fy, pm_face_scale_i(7), bowl_color(0.95f));
  pm_gfx->drawCircle(fx, fy, pm_face_scale_i(10), pm_gfx->color565(255, 245, 210));
}

void pm_face_tibetan_bowl_draw(void) {
  const float energy = bowl_visual_energy(pm_face_tibetan_bowl_energy());
  pm_gfx->fillScreen(pm_gfx->color565(5, 4, 3));

  draw_chakra_sectors(energy);
  draw_standing_wave(energy);
  draw_trail();
  draw_bowl_graphic(energy);
  draw_rim_finger();

  char debug[44];
  if (s_touch_down) {
    snprintf(debug, sizeof(debug), "touch r%.0f a%03d f%.0f q%.2f", s_touch_r,
             static_cast<int>(theta_norm(s_finger_ang) * 180.f / kPi), s_touch_hz, s_touch_quality);
  } else {
    snprintf(debug, sizeof(debug), "touch -- f%.0f", kChakras[s_chakra_idx].hz);
  }
  pm_face_draw_centered_line(debug, 414, bowl_color(0.72f), 1, 1);
}

int pm_face_tibetan_bowl_cycle_chakra(int delta) {
  int v = (s_chakra_idx + delta) % kChakraCount;
  if (v < 0) {
    v += kChakraCount;
  }
  s_chakra_idx = v;
  s_touch_hz = kChakras[s_chakra_idx].hz;
  s_finger_ang = (static_cast<float>(s_chakra_idx) + 0.5f) * (kTwoPi / static_cast<float>(kChakraCount));
  if (s_center_hold_sounding) {
    bowl_center_hold();
  }
  return s_chakra_idx;
}

bool pm_face_tibetan_bowl_touch_tick(uint32_t now_ms) {
  int16_t xs[1];
  int16_t ys[1];
  const uint8_t n = pm_touch_sample(xs, ys, 1);

  if (n == 0) {
    if (s_center_touch) {
      s_touch_hz = kChakras[s_chakra_idx].hz;
      bowl_center_strike(now_ms);
      clear_center_touch();
      return true;
    }
    return s_ring_envelope > 0.02f;
  }

  const int16_t x = xs[0];
  const int16_t y = ys[0];
  float r = 0.f;
  float theta = 0.f;
  touch_polar(x, y, &r, &theta);
  s_touch_r = r;

  if (!s_center_touch) {
    if (r > kCenterTouchRadius) {
      return false;
    }
    s_center_touch = true;
    s_touch_down = true;
    s_center_hold_sounding = false;
    s_center_down_ms = now_ms;
    s_center_x0 = x;
    s_center_y0 = y;
    s_finger_ang = (static_cast<float>(s_chakra_idx) + 0.5f) * (kTwoPi / static_cast<float>(kChakraCount));
    s_touch_hz = kChakras[s_chakra_idx].hz;
    s_touch_quality = 1.f;
    s_last_touch_ms = now_ms;
    return true;
  }

  const int16_t dx = static_cast<int16_t>(x - s_center_x0);
  const int16_t dy = static_cast<int16_t>(y - s_center_y0);
  if (abs(dx) > kCenterTouchSlop || abs(dy) > kCenterTouchSlop || r > kCenterTouchReleaseRadius) {
    if (s_center_hold_sounding) {
      s_touch_hz = kChakras[s_chakra_idx].hz;
      bowl_center_strike(now_ms);
    }
    clear_center_touch();
    return true;
  }

  s_touch_down = true;
  s_touch_hz = kChakras[s_chakra_idx].hz;
  s_touch_quality = 1.f;
  s_finger_ang = (static_cast<float>(s_chakra_idx) + 0.5f) * (kTwoPi / static_cast<float>(kChakraCount));
  if (now_ms - s_last_touch_ms >= 35u) {
    s_last_touch_ms = now_ms;
    if (now_ms - s_center_down_ms >= kCenterHoldMs) {
      bowl_center_hold();
      s_center_hold_sounding = true;
    }
  }
  return s_center_hold_sounding;
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
  const float energy = pm_face_tibetan_bowl_energy();
  if (s_ring_envelope > 0.001f) {
    s_ring_envelope *= 0.965f;
  } else {
    s_ring_envelope = 0.f;
  }
  if (energy < 0.01f && !s_touch_down && s_ring_envelope < 0.02f) {
    return false;
  }
  if (now_ms - s_last_anim_ms < 40u) {
    return energy > 0.01f || s_touch_down || s_ring_envelope > 0.02f;
  }
  s_last_anim_ms = now_ms;
  s_wave_phase += 0.11f + 0.06f * s_ring_envelope;
  return true;
}

void pm_face_tibetan_bowl_stop(void) {
  pm_speaker_bowl_voice_stop();
  s_touch_down = false;
  s_center_touch = false;
  s_center_hold_sounding = false;
  s_have_ang = false;
  s_block_rim_swipe = false;
  s_rim_stroke = false;
  s_trail_len = 0;
  s_ring_envelope = 0.f;
}

float pm_face_tibetan_bowl_energy(void) { return pm_speaker_bowl_voice_energy(); }

int pm_face_tibetan_bowl_chakra_index(void) { return s_chakra_idx; }
