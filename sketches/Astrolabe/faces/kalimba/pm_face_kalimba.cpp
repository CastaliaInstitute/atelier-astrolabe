#include "faces/kalimba/pm_face_kalimba.h"

#include <Arduino_GFX_Library.h>
#include <cmath>
#include <cstdio>

#include "faces/shared/pm_face_draw.h"
#include "pin_config.h"
#include "pm_display.h"
#include "pm_midi.h"
#include "pm_speaker.h"

static constexpr int kCx = pm_face_lcd_cx;
static constexpr int kCy = pm_face_lcd_cy;
static constexpr int kTineCount = 9;

struct KalimbaTine {
  const char *name;
  float hz;
  uint8_t midi;
  int x;
  int len;
};

static const KalimbaTine kTines[kTineCount] = {
    {"C4", 261.63f, 60, -120, 132}, {"D4", 293.66f, 62, -90, 154}, {"E4", 329.63f, 64, -60, 176},
    {"G4", 392.00f, 67, -30, 202},  {"A4", 440.00f, 69, 0, 224},   {"C5", 523.25f, 72, 30, 202},
    {"D5", 587.33f, 74, 60, 176},   {"E5", 659.25f, 76, 90, 154},  {"G5", 783.99f, 79, 120, 132},
};

static int s_note_idx = -1;
static uint32_t s_note_ms = 0;
static uint32_t s_last_anim_ms = 0;
static float s_phase = 0.f;
static bool s_midi_note_on = false;

static uint16_t wood(float d) {
  return pm_gfx->color565(static_cast<uint8_t>(150.f * d), static_cast<uint8_t>(80.f * d),
                          static_cast<uint8_t>(38.f * d));
}

static float energy(void) {
  if (s_note_idx < 0) {
    return 0.f;
  }
  const uint32_t age = millis() - s_note_ms;
  if (age > 760u && !pm_speaker_is_playing()) {
    return 0.f;
  }
  const float e = 1.f - static_cast<float>(age) / 760.f;
  return e < 0.f ? 0.f : e;
}

static void midi_note_off_current(void) {
  if (s_midi_note_on && s_note_idx >= 0) {
    (void)pm_midi_note_off(PmMidiInstrument::Kalimba, kTines[s_note_idx].midi);
  }
  s_midi_note_on = false;
}

static bool hit_tine(int16_t x, int16_t y, int *idx) {
  int best = -1;
  int best_d = 0;
  for (int i = 0; i < kTineCount; ++i) {
    const int tx = kCx + kTines[i].x;
    const int top = kCy - 124;
    const int bottom = top + kTines[i].len;
    if (y < top - 18 || y > bottom + 26) {
      continue;
    }
    const int d = abs(static_cast<int>(x) - tx);
    if (d <= 20 && (best < 0 || d < best_d)) {
      best = i;
      best_d = d;
    }
  }
  if (best < 0) {
    return false;
  }
  *idx = best;
  return true;
}

void pm_face_kalimba_draw(void) {
  const float e = energy();
  pm_gfx->fillScreen(pm_gfx->color565(8, 6, 5));
  pm_gfx->fillRoundRect(kCx - 168, kCy - 116, 336, 270, 22, wood(0.78f));
  pm_gfx->drawRoundRect(kCx - 168, kCy - 116, 336, 270, 22, wood(1.05f));
  pm_gfx->fillCircle(kCx, kCy + 42, 46, pm_gfx->color565(36, 17, 8));
  pm_gfx->drawCircle(kCx, kCy + 42, 50, wood(0.42f));
  pm_gfx->fillRoundRect(kCx - 148, kCy - 132, 296, 28, 9, pm_gfx->color565(78, 62, 50));

  for (int i = 0; i < kTineCount; ++i) {
    const bool active = i == s_note_idx && e > 0.02f;
    const int x = kCx + kTines[i].x + (active ? static_cast<int>(sinf(s_phase) * 5.f * e) : 0);
    const int top = kCy - 122;
    const int bottom = top + kTines[i].len;
    const uint16_t metal = active ? pm_gfx->color565(236, 230, 164) : pm_gfx->color565(188, 198, 198);
    pm_gfx->fillRoundRect(x - 10, top, 20, bottom - top, 7, metal);
    pm_gfx->drawRoundRect(x - 10, top, 20, bottom - top, 7, pm_gfx->color565(80, 84, 88));
    pm_face_draw_centered_line(kTines[i].name, bottom + 12, active ? metal : wood(0.34f), 1, 1);
  }

  pm_face_draw_centered_line("Kalimba", 38, pm_gfx->color565(236, 218, 178), 2, 2);
  pm_face_draw_centered_line(s_note_idx >= 0 && e > 0.02f ? kTines[s_note_idx].name : "pentatonic tines", 414,
                             pm_gfx->color565(220, 204, 150), 1, 2);
}

bool pm_face_kalimba_play_at(int16_t x, int16_t y) {
  int idx = -1;
  if (!hit_tine(x, y, &idx)) {
    idx = (s_note_idx + 1) % kTineCount;
  }
  midi_note_off_current();
  s_note_idx = idx;
  s_note_ms = millis();
  s_last_anim_ms = 0;
  s_phase = 0.f;
  (void)pm_midi_note_on(PmMidiInstrument::Kalimba, kTines[idx].midi, 104);
  s_midi_note_on = true;
  return pm_speaker_play_synth_note_begin(kTines[idx].hz, 720, PmSynthPatch::Kalimba, 0.92f);
}

bool pm_face_kalimba_anim_tick(uint32_t now_ms) {
  if (s_note_idx < 0 || energy() <= 0.02f) {
    midi_note_off_current();
    return false;
  }
  if (now_ms - s_last_anim_ms < 38u) {
    return false;
  }
  s_last_anim_ms = now_ms;
  s_phase += 0.66f;
  return true;
}

void pm_face_kalimba_stop(void) {
  midi_note_off_current();
  if (pm_speaker_is_playing()) {
    pm_speaker_abort();
    pm_speaker_tone_stop();
  }
  s_note_idx = -1;
}

const char *pm_face_kalimba_note_label(void) {
  return (s_note_idx >= 0 && s_note_idx < kTineCount) ? kTines[s_note_idx].name : "";
}
