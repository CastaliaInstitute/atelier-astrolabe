#include "faces/piano/pm_face_piano.h"

#include <Arduino_GFX_Library.h>
#include <cmath>
#include <cstdio>

#include "faces/shared/pm_face_draw.h"
#include "pin_config.h"
#include "pm_display.h"
#include "pm_speaker.h"

static constexpr int kCx = pm_face_lcd_cx;
static constexpr int kCy = pm_face_lcd_cy;
static constexpr int kWhiteCount = 7;
static constexpr int kBlackCount = 5;
static constexpr int kNoteCount = 12;
static constexpr int kOuterR = pm_face_scale_i(214);
static constexpr int kWhiteInnerR = pm_face_scale_i(104);
static constexpr int kBlackOuterR = pm_face_scale_i(132);
static constexpr int kBlackInnerR = pm_face_scale_i(46);
static constexpr float kWhiteSpan = 360.f / static_cast<float>(kWhiteCount);

struct PianoNote {
  const char *name;
  float hz;
  int semitone;
};

struct BlackKey {
  int note_idx;
  float center_deg;
};

static const PianoNote kNotes[kNoteCount] = {
    {"C", 261.63f, 0},   {"C#", 277.18f, 1}, {"D", 293.66f, 2},   {"D#", 311.13f, 3},
    {"E", 329.63f, 4},   {"F", 349.23f, 5},  {"F#", 369.99f, 6},  {"G", 392.00f, 7},
    {"G#", 415.30f, 8},  {"A", 440.00f, 9},  {"A#", 466.16f, 10}, {"B", 493.88f, 11},
};

static const int kWhiteNoteIdx[kWhiteCount] = {0, 2, 4, 5, 7, 9, 11};
static const BlackKey kBlackKeys[kBlackCount] = {
    {1, kWhiteSpan * 0.5f},
    {3, kWhiteSpan * 1.5f},
    {6, kWhiteSpan * 3.5f},
    {8, kWhiteSpan * 4.5f},
    {10, kWhiteSpan * 5.5f},
};

static int s_note_idx = -1;
static uint32_t s_note_start_ms = 0;
static uint32_t s_last_anim_ms = 0;
static float s_phase = 0.f;

static uint16_t key_white(bool active, float energy) {
  if (active) {
    return pm_gfx->color565(255, 236, 172 + static_cast<uint8_t>(40.f * energy));
  }
  return pm_gfx->color565(238, 236, 222);
}

static uint16_t key_black(bool active, float energy) {
  if (active) {
    return pm_gfx->color565(40, 190 + static_cast<uint8_t>(45.f * energy), 210);
  }
  return pm_gfx->color565(10, 13, 20);
}

static float note_energy(void) {
  if (s_note_idx < 0) {
    return 0.f;
  }
  const uint32_t age = millis() - s_note_start_ms;
  if (age > 720u && !pm_speaker_is_playing()) {
    return 0.f;
  }
  const float t = static_cast<float>(age) / 720.f;
  const float e = 1.f - t;
  return e < 0.f ? 0.f : e;
}

static float point_deg(int16_t x, int16_t y, float *out_r) {
  const float dx = static_cast<float>(x - kCx);
  const float dy = static_cast<float>(y - kCy);
  if (out_r) {
    *out_r = sqrtf(dx * dx + dy * dy);
  }
  float deg = atan2f(dy, dx) * 180.f / pm_face_k_pi + 90.f;
  while (deg < 0.f) {
    deg += 360.f;
  }
  while (deg >= 360.f) {
    deg -= 360.f;
  }
  return deg;
}

static float angle_delta_deg(float a, float b) {
  float d = fabsf(a - b);
  if (d > 180.f) {
    d = 360.f - d;
  }
  return d;
}

static bool hit_note(int16_t x, int16_t y, int *out_idx) {
  float r = 0.f;
  const float deg = point_deg(x, y, &r);
  if (r < kBlackInnerR || r > kOuterR + 16) {
    return false;
  }

  if (r <= kBlackOuterR + 12) {
    for (const BlackKey &bk : kBlackKeys) {
      if (angle_delta_deg(deg, bk.center_deg) <= 13.5f && r >= kBlackInnerR - 8) {
        *out_idx = bk.note_idx;
        return true;
      }
    }
  }

  if (r < kWhiteInnerR - 10) {
    return false;
  }
  int white = static_cast<int>(floorf((deg + kWhiteSpan * 0.5f) / kWhiteSpan)) % kWhiteCount;
  if (white < 0) {
    white += kWhiteCount;
  }
  *out_idx = kWhiteNoteIdx[white];
  return true;
}

static void draw_label_at_deg(const char *label, float deg, int r, uint16_t col) {
  pm_face_draw_label_at_polar(kCx, kCy, r, pm_face_deg_to_rad(deg), label, col);
}

