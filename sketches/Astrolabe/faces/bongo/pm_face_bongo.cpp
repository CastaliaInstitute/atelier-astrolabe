#include "faces/bongo/pm_face_bongo.h"

#include <Arduino_GFX_Library.h>
#include <cmath>
#include <cstdio>

#include "faces/shared/pm_face_draw.h"
#include "pin_config.h"
#include "pm_display.h"
#include "pm_motion.h"
#include "pm_speaker.h"

static constexpr int kCx = pm_face_lcd_cx;
static constexpr int kCy = pm_face_lcd_cy;
static constexpr int kDrumRadius = 170;
static constexpr int kInnerRadius = 72;
static constexpr float kLowHz = 148.f;
static constexpr float kHighHz = 420.f;

static int16_t s_hit_x = kCx;
static int16_t s_hit_y = kCy;
static float s_hit_r_norm = 0.f;
static float s_hit_force = 0.55f;
static float s_last_hz = kLowHz;
static uint32_t s_hit_ms = 0;
static uint32_t s_last_anim_ms = 0;
static uint32_t s_last_motion_ms = 0;
static float s_phase = 0.f;
static float s_force_peak_g = 0.f;

static float clamp01(float v) {
  if (v < 0.f) {
    return 0.f;
  }
  if (v > 1.f) {
    return 1.f;
  }
  return v;
}

static uint16_t skin(float dim) {
  const float d = clamp01(dim);
  return pm_gfx->color565(static_cast<uint8_t>(232.f * d), static_cast<uint8_t>(180.f * d),
                          static_cast<uint8_t>(124.f * d));
}

static uint16_t wood(float dim) {
  const float d = clamp01(dim);
  return pm_gfx->color565(static_cast<uint8_t>(158.f * d), static_cast<uint8_t>(78.f * d),
                          static_cast<uint8_t>(36.f * d));
}

static float hit_energy(void) {
  if (s_hit_ms == 0) {
    return 0.f;
  }
  const uint32_t age = millis() - s_hit_ms;
  if (age > 720u && !pm_speaker_is_playing()) {
    return 0.f;
  }
  const float t = static_cast<float>(age) / 720.f;
  return clamp01(1.f - t);
}

static float pitch_from_radius(float r_norm) {
  const float shaped = sqrtf(clamp01(r_norm));
  return kLowHz + (kHighHz - kLowHz) * shaped;
}

static float force_from_imu(void) {
  float ax = 0.f;
  float ay = 0.f;
  float az = 0.f;
  float excess_g = s_force_peak_g;
  if (!pm_motion_accel_g(&ax, &ay, &az)) {
    return 0.55f;
  } else {
    const float mag = sqrtf(ax * ax + ay * ay + az * az);
    const float now_excess = fabsf(mag - 1.f);
    if (now_excess > excess_g) {
      excess_g = now_excess;
    }
  }
  s_force_peak_g = 0.f;
  return 0.38f + 0.62f * clamp01(excess_g / 1.35f);
}

static void draw_head(float energy) {
  const uint16_t bg = pm_gfx->color565(8, 7, 8);
  const uint16_t shadow = pm_gfx->color565(24, 12, 8);
  pm_gfx->fillScreen(bg);
  pm_gfx->fillEllipse(kCx, kCy + 36, kDrumRadius + 22, kDrumRadius - 14, shadow);
  pm_gfx->fillCircle(kCx, kCy, kDrumRadius + 15, wood(0.55f));
  pm_gfx->fillCircle(kCx, kCy, kDrumRadius + 4, wood(0.84f));
  pm_gfx->fillCircle(kCx, kCy, kDrumRadius - 12, skin(0.93f + 0.07f * energy));
  pm_gfx->drawCircle(kCx, kCy, kDrumRadius - 11, skin(0.58f));
  pm_gfx->drawCircle(kCx, kCy, kInnerRadius, skin(0.58f));
  pm_gfx->drawCircle(kCx, kCy, kInnerRadius + 1, skin(0.45f));

  for (int i = 0; i < 12; ++i) {
    const float a = static_cast<float>(i) * (6.2831853f / 12.f);
    const int x0 = kCx + static_cast<int>(cosf(a) * static_cast<float>(kDrumRadius - 5));
    const int y0 = kCy + static_cast<int>(sinf(a) * static_cast<float>(kDrumRadius - 5));
    pm_gfx->fillCircle(x0, y0, 6, wood(0.30f));
    pm_gfx->drawCircle(x0, y0, 8, skin(0.55f));
  }
}

