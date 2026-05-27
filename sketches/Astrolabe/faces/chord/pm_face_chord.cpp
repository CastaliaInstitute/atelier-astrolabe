#include "faces/chord/pm_face_chord.h"

#include <Arduino_GFX_Library.h>
#include <cmath>

#include "faces/shared/pm_face_draw.h"
#include "pin_config.h"
#include "pm_display.h"
#include "pm_midi.h"
#include "pm_speaker.h"

static constexpr int kCx = pm_face_lcd_cx;
static constexpr int kCy = pm_face_lcd_cy;
static constexpr int kChordCount = 8;

struct ChordPad {
  const char *name;
  uint8_t root;
  int8_t third;
  int8_t fifth;
  int8_t seventh;
};

static const ChordPad kChords[kChordCount] = {
    {"C", 60, 4, 7, -1},  {"Dm", 62, 3, 7, -1}, {"Em", 64, 3, 7, -1}, {"F", 65, 4, 7, -1},
    {"G7", 67, 4, 7, 10}, {"Am", 69, 3, 7, -1}, {"Bb", 70, 4, 7, -1}, {"C8", 72, 4, 7, -1},
};

static int s_chord_idx = -1;
static uint32_t s_chord_ms = 0;
static uint32_t s_last_anim_ms = 0;
static float s_phase = 0.f;
static bool s_midi_on = false;

static float energy(void) {
  if (s_chord_idx < 0) {
    return 0.f;
  }
  const uint32_t age = millis() - s_chord_ms;
  if (age > 980u && !pm_speaker_is_playing()) {
    return 0.f;
  }
  const float e = 1.f - static_cast<float>(age) / 980.f;
  return e < 0.f ? 0.f : e;
}

static void chord_notes(const ChordPad &c, uint8_t *notes, int *count) {
  notes[0] = c.root;
  notes[1] = static_cast<uint8_t>(c.root + c.third);
  notes[2] = static_cast<uint8_t>(c.root + c.fifth);
  *count = 3;
  if (c.seventh >= 0) {
    notes[3] = static_cast<uint8_t>(c.root + c.seventh);
    *count = 4;
  }
}

static void midi_off_current(void) {
  if (!s_midi_on || s_chord_idx < 0) {
    s_midi_on = false;
    return;
  }
  uint8_t notes[4] = {};
  int count = 0;
  chord_notes(kChords[s_chord_idx], notes, &count);
  for (int i = 0; i < count; ++i) {
    (void)pm_midi_note_off(PmMidiInstrument::Chord, notes[i]);
  }
  s_midi_on = false;
}

static int pad_from_point(int16_t x, int16_t y) {
  const float dx = static_cast<float>(x - kCx);
  const float dy = static_cast<float>(y - kCy);
  const float r = sqrtf(dx * dx + dy * dy);
  if (r < 44.f || r > 214.f) {
    return -1;
  }
  float deg = atan2f(dy, dx) * 180.f / pm_face_k_pi + 90.f;
  while (deg < 0.f) {
    deg += 360.f;
  }
  return static_cast<int>(floorf((deg + 22.5f) / 45.f)) % kChordCount;
}

void pm_face_chord_draw(void) {
  const float e = energy();
  pm_gfx->fillScreen(pm_gfx->color565(7, 8, 12));
  for (int i = 0; i < kChordCount; ++i) {
    const bool active = i == s_chord_idx && e > 0.02f;
    const float start = static_cast<float>(i) * 45.f - 22.f;
    const float end = start + 42.f;
    const uint16_t col = pm_face_color565_from_hsv(pm_gfx, 24.f + static_cast<float>(i) * 36.f, 0.58f,
                                                   active ? 0.82f : 0.32f);
    pm_face_draw_annular_wedge(kCx, kCy, active ? 50 : 58, 204, start, end, col);
    pm_face_draw_label_at_polar(kCx, kCy, 144, pm_face_deg_to_rad(static_cast<float>(i) * 45.f),
                                kChords[i].name, active ? pm_gfx->color565(18, 12, 8)
                                                        : pm_gfx->color565(225, 232, 220));
  }
  pm_gfx->fillCircle(kCx, kCy, 48, pm_gfx->color565(12, 16, 22));
  pm_gfx->drawCircle(kCx, kCy, 54 + static_cast<int>(e * 8.f * sinf(s_phase)), pm_gfx->color565(238, 210, 128));
  pm_face_draw_centered_line("Chord", 38, pm_gfx->color565(238, 220, 170), 2, 2);
  pm_face_draw_centered_line(s_chord_idx >= 0 && e > 0.02f ? kChords[s_chord_idx].name : "tap a harmony pad", 414,
                             pm_gfx->color565(220, 210, 166), 1, 2);
}

bool pm_face_chord_play_at(int16_t x, int16_t y) {
  int idx = pad_from_point(x, y);
  if (idx < 0) {
    idx = (s_chord_idx + 1) % kChordCount;
  }
  midi_off_current();
  s_chord_idx = idx;
  s_chord_ms = millis();
  s_last_anim_ms = 0;
  s_phase = 0.f;
  uint8_t notes[4] = {};
  int count = 0;
  chord_notes(kChords[idx], notes, &count);
  for (int i = 0; i < count; ++i) {
    (void)pm_midi_note_on(PmMidiInstrument::Chord, notes[i], static_cast<uint8_t>(i == 0 ? 98 : 84));
  }
  s_midi_on = true;
  return pm_speaker_play_synth_note_begin(
      440.f * powf(2.f, (static_cast<float>(kChords[idx].root) - 69.f) / 12.f), 980, PmSynthPatch::Chord,
      0.82f);
}

bool pm_face_chord_anim_tick(uint32_t now_ms) {
  if (s_chord_idx < 0 || energy() <= 0.02f) {
    midi_off_current();
    return false;
  }
  if (now_ms - s_last_anim_ms < 45u) {
    return false;
  }
  s_last_anim_ms = now_ms;
  s_phase += 0.28f;
  return true;
}

void pm_face_chord_stop(void) {
  midi_off_current();
  if (pm_speaker_is_playing()) {
    pm_speaker_abort();
    pm_speaker_tone_stop();
  }
  s_chord_idx = -1;
}

const char *pm_face_chord_label(void) {
  return (s_chord_idx >= 0 && s_chord_idx < kChordCount) ? kChords[s_chord_idx].name : "";
}
