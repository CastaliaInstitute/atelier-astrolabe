#include "faces/drone/pm_face_drone.h"

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

struct DroneRoot {
  const char *name;
  float hz;
  uint8_t midi;
};

static const DroneRoot kRoots[] = {
    {"C", 130.81f, 48}, {"D", 146.83f, 50}, {"F", 174.61f, 53}, {"G", 196.00f, 55}, {"A", 220.00f, 57},
};

static int s_root_idx = 0;
static bool s_on = false;
static uint32_t s_started_ms = 0;
static uint32_t s_last_anim_ms = 0;
static float s_phase = 0.f;

static void drone_notes_off(void) {
  (void)pm_midi_note_off(PmMidiInstrument::Drone, kRoots[s_root_idx].midi);
  (void)pm_midi_note_off(PmMidiInstrument::Drone, static_cast<uint8_t>(kRoots[s_root_idx].midi + 7));
  (void)pm_midi_note_off(PmMidiInstrument::Drone, static_cast<uint8_t>(kRoots[s_root_idx].midi + 12));
}

static void drone_notes_on(void) {
  (void)pm_midi_note_on(PmMidiInstrument::Drone, kRoots[s_root_idx].midi, 80);
  (void)pm_midi_note_on(PmMidiInstrument::Drone, static_cast<uint8_t>(kRoots[s_root_idx].midi + 7), 64);
  (void)pm_midi_note_on(PmMidiInstrument::Drone, static_cast<uint8_t>(kRoots[s_root_idx].midi + 12), 52);
}

void pm_face_drone_draw(void) {
  pm_gfx->fillScreen(pm_gfx->color565(3, 9, 14));
  const uint16_t glow = pm_gfx->color565(88, 210, 180);
  const uint16_t dim = pm_gfx->color565(58, 92, 92);
  for (int i = 0; i < 7; ++i) {
    const int r = 42 + i * 24 + (s_on ? static_cast<int>(sinf(s_phase + i) * 6.f) : 0);
    pm_gfx->drawCircle(kCx, kCy, r, s_on ? glow : dim);
  }
  pm_gfx->fillCircle(kCx, kCy, 58, s_on ? pm_gfx->color565(30, 88, 78) : pm_gfx->color565(16, 30, 34));
  pm_gfx->drawCircle(kCx, kCy, 66, glow);

  char line[32];
  snprintf(line, sizeof(line), "Drone %s", kRoots[s_root_idx].name);
  pm_face_draw_centered_line(line, 48, glow, 2, 2);
  pm_face_draw_centered_line(s_on ? "root + fifth + octave" : "tap to hold drone", kCy - 8,
                             pm_gfx->color565(220, 244, 230), 1, 2);
  pm_face_draw_centered_line("swipe up/down root", 416, dim, 1, 1);
}

bool pm_face_drone_toggle_at(int16_t x, int16_t y) {
  (void)x;
  (void)y;
  if (s_on) {
    pm_face_drone_stop();
    return true;
  }
  s_on = true;
  s_started_ms = millis();
  s_last_anim_ms = 0;
  s_phase = 0.f;
  drone_notes_on();
  return pm_speaker_play_synth_loop_begin(kRoots[s_root_idx].hz, PmSynthPatch::Drone, 0.72f);
}

void pm_face_drone_cycle_root(int delta) {
  const bool was_on = s_on;
  if (was_on) {
    drone_notes_off();
    pm_speaker_tone_stop();
  }
  const int n = static_cast<int>(sizeof(kRoots) / sizeof(kRoots[0]));
  s_root_idx = (s_root_idx + delta + n) % n;
  if (was_on) {
    drone_notes_on();
    (void)pm_speaker_play_synth_loop_begin(kRoots[s_root_idx].hz, PmSynthPatch::Drone, 0.72f);
  }
}

bool pm_face_drone_anim_tick(uint32_t now_ms) {
  if (!s_on) {
    return false;
  }
  if (now_ms - s_started_ms > 2300u && !pm_speaker_is_playing()) {
    (void)pm_speaker_play_synth_loop_begin(kRoots[s_root_idx].hz, PmSynthPatch::Drone, 0.72f);
  }
  if (now_ms - s_last_anim_ms < 55u) {
    return false;
  }
  s_last_anim_ms = now_ms;
  s_phase += 0.18f;
  return true;
}

void pm_face_drone_stop(void) {
  drone_notes_off();
  s_on = false;
  if (pm_speaker_is_playing()) {
    pm_speaker_abort();
    pm_speaker_tone_stop();
  }
}

const char *pm_face_drone_label(void) { return kRoots[s_root_idx].name; }