static void draw_pitch_rings(float energy) {
  for (int i = 1; i <= 4; ++i) {
    const int r = (kDrumRadius * i) / 4;
    const float t = static_cast<float>(i) / 4.f;
    const uint16_t c = pm_face_color565_from_hsv(pm_gfx, 35.f + 145.f * t, 0.38f, 0.22f + 0.16f * energy);
    pm_gfx->drawCircle(kCx, kCy, r, c);
  }
}

static void draw_hit(float energy) {
  if (energy <= 0.02f) {
    return;
  }
  const uint16_t accent = pm_face_color565_from_hsv(pm_gfx, 32.f + 150.f * s_hit_r_norm, 0.78f,
                                                   0.62f + 0.30f * energy);
  const int force_grow = static_cast<int>(10.f * s_hit_force);
  const int pulse = static_cast<int>((1.f - energy) * (42.f + 34.f * s_hit_force));
  pm_gfx->fillCircle(s_hit_x, s_hit_y, 10 + force_grow + static_cast<int>(8.f * energy), accent);
  pm_gfx->drawCircle(s_hit_x, s_hit_y, 18 + force_grow + pulse, accent);
  pm_gfx->drawCircle(kCx, kCy,
                     kInnerRadius + static_cast<int>(sinf(s_phase) * (3.f + 5.f * s_hit_force) * energy),
                     accent);
}

void pm_face_bongo_draw(void) {
  const float energy = hit_energy();
  draw_head(energy);
  draw_pitch_rings(energy);
  draw_hit(energy);

  char line[28];
  pm_face_draw_centered_line("Bongo", 52, wood(1.f), 2, 2);
  if (s_hit_ms != 0 && energy > 0.02f) {
    snprintf(line, sizeof(line), "%.0f Hz  %u%%", static_cast<double>(s_last_hz),
             static_cast<unsigned>(s_hit_force * 100.f));
    pm_face_draw_centered_line(line, 392, pm_face_color565_from_hsv(pm_gfx, 32.f + 150.f * s_hit_r_norm, 0.72f,
                                                                   0.90f),
                               1, 2);
  } else {
    pm_face_draw_centered_line("center low / rim high", 392, wood(0.62f), 1, 1);
  }
  pm_face_draw_centered_line("tap distance sets pitch", 418, skin(0.48f), 1, 1);
}

bool pm_face_bongo_play_at(int16_t x, int16_t y) {
  const float dx = static_cast<float>(x - kCx);
  const float dy = static_cast<float>(y - kCy);
  const float r = sqrtf(dx * dx + dy * dy);
  s_hit_r_norm = clamp01(r / static_cast<float>(kDrumRadius));
  if (r > static_cast<float>(kDrumRadius) && r > 0.1f) {
    const float scale = static_cast<float>(kDrumRadius) / r;
    s_hit_x = static_cast<int16_t>(kCx + static_cast<int>(dx * scale));
    s_hit_y = static_cast<int16_t>(kCy + static_cast<int>(dy * scale));
  } else {
    s_hit_x = x;
    s_hit_y = y;
  }
  s_last_hz = pitch_from_radius(s_hit_r_norm);
  s_hit_force = force_from_imu();
  s_hit_ms = millis();
  s_last_anim_ms = 0;
  s_phase = 0.f;
  const float strength = clamp01((0.64f - 0.08f * s_hit_r_norm) + 0.62f * s_hit_force);
  return pm_speaker_play_bongo_begin(s_last_hz, strength);
}

bool pm_face_bongo_motion_tick(uint32_t now_ms) {
  if (now_ms - s_last_motion_ms < 12u) {
    return false;
  }
  s_last_motion_ms = now_ms;

  float ax = 0.f;
  float ay = 0.f;
  float az = 0.f;
  if (!pm_motion_accel_g(&ax, &ay, &az)) {
    return false;
  }
  const float mag = sqrtf(ax * ax + ay * ay + az * az);
  const float excess_g = fabsf(mag - 1.f);
  s_force_peak_g *= 0.88f;
  if (excess_g > s_force_peak_g) {
    s_force_peak_g = excess_g;
  }
  return false;
}

bool pm_face_bongo_anim_tick(uint32_t now_ms) {
  if (hit_energy() <= 0.02f) {
    return false;
  }
  if (now_ms - s_last_anim_ms < 38u) {
    return false;
  }
  s_last_anim_ms = now_ms;
  s_phase += 0.58f;
  return true;
}

void pm_face_bongo_stop(void) {
  if (pm_speaker_is_playing()) {
    pm_speaker_abort();
  }
  s_hit_ms = 0;
}

float pm_face_bongo_last_hz(void) { return s_last_hz; }

float pm_face_bongo_last_radius_norm(void) { return s_hit_r_norm; }

float pm_face_bongo_last_force(void) { return s_hit_force; }
