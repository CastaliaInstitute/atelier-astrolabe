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
static constexpr float kTargetAngVel = 2.8f;
static constexpr float kRimSwipeBlockRad = 0.45f;
static constexpr int kChakraCount = 7;

static const float kChakraHz[kChakraCount] = {256.f, 288.f, 320.f, 341.3f, 384.f, 426.7f, 480.f};
static const char *kChakraNames[kChakraCount] = {"Root",       "Sacral", "Solar", "Heart",
                                                 "Throat",     "3rd Eye", "Crown"};

static float s_brightness = 0.55f;
static float s_finger_ang = 0.f;
static float s_last_ang = 0.f;
static float s_stroke_arc = 0.f;
static float s_wave_phase = 0.f;
static float s_trail_ang[12] = {};
static int s_trail_len = 0;
static int s_chakra_idx = 3;
static uint32_t s_last_anim_ms = 0;
static uint32_t s_last_touch_ms = 0;
static bool s_touch_down = false;
static bool s_rim_stroke = false;
static bool s_block_rim_swipe = false;
static bool s_have_ang = false;

static void rim_radii(int *r_inner, int *r_out) {
  const int R = (LCD_WIDTH < LCD_HEIGHT ? LCD_WIDTH : LCD_HEIGHT) / 2;
  *r_out = R - 9;
  *r_inner = *r_out - 5;
}

static float bowl_rim_mid(void) {
  int r_in = 0;
  int r_out = 0;
  rim_radii(&r_in, &r_out);
  return static_cast<float>(r_in + r_out) * 0.5f;
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
  return kChakraHz[i0] * (1.f - smooth) + kChakraHz[i1] * smooth;
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
  const float rim = bowl_rim_mid();
  const float err = fabsf(r - rim);
  const float q = 1.f - err / 18.f;
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
  const int r_lab = r_out + 14;
  for (int i = 0; i < kChakraCount; ++i) {
    const float start_deg = static_cast<float>(i) * (360.f / static_cast<float>(kChakraCount));
    const float end_deg = static_cast<float>(i + 1) * (360.f / static_cast<float>(kChakraCount));
    const float dim = (i == s_chakra_idx) ? (0.22f + 0.35f * energy) : 0.06f;
    pm_face_draw_annular_wedge(kCx, kCy, r_in - 2, r_out + 4, start_deg, end_deg, bowl_gold(dim));
    if (i == s_chakra_idx) {
      const float amid = pm_face_deg_to_rad((start_deg + end_deg) * 0.5f);
      pm_face_draw_label_at_polar(kCx, kCy, r_lab, amid, kChakraNames[i], bowl_gold(0.85f));
    }
  }
  if (s_touch_down && energy > 0.05f) {
    const int r_mid = static_cast<int>(bowl_rim_mid());
    const int fx = kCx + static_cast<int>(cosf(s_finger_ang) * static_cast<float>(r_mid));
    const int fy = kCy + static_cast<int>(sinf(s_finger_ang) * static_cast<float>(r_mid));
    pm_gfx->drawCircle(fx, fy, 12, bowl_gold(0.35f + 0.4f * energy));
  }
}

static void draw_standing_wave(float energy) {
  if (energy < 0.03f && !s_touch_down) {
    return;
  }
  int r_in = 0;
  int r_out = 0;
  rim_radii(&r_in, &r_out);
  const int r_mid = (r_in + r_out) / 2;
  const int modes = 2 + static_cast<int>(s_brightness * 6.f);
  const float amp = 5.f + 14.f * energy;
  const uint16_t col = bowl_gold(0.25f + 0.55f * energy);

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
    pm_gfx->fillCircle(x, y, 4, bowl_gold(0.2f + 0.65f * alpha));
  }
}

static void draw_bowl_graphic(float energy) {
  const uint16_t rim = bowl_gold(0.75f + 0.25f * energy);
  const uint16_t body = bowl_gold(0.45f + 0.25f * energy);
  const uint16_t inner = pm_gfx->color565(18, 14, 8);
  const int cy = kCy + 8;
  const int breath = static_cast<int>(4.f * sinf(s_wave_phase * 0.7f));

  pm_gfx->fillEllipse(kCx, cy + 20, 88, 20 + breath / 2, body);
  pm_gfx->fillEllipse(kCx, cy, 74, 44 + breath, body);
  pm_gfx->fillEllipse(kCx, cy - 8, 58, 32, inner);
  pm_gfx->drawEllipse(kCx, cy, 76, 46 + breath, rim);
  pm_gfx->drawEllipse(kCx, cy - 4, 64, 36, rim);
  if (energy > 0.08f) {
    pm_gfx->fillCircle(kCx, cy - 2, 10 + static_cast<int>(8.f * energy), bowl_gold(0.15f + 0.35f * energy));
  }
}

static void draw_rim_finger(void) {
  if (!s_touch_down) {
    return;
  }
  const int r_mid = static_cast<int>(bowl_rim_mid());
  const int fx = kCx + static_cast<int>(cosf(s_finger_ang) * static_cast<float>(r_mid));
  const int fy = kCy + static_cast<int>(sinf(s_finger_ang) * static_cast<float>(r_mid));
  pm_gfx->fillCircle(fx, fy, 7, bowl_gold(0.95f));
  pm_gfx->drawCircle(fx, fy, 10, pm_gfx->color565(255, 245, 210));
}

