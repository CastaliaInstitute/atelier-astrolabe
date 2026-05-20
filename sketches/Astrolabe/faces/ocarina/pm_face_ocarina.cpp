#include "faces/ocarina/pm_face_ocarina.h"

#include <Arduino_GFX_Library.h>
#include <cmath>
#include <cstdio>

#include "faces/shared/pm_face_draw.h"
#include "pin_config.h"
#include "pm_display.h"
#include "pm_speaker.h"

static constexpr int kCx = pm_face_lcd_cx;
static constexpr int kCy = pm_face_lcd_cy;
static constexpr int kHoleCount = 6;

struct OcarinaKey {
  const char *name;
  float root_hz;
};

struct OcarinaHole {
  int x;
  int y;
  int r;
  int semitone;
  const char *degree;
};

static const OcarinaKey kKeys[] = {
    {"C", 261.63f},
    {"D", 293.66f},
    {"F", 349.23f},
    {"G", 392.00f},
    {"A", 440.00f},
};

static const OcarinaHole kHoles[kHoleCount] = {
    {kCx - 70, kCy - 22, 18, 0, "1"},
    {kCx - 24, kCy - 42, 17, 2, "2"},
    {kCx + 25, kCy - 34, 18, 4, "3"},
    {kCx + 72, kCy - 8, 17, 7, "5"},
    {kCx - 28, kCy + 28, 15, 9, "6"},
    {kCx + 30, kCy + 34, 15, 12, "8"},
};

static int s_key_idx = 0;
static int s_note_idx = -1;
static uint32_t s_note_start_ms = 0;
static uint32_t s_last_anim_ms = 0;
static float s_phase = 0.f;

static uint16_t clay(float dim) {
  const float d = dim < 0.f ? 0.f : (dim > 1.f ? 1.f : dim);
  return pm_gfx->color565(static_cast<uint8_t>(216.f * d), static_cast<uint8_t>(104.f * d),
                          static_cast<uint8_t>(55.f * d));
}

static uint16_t jade(float dim) {
  const float d = dim < 0.f ? 0.f : (dim > 1.f ? 1.f : dim);
  return pm_gfx->color565(static_cast<uint8_t>(64.f * d), static_cast<uint8_t>(220.f * d),
                          static_cast<uint8_t>(170.f * d));
}

static float note_hz(int hole_idx) {
  if (hole_idx < 0 || hole_idx >= kHoleCount) {
    return kKeys[s_key_idx].root_hz;
  }
  return kKeys[s_key_idx].root_hz * powf(2.f, static_cast<float>(kHoles[hole_idx].semitone) / 12.f);
}

static bool hit_hole(int16_t x, int16_t y, int *out_idx) {
  int best = -1;
  int best_d2 = 0;
  for (int i = 0; i < kHoleCount; ++i) {
    const int dx = static_cast<int>(x) - kHoles[i].x;
    const int dy = static_cast<int>(y) - kHoles[i].y;
    const int d2 = dx * dx + dy * dy;
    const int hit_r = kHoles[i].r + 18;
    if (d2 <= hit_r * hit_r && (best < 0 || d2 < best_d2)) {
      best = i;
      best_d2 = d2;
    }
  }
  if (best < 0) {
    return false;
  }
  *out_idx = best;
  return true;
}

static float note_energy(void) {
  if (s_note_idx < 0) {
    return 0.f;
  }
  const uint32_t age = millis() - s_note_start_ms;
  if (age > 820u && !pm_speaker_is_playing()) {
    return 0.f;
  }
  const float t = static_cast<float>(age) / 820.f;
  const float e = 1.f - t;
  return e < 0.f ? 0.f : e;
}

static void draw_breath_rings(float energy) {
  if (energy <= 0.02f) {
    return;
  }
  const uint16_t ring = jade(0.32f + 0.42f * energy);
  for (int i = 0; i < 4; ++i) {
    const int r = 86 + i * 23 + static_cast<int>(sinf(s_phase + static_cast<float>(i)) * 5.f);
    pm_gfx->drawCircle(kCx, kCy, r, ring);
  }
}

