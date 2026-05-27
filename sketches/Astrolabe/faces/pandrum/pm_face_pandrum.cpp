#include "faces/pandrum/pm_face_pandrum.h"

#include <Arduino_GFX_Library.h>
#include <cmath>
#include <cstdio>

#include "faces/shared/pm_face_draw.h"
#include "pin_config.h"
#include "pm_display.h"
#include "pm_midi.h"
#include "pm_motion.h"
#include "pm_speaker.h"

static constexpr int kCx = pm_face_lcd_cx;
static constexpr int kCy = pm_face_lcd_cy;
static constexpr int kNoteCount = 14;
static constexpr int kShellR = 204;
static constexpr int kDingR = 54;

struct PanNote {
  const char *name;
  float hz;
  float deg;
  int ring_r;
  int pad_rx;
  int pad_ry;
  uint8_t midi;
};

static const PanNote kNotes[kNoteCount] = {
    {"D3", 146.83f, 0.f, 0, 54, 54, 50},
    {"A3", 220.00f, 270.f, 86, 34, 48, 57},
    {"Bb3", 233.08f, 321.f, 86, 34, 48, 58},
    {"C4", 261.63f, 13.f, 86, 34, 48, 60},
    {"D4", 293.66f, 64.f, 86, 34, 48, 62},
    {"E4", 329.63f, 116.f, 86, 34, 48, 64},
    {"F4", 349.23f, 167.f, 86, 34, 48, 65},
    {"G4", 392.00f, 219.f, 86, 34, 48, 67},
    {"A4", 440.00f, 296.f, 148, 31, 42, 69},
    {"C5", 523.25f, 356.f, 148, 31, 42, 72},
    {"D5", 587.33f, 56.f, 148, 31, 42, 74},
    {"E5", 659.25f, 116.f, 148, 31, 42, 76},
    {"F5", 698.46f, 176.f, 148, 31, 42, 77},
    {"A5", 880.00f, 236.f, 148, 31, 42, 81},
};

static int s_note_idx = -1;
static uint32_t s_note_start_ms = 0;
static uint32_t s_last_anim_ms = 0;
static uint32_t s_last_motion_ms = 0;
static float s_phase = 0.f;
static float s_last_hz = kNotes[0].hz;
static float s_hit_force = 0.50f;
static float s_force_peak_g = 0.f;
static bool s_midi_note_on = false;

static float clamp01(float v) {
  if (v < 0.f) {
    return 0.f;
  }
  if (v > 1.f) {
    return 1.f;
  }
  return v;
}

static uint16_t steel(float dim) {
  const float d = clamp01(dim);
  return pm_gfx->color565(static_cast<uint8_t>(112.f * d), static_cast<uint8_t>(142.f * d),
                          static_cast<uint8_t>(156.f * d));
}

static uint16_t ember(float dim) {
  const float d = clamp01(dim);
  return pm_gfx->color565(static_cast<uint8_t>(246.f * d), static_cast<uint8_t>(172.f * d),
                          static_cast<uint8_t>(82.f * d));
}

static void note_xy(const PanNote &n, int *x, int *y) {
  if (n.ring_r == 0) {
    *x = kCx;
    *y = kCy;
    return;
  }
  const float a = pm_face_deg_to_rad(n.deg);
  *x = kCx + static_cast<int>(sinf(a) * static_cast<float>(n.ring_r));
  *y = kCy - static_cast<int>(cosf(a) * static_cast<float>(n.ring_r));
}

static float note_energy(void) {
  if (s_note_idx < 0) {
    return 0.f;
  }
  const uint32_t age = millis() - s_note_start_ms;
  if (age > 860u && !pm_speaker_is_playing()) {
    return 0.f;
  }
  return clamp01(1.f - static_cast<float>(age) / 860.f);
}

static float force_from_imu(void) {
  float ax = 0.f;
  float ay = 0.f;
  float az = 0.f;
  float excess_g = s_force_peak_g;
  if (!pm_motion_accel_g(&ax, &ay, &az)) {
    return 0.50f;
  }
  const float mag = sqrtf(ax * ax + ay * ay + az * az);
  const float now_excess = fabsf(mag - 1.f);
  if (now_excess > excess_g) {
    excess_g = now_excess;
  }
  s_force_peak_g = 0.f;
  return 0.32f + 0.68f * clamp01(excess_g / 1.20f);
}