static void draw_white_keys(float energy) {
  const uint16_t edge = pm_gfx->color565(92, 96, 104);
  for (int i = 0; i < kWhiteCount; ++i) {
    const float center = static_cast<float>(i) * kWhiteSpan;
    const float start = center - kWhiteSpan * 0.5f + 1.8f;
    const float end = center + kWhiteSpan * 0.5f - 1.8f;
    const int note = kWhiteNoteIdx[i];
    const bool active = note == s_note_idx && energy > 0.02f;
    pm_face_draw_annular_wedge(kCx, kCy, kWhiteInnerR - (active ? 4 : 0), kOuterR, start, end,
                               key_white(active, energy));
    pm_face_draw_radial_annulus_slice(kCx, kCy, pm_face_deg_to_rad(start), kWhiteInnerR, kOuterR, edge, 1);
    draw_label_at_deg(kNotes[note].name, center, pm_face_scale_i(176),
                      active ? pm_gfx->color565(56, 35, 12) : pm_gfx->color565(60, 62, 68));
  }
  pm_gfx->drawCircle(kCx, kCy, kOuterR, edge);
  pm_gfx->drawCircle(kCx, kCy, kWhiteInnerR, edge);
}

static void draw_black_keys(float energy) {
  const uint16_t edge = pm_gfx->color565(70, 84, 96);
  for (const BlackKey &bk : kBlackKeys) {
    const bool active = bk.note_idx == s_note_idx && energy > 0.02f;
    const float half_span = active ? 14.8f : 12.8f;
    pm_face_draw_annular_wedge(kCx, kCy, kBlackInnerR, kBlackOuterR + (active ? 8 : 0),
                               bk.center_deg - half_span, bk.center_deg + half_span,
                               key_black(active, energy));
    pm_face_draw_radial_annulus_slice(kCx, kCy, pm_face_deg_to_rad(bk.center_deg - half_span),
                                      kBlackInnerR, kBlackOuterR, edge, 1);
    pm_face_draw_radial_annulus_slice(kCx, kCy, pm_face_deg_to_rad(bk.center_deg + half_span),
                                      kBlackInnerR, kBlackOuterR, edge, 1);
    draw_label_at_deg(kNotes[bk.note_idx].name, bk.center_deg, pm_face_scale_i(92),
                      active ? pm_gfx->color565(4, 18, 22) : pm_gfx->color565(220, 232, 238));
  }
}

static void draw_center(float energy) {
  const uint16_t center = pm_gfx->color565(14, 18, 28);
  const uint16_t glow = pm_gfx->color565(42, 190, 210);
  const int breath = static_cast<int>(energy * (10.f + 4.f * sinf(s_phase)));
  pm_gfx->fillCircle(kCx, kCy, pm_face_scale_i(38) + static_cast<int>(energy * pm_face_scale_i(6)), center);
  pm_gfx->drawCircle(kCx, kCy, pm_face_scale_i(42) + breath, glow);
  if (s_note_idx >= 0 && energy > 0.02f) {
    pm_face_draw_centered_line(kNotes[s_note_idx].name, kCy - 10, glow, 2, 2);
    char hz[16];
    snprintf(hz, sizeof(hz), "%.0f", kNotes[s_note_idx].hz);
    pm_face_draw_centered_line(hz, kCy + 18, pm_gfx->color565(180, 232, 232), 1, 1);
  } else {
    pm_face_draw_centered_line("C", kCy - 10, pm_gfx->color565(210, 220, 224), 2, 2);
    pm_face_draw_centered_line("octave", kCy + 18, pm_gfx->color565(130, 144, 152), 1, 1);
  }
}

void pm_face_piano_draw(void) {
  const float energy = note_energy();
  pm_gfx->fillScreen(pm_gfx->color565(5, 7, 12));
  draw_white_keys(energy);
  draw_black_keys(energy);
  draw_center(energy);

  pm_face_draw_centered_line("Circular Piano", pm_face_scale_y(34), pm_gfx->color565(232, 238, 232), 2, 2);
  pm_face_draw_centered_line("white outer  black inner", pm_face_scale_y(418), pm_gfx->color565(110, 126, 136),
                             1, 1);
}

bool pm_face_piano_play_at(int16_t x, int16_t y) {
  int idx = -1;
  if (!hit_note(x, y, &idx)) {
    return false;
  }
  s_note_idx = idx;
  s_note_start_ms = millis();
  s_last_anim_ms = 0;
  s_phase = 0.f;
  return pm_speaker_play_tone_begin(kNotes[idx].hz, 520);
}

bool pm_face_piano_anim_tick(uint32_t now_ms) {
  if (s_note_idx < 0 || note_energy() <= 0.02f) {
    return false;
  }
  if (now_ms - s_last_anim_ms < 45u) {
    return false;
  }
  s_last_anim_ms = now_ms;
  s_phase += 0.38f;
  return true;
}

void pm_face_piano_stop(void) {
  if (pm_speaker_is_playing()) {
    pm_speaker_abort();
    pm_speaker_tone_stop();
  }
  s_note_idx = -1;
}

const char *pm_face_piano_note_label(void) {
  return (s_note_idx >= 0 && s_note_idx < kNoteCount) ? kNotes[s_note_idx].name : "";
}

int pm_face_piano_note_index(void) { return s_note_idx; }