static void draw_ocarina_body(float energy) {
  const int breath = static_cast<int>(5.f * energy * sinf(s_phase));
  const uint16_t shadow = pm_gfx->color565(18, 10, 8);
  const uint16_t body = clay(0.72f + 0.12f * energy);
  const uint16_t body_hi = clay(0.98f);
  const uint16_t edge = clay(0.42f);

  pm_gfx->fillEllipse(kCx + 4, kCy + 18, 132, 86, shadow);
  pm_gfx->fillRoundRect(kCx - 168, kCy - 74, 110, 42, 16, shadow);
  pm_gfx->fillRoundRect(kCx - 174, kCy - 85, 124, 44, 16, body);
  pm_gfx->drawRoundRect(kCx - 174, kCy - 85, 124, 44, 16, edge);
  pm_gfx->fillTriangle(kCx - 62, kCy - 82, kCx - 24, kCy - 50, kCx - 62, kCy - 44, body);

  pm_gfx->fillEllipse(kCx + 8, kCy + 4, 126 + breath, 82, body);
  pm_gfx->drawEllipse(kCx + 8, kCy + 4, 128 + breath, 84, edge);
  pm_gfx->fillEllipse(kCx - 20, kCy - 30, 54, 20, body_hi);
  pm_gfx->fillEllipse(kCx + 86, kCy - 42, 24, 18, clay(0.58f));
  pm_gfx->drawEllipse(kCx + 86, kCy - 42, 25, 19, edge);
}

static void draw_holes(float energy) {
  const uint16_t rim = pm_gfx->color565(242, 176, 112);
  const uint16_t dark = pm_gfx->color565(18, 9, 8);
  const uint16_t active = jade(0.90f);
  for (int i = 0; i < kHoleCount; ++i) {
    const OcarinaHole &h = kHoles[i];
    const bool on = i == s_note_idx && energy > 0.02f;
    const int grow = on ? static_cast<int>(5.f * energy) : 0;
    pm_gfx->fillCircle(h.x, h.y, h.r + grow + 3, on ? active : rim);
    pm_gfx->fillCircle(h.x, h.y, h.r + grow, dark);
    if (on) {
      pm_gfx->drawCircle(h.x, h.y, h.r + 8 + grow, active);
    }
    pm_face_draw_centered_line(h.degree, h.y - 5, on ? active : pm_gfx->color565(112, 70, 48), 1, 1);
  }
}

void pm_face_ocarina_draw(void) {
  const float energy = note_energy();
  pm_gfx->fillScreen(pm_gfx->color565(6, 9, 12));
  draw_breath_rings(energy);
  draw_ocarina_body(energy);
  draw_holes(energy);

  char line[28];
  snprintf(line, sizeof(line), "Ocarina %s", kKeys[s_key_idx].name);
  pm_face_draw_centered_line(line, 56, clay(0.96f), 2, 2);
  if (s_note_idx >= 0 && energy > 0.02f) {
    snprintf(line, sizeof(line), "%s %.0f Hz", kHoles[s_note_idx].degree, note_hz(s_note_idx));
    pm_face_draw_centered_line(line, 390, jade(0.80f), 1, 2);
  } else {
    pm_face_draw_centered_line("tap holes", 390, clay(0.62f), 1, 2);
  }
  pm_face_draw_centered_line("swipe up/down key", 416, clay(0.42f), 1, 1);
}

bool pm_face_ocarina_play_at(int16_t x, int16_t y) {
  int idx = -1;
  if (!hit_hole(x, y, &idx)) {
    idx = (s_note_idx + 1) % kHoleCount;
  }
  s_note_idx = idx;
  s_note_start_ms = millis();
  s_last_anim_ms = 0;
  s_phase = 0.f;
  return pm_speaker_play_tone_begin(note_hz(idx), 620);
}

void pm_face_ocarina_cycle_key(int delta) {
  const int n = static_cast<int>(sizeof(kKeys) / sizeof(kKeys[0]));
  int v = (s_key_idx + delta) % n;
  if (v < 0) {
    v += n;
  }
  s_key_idx = v;
}

bool pm_face_ocarina_anim_tick(uint32_t now_ms) {
  if (s_note_idx < 0 || note_energy() <= 0.02f) {
    return false;
  }
  if (now_ms - s_last_anim_ms < 45u) {
    return false;
  }
  s_last_anim_ms = now_ms;
  s_phase += 0.34f;
  return true;
}

void pm_face_ocarina_stop(void) {
  if (pm_speaker_is_playing()) {
    pm_speaker_abort();
    pm_speaker_tone_stop();
  }
  s_note_idx = -1;
}

const char *pm_face_ocarina_key_label(void) { return kKeys[s_key_idx].name; }

int pm_face_ocarina_note_index(void) { return s_note_idx; }