void pm_face_tibetan_bowl_draw(void) {
  const float energy = pm_face_tibetan_bowl_energy();
  pm_gfx->fillScreen(pm_gfx->color565(5, 4, 3));

  draw_chakra_sectors(energy);
  draw_standing_wave(energy);
  draw_trail();
  draw_bowl_graphic(energy);
  draw_rim_finger();

  char line[20];
  snprintf(line, sizeof(line), "%s", kChakraNames[s_chakra_idx]);
  pm_face_draw_centered_line(line, 56, bowl_gold(0.92f), 2, 2);
  snprintf(line, sizeof(line), "%.0f Hz", frequency_from_angle(s_finger_ang));
  pm_face_draw_centered_line(line, 400, bowl_gold(0.68f), 1, 2);
  pm_face_draw_centered_line("rim drag · tap center", 418, bowl_gold(0.42f), 1, 1);
}

void pm_face_tibetan_bowl_brightness_delta(float delta) {
  s_brightness += delta;
  if (s_brightness < 0.15f) {
    s_brightness = 0.15f;
  } else if (s_brightness > 1.f) {
    s_brightness = 1.f;
  }
}

bool pm_face_tibetan_bowl_touch_tick(uint32_t now_ms) {
  int16_t xs[2];
  int16_t ys[2];
  const uint8_t n = pm_touch_sample(xs, ys, 2);

  if (n == 0) {
    if (s_touch_down) {
      if (s_rim_stroke && s_stroke_arc >= kRimSwipeBlockRad) {
        s_block_rim_swipe = true;
      }
      PmBowlVoiceCtrl off = {};
      off.finger_down = false;
      pm_speaker_bowl_voice_push(off);
      s_touch_down = false;
      s_have_ang = false;
      s_trail_len = 0;
      return true;
    }
    return false;
  }

  const int16_t x = xs[0];
  const int16_t y = ys[0];
  float r = 0.f;
  float theta = 0.f;
  touch_polar(x, y, &r, &theta);

  if (r < 78.f) {
    if (!s_touch_down) {
      PmBowlVoiceCtrl strike = {};
      strike.target_hz = frequency_from_angle(theta);
      strike.center_strike = true;
      strike.brightness = s_brightness;
      pm_speaker_bowl_voice_push(strike);
      s_wave_phase = 0.f;
    }
    s_touch_down = true;
    s_finger_ang = theta;
    return true;
  }

  const float rq = rim_quality(r);
  if (rq < 0.08f) {
    if (s_touch_down) {
      PmBowlVoiceCtrl off = {};
      off.finger_down = false;
      pm_speaker_bowl_voice_push(off);
      s_touch_down = false;
      s_have_ang = false;
      return true;
    }
    return false;
  }

  const float target_hz = frequency_from_angle(theta);
  s_chakra_idx = chakra_index_from_angle(theta);
  s_finger_ang = theta;

  float excitation = 0.f;
  float ang_vel = 0.f;
  if (s_touch_down && s_have_ang) {
    const float d_ang = unwrap_delta(s_last_ang, theta);
    ang_vel = fabsf(d_ang) / (static_cast<float>((now_ms > s_last_touch_ms) ? (now_ms - s_last_touch_ms) : 16u) * 0.001f);
    s_stroke_arc += fabsf(d_ang);
    if (fabsf(d_ang) > 0.02f) {
      push_trail(theta);
    }
    excitation = fabsf(ang_vel) / kTargetAngVel;
    if (excitation > 1.f) {
      excitation = 1.f;
    }
    if (d_ang > 0.f) {
      s_brightness += 0.002f;
    } else if (d_ang < 0.f) {
      s_brightness -= 0.001f;
    }
    if (s_brightness > 1.f) {
      s_brightness = 1.f;
    } else if (s_brightness < 0.15f) {
      s_brightness = 0.15f;
    }
  } else {
    s_rim_stroke = true;
    s_stroke_arc = 0.f;
    s_trail_len = 0;
    push_trail(theta);
  }

  s_last_ang = theta;
  s_have_ang = true;
  s_touch_down = true;
  s_last_touch_ms = now_ms;

  PmBowlVoiceCtrl ctrl = {};
  ctrl.target_hz = target_hz;
  ctrl.excitation = excitation * rq;
  ctrl.brightness = s_brightness;
  ctrl.pan = sinf(theta);
  ctrl.rim_quality = rq;
  ctrl.finger_down = true;
  pm_speaker_bowl_voice_push(ctrl);

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
  const float energy = pm_face_tibetan_bowl_energy();
  if (energy < 0.01f && !s_touch_down) {
    return false;
  }
  if (now_ms - s_last_anim_ms < 40u) {
    return energy > 0.01f || s_touch_down;
  }
  s_last_anim_ms = now_ms;
  s_wave_phase += 0.11f;
  return true;
}

void pm_face_tibetan_bowl_stop(void) {
  pm_speaker_bowl_voice_stop();
  s_touch_down = false;
  s_have_ang = false;
  s_block_rim_swipe = false;
  s_rim_stroke = false;
  s_trail_len = 0;
}

float pm_face_tibetan_bowl_energy(void) { return pm_speaker_bowl_voice_energy(); }

int pm_face_tibetan_bowl_chakra_index(void) { return s_chakra_idx; }