static void midi_note_off_current(void) {
  if (s_midi_note_on && s_note_idx >= 0) {
    (void)pm_midi_note_off(PmMidiInstrument::PanDrum, kNotes[s_note_idx].midi);
  }
  s_midi_note_on = false;
}

static bool hit_note(int16_t x, int16_t y, int *out_idx) {
  const int cdx = static_cast<int>(x) - kCx;
  const int cdy = static_cast<int>(y) - kCy;
  if (cdx * cdx + cdy * cdy <= (kDingR + 18) * (kDingR + 18)) {
    *out_idx = 0;
    return true;
  }

  int best = -1;
  float best_score = 0.f;
  for (int i = 1; i < kNoteCount; ++i) {
    int px = 0;
    int py = 0;
    note_xy(kNotes[i], &px, &py);
    const float dx = static_cast<float>(x - px);
    const float dy = static_cast<float>(y - py);
    const float rx = static_cast<float>(kNotes[i].pad_rx + 14);
    const float ry = static_cast<float>(kNotes[i].pad_ry + 14);
    const float score = (dx * dx) / (rx * rx) + (dy * dy) / (ry * ry);
    if (score <= 1.f && (best < 0 || score < best_score)) {
      best = i;
      best_score = score;
    }
  }
  if (best < 0) {
    return false;
  }
  *out_idx = best;
  return true;
}

static void draw_label_at_xy(const char *text, int x, int y, uint16_t col, uint8_t sx, uint8_t sy) {
  pm_gfx->setTextSize(sx, sy);
  pm_gfx->setTextColor(col);
  int16_t x1 = 0;
  int16_t y1 = 0;
  uint16_t w = 0;
  uint16_t h = 0;
  pm_gfx->getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  pm_gfx->setCursor(x - static_cast<int>(w) / 2, y - static_cast<int>(h) / 2);
  pm_gfx->print(text);
}

static void draw_shell(float energy) {
  pm_gfx->fillScreen(pm_gfx->color565(4, 8, 10));
  pm_gfx->fillCircle(kCx, kCy + 10, kShellR + 12, pm_gfx->color565(3, 5, 6));
  pm_gfx->fillCircle(kCx, kCy, kShellR, steel(0.74f + 0.10f * energy));
  pm_gfx->fillCircle(kCx - 34, kCy - 44, 76, steel(0.94f));
  pm_gfx->fillCircle(kCx, kCy, kShellR - 8, steel(0.58f + 0.12f * energy));
  pm_gfx->drawCircle(kCx, kCy, kShellR, steel(1.f));
  pm_gfx->drawCircle(kCx, kCy, kShellR - 18, steel(0.38f));
  pm_gfx->drawCircle(kCx, kCy, 128, steel(0.34f));
}

static void draw_tone_field(int idx, float energy) {
  const PanNote &n = kNotes[idx];
  int x = 0;
  int y = 0;
  note_xy(n, &x, &y);
  const bool active = idx == s_note_idx && energy > 0.02f;
  const float force = active ? s_hit_force : 0.f;
  const float pulse = active ? energy * (0.70f + 0.55f * force) : 0.f;
  const uint16_t pad = active ? ember(0.82f + 0.18f * energy) : steel(idx == 0 ? 0.72f : 0.82f);
  const uint16_t cut = active ? ember(0.55f) : steel(0.30f);
  const uint16_t label = active ? pm_gfx->color565(38, 22, 6) : pm_gfx->color565(225, 238, 232);
  const int rx = n.pad_rx + static_cast<int>(5.f * pulse);
  const int ry = n.pad_ry + static_cast<int>(4.f * pulse);

  if (idx == 0) {
    pm_gfx->fillCircle(x, y, rx + 7, cut);
    pm_gfx->fillCircle(x, y, rx, pad);
    pm_gfx->drawCircle(x, y, rx + 14 + static_cast<int>(10.f * pulse), cut);
  } else {
    pm_gfx->fillEllipse(x, y, rx + 8, ry + 8, cut);
    pm_gfx->fillEllipse(x, y, rx, ry, pad);
    pm_gfx->drawEllipse(x, y, rx + 13 + static_cast<int>(7.f * pulse), ry + 12, cut);
    pm_gfx->drawLine(x - rx + 8, y, x + rx - 8, y, steel(0.26f));
  }
  draw_label_at_xy(n.name, x, y, label, idx == 0 ? 2 : 1, idx == 0 ? 2 : 1);
}

static void draw_resonance(float energy) {
  if (energy <= 0.02f || s_note_idx < 0) {
    return;
  }
  int x = 0;
  int y = 0;
  note_xy(kNotes[s_note_idx], &x, &y);
  const uint16_t glow = ember(0.42f + 0.42f * energy);
  for (int i = 0; i < 3; ++i) {
    const int r = 28 + i * 34 + static_cast<int>((1.f - energy) * (22.f + 30.f * s_hit_force));
    pm_gfx->drawCircle(x, y, r, glow);
  }
  pm_gfx->drawCircle(kCx, kCy, 150 + static_cast<int>(sinf(s_phase) * (4.f + 8.f * s_hit_force)), glow);
}

void pm_face_pandrum_draw(void) {
  const float energy = note_energy();
  draw_shell(energy);
  for (int i = 8; i < kNoteCount; ++i) {
    draw_tone_field(i, energy);
  }
  for (int i = 1; i <= 7; ++i) {
    draw_tone_field(i, energy);
  }
  draw_tone_field(0, energy);
  draw_resonance(energy);

  pm_face_draw_centered_line("PanDrum", 38, pm_gfx->color565(230, 238, 232), 2, 2);
  if (s_note_idx >= 0 && energy > 0.02f) {
    char line[24];
    snprintf(line, sizeof(line), "%s %.0fHz %u%%", kNotes[s_note_idx].name,
             static_cast<double>(kNotes[s_note_idx].hz),
             static_cast<unsigned>(s_hit_force * 100.f));
    pm_face_draw_centered_line(line, 406, ember(0.95f), 1, 2);
  } else {
    pm_face_draw_centered_line("14-note handpan", 406, steel(0.72f), 1, 2);
  }
  pm_face_draw_centered_line("tap tone fields", 430, steel(0.50f), 1, 1);
}

bool pm_face_pandrum_play_at(int16_t x, int16_t y) {
  int idx = -1;
  if (!hit_note(x, y, &idx)) {
    return false;
  }
  midi_note_off_current();
  s_note_idx = idx;
  s_note_start_ms = millis();
  s_last_anim_ms = 0;
  s_phase = 0.f;
  s_last_hz = kNotes[idx].hz;
  s_hit_force = force_from_imu();
  const uint32_t duration_ms = 540u + static_cast<uint32_t>(360.f * s_hit_force);
  (void)pm_midi_note_on(PmMidiInstrument::PanDrum, kNotes[idx].midi,
                        static_cast<uint8_t>(72 + static_cast<int>(44.f * s_hit_force)));
  s_midi_note_on = true;
  return pm_speaker_play_synth_note_begin(kNotes[idx].hz, duration_ms + 220u, PmSynthPatch::PanDrum,
                                          0.72f + 0.34f * s_hit_force);
}

bool pm_face_pandrum_motion_tick(uint32_t now_ms) {
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

bool pm_face_pandrum_anim_tick(uint32_t now_ms) {
  if (s_note_idx < 0 || note_energy() <= 0.02f) {
    midi_note_off_current();
    return false;
  }
  if (now_ms - s_last_anim_ms < 42u) {
    return false;
  }
  s_last_anim_ms = now_ms;
  s_phase += 0.42f;
  return true;
}

void pm_face_pandrum_stop(void) {
  midi_note_off_current();
  if (pm_speaker_is_playing()) {
    pm_speaker_abort();
    pm_speaker_tone_stop();
  }
  s_note_idx = -1;
}

const char *pm_face_pandrum_note_label(void) {
  return (s_note_idx >= 0 && s_note_idx < kNoteCount) ? kNotes[s_note_idx].name : "";
}

int pm_face_pandrum_note_index(void) { return s_note_idx; }

float pm_face_pandrum_last_hz(void) { return s_last_hz; }

float pm_face_pandrum_last_force(void) { return s_hit_force; }
