#include "faces/ocarina/pm_face_ocarina.h"

#include <Arduino_GFX_Library.h>
#include <cmath>
#include <cstdio>

#include "faces/shared/pm_face_draw.h"
#include "pin_config.h"
#include "pm_display.h"
#include "pm_midi.h"
#include "pm_mic.h"
#include "pm_speaker.h"

static constexpr int kCx = pm_face_lcd_cx;
static constexpr int kCy = pm_face_lcd_cy;
static constexpr int kHoleCount = 6;

struct OcarinaKey {
  const char *name;
  float root_hz;
  uint8_t root_midi;
};

struct OcarinaHole {
  int x;
  int y;
  int r;
  int semitone;
  const char *degree;
};

static const OcarinaKey kKeys[] = {
    {"C", 261.63f, 60},
    {"D", 293.66f, 62},
    {"F", 349.23f, 65},
    {"G", 392.00f, 67},
    {"A", 440.00f, 69},
};

static const OcarinaHole kHoles[kHoleCount] = {
    {kCx - 98, 132, 18, 0, "1"},
    {kCx, 116, 18, 2, "2"},
    {kCx + 98, 132, 18, 4, "3"},
    {kCx - 98, 334, 18, 7, "5"},
    {kCx, 350, 18, 9, "6"},
    {kCx + 98, 334, 18, 12, "8"},
};

static int s_key_idx = 0;
static int s_note_idx = -1;
static int s_selected_idx = 0;
static uint32_t s_note_start_ms = 0;
static uint32_t s_last_anim_ms = 0;
static uint32_t s_last_breath_ms = 0;
static uint32_t s_last_mic_ms = 0;
static uint32_t s_breath_arm_ms = 0;
static uint32_t s_breath_calibrate_until_ms = 0;
static uint32_t s_breath_phrase_until_ms = 0;
static float s_phase = 0.f;
static float s_breath_level = 0.f;
static float s_breath_floor = 0.f;
static uint8_t s_breath_hot_frames = 0;
static bool s_midi_note_on = false;
static bool s_breath_sounding = false;
static bool s_touch_direct_active = false;
static int s_touch_direct_idx = -1;

static float clamp01(float v) {
  if (v < 0.f) {
    return 0.f;
  }
  if (v > 1.f) {
    return 1.f;
  }
  return v;
}

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

static uint8_t note_midi(int hole_idx) {
  if (hole_idx < 0 || hole_idx >= kHoleCount) {
    return kKeys[s_key_idx].root_midi;
  }
  return static_cast<uint8_t>(kKeys[s_key_idx].root_midi + kHoles[hole_idx].semitone);
}

static void midi_note_off_current(void) {
  if (s_midi_note_on && s_note_idx >= 0) {
    (void)pm_midi_note_off(PmMidiInstrument::Ocarina, note_midi(s_note_idx));
  }
  s_midi_note_on = false;
}

static float breath_level_from_frame(const int16_t *raw, size_t frame_samples, int channels) {
  if (!raw || frame_samples == 0 || channels <= 0) {
    return 0.f;
  }
  uint32_t peak = 0;
  uint64_t sum_sq = 0;
  for (size_t i = 0; i < frame_samples; ++i) {
    int32_t best = 0;
    for (int ch = 0; ch < channels; ++ch) {
      const int32_t v = raw[i * static_cast<size_t>(channels) + static_cast<size_t>(ch)];
      const int32_t a = v < 0 ? -v : v;
      if (a > best) {
        best = a;
      }
    }
    if (static_cast<uint32_t>(best) > peak) {
      peak = static_cast<uint32_t>(best);
    }
    sum_sq += static_cast<uint64_t>(best) * static_cast<uint64_t>(best);
  }
  const float rms = sqrtf(static_cast<float>(sum_sq) / static_cast<float>(frame_samples)) / 32768.f;
  const float pk = static_cast<float>(peak) / 32768.f;
  return clamp01((rms - 0.045f) * 6.0f + (pk - 0.180f) * 1.1f);
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
  const uint16_t ring = jade(0.24f + 0.38f * energy);
  for (int i = 0; i < 5; ++i) {
    const int r = 38 + i * 28 + static_cast<int>(sinf(s_phase + static_cast<float>(i)) * 5.f);
    pm_gfx->drawCircle(kCx, kCy, r, ring);
  }
}

static void draw_instrument_plate(float energy) {
  const uint16_t rim = pm_gfx->color565(34, 52, 58);
  const uint16_t grid = pm_gfx->color565(18, 32, 38);
  const uint16_t accent = jade(0.52f + 0.20f * energy);

  pm_gfx->fillRect(kCx - 142, 76, 284, 320, pm_gfx->color565(7, 13, 17));
  pm_gfx->fillCircle(kCx - 100, 118, 42, pm_gfx->color565(7, 13, 17));
  pm_gfx->fillCircle(kCx + 100, 118, 42, pm_gfx->color565(7, 13, 17));
  pm_gfx->fillCircle(kCx - 100, 354, 42, pm_gfx->color565(7, 13, 17));
  pm_gfx->fillCircle(kCx + 100, 354, 42, pm_gfx->color565(7, 13, 17));
  pm_gfx->drawRect(kCx - 146, 72, 292, 328, rim);
  pm_gfx->drawRect(kCx - 108, 104, 216, 258, grid);
  pm_gfx->drawLine(kCx - 128, 234, kCx + 128, 234, grid);
  pm_gfx->drawLine(kCx, 90, kCx, 376, grid);

  const uint16_t mic = pm_gfx->color565(96, 120, 122);
  pm_gfx->drawCircle(kCx - 36, 58, 7, mic);
  pm_gfx->drawCircle(kCx + 36, 58, 7, mic);
  pm_gfx->drawLine(kCx - 24, 58, kCx + 24, 58, pm_gfx->color565(34, 58, 62));

  pm_gfx->fillCircle(kCx, kCy, 42 + static_cast<int>(energy * 8.f), pm_gfx->color565(10, 24, 28));
  pm_gfx->drawCircle(kCx, kCy, 48 + static_cast<int>(energy * 8.f), accent);
  pm_face_draw_centered_line(kKeys[s_key_idx].name, kCy - 11, accent, 2, 2);
  pm_face_draw_centered_line("key", kCy + 18, pm_gfx->color565(110, 148, 148), 1, 1);
}

static void draw_holes(float energy) {
  const uint16_t pad = pm_gfx->color565(18, 34, 40);
  const uint16_t pad_edge = pm_gfx->color565(62, 86, 92);
  const uint16_t active = jade(0.90f);
  const uint16_t text = pm_gfx->color565(196, 220, 214);
  for (int i = 0; i < kHoleCount; ++i) {
    const OcarinaHole &h = kHoles[i];
    const bool on = i == s_note_idx && energy > 0.02f;
    const bool selected = i == s_selected_idx;
    const int grow = on ? static_cast<int>(9.f * energy) : 0;
    const int r = h.r + 22 + grow;
    pm_gfx->fillCircle(h.x, h.y, r + 5, on ? active : (selected ? jade(0.42f) : pad_edge));
    pm_gfx->fillCircle(h.x, h.y, r, on ? pm_gfx->color565(24, 112, 96) : pad);
    if (on) {
      pm_gfx->drawCircle(h.x, h.y, r + 12, active);
      pm_gfx->drawCircle(h.x, h.y, r + 20, jade(0.44f));
    }
    pm_face_draw_centered_line(h.degree, h.y - 15, on ? pm_gfx->color565(8, 24, 22) : text, 2, 2);
    char note[8];
    snprintf(note, sizeof(note), "%+d", h.semitone);
    pm_face_draw_centered_line(note, h.y + 13, on ? pm_gfx->color565(8, 24, 22) : pm_gfx->color565(110, 144, 146),
                               1, 1);
  }
}

void pm_face_ocarina_draw(void) {
  const float energy = note_energy();
  pm_gfx->fillScreen(pm_gfx->color565(3, 7, 10));
  draw_breath_rings(energy);
  draw_instrument_plate(energy);
  draw_holes(energy);

  char line[28];
  snprintf(line, sizeof(line), "Ocarina / MIDI 1");
  pm_face_draw_centered_line(line, 24, jade(0.82f), 2, 2);
  if (s_note_idx >= 0 && energy > 0.02f) {
    snprintf(line, sizeof(line), "pad %s  %.0f Hz", kHoles[s_note_idx].degree, note_hz(s_note_idx));
    pm_face_draw_centered_line(line, 414, jade(0.80f), 1, 2);
  } else if (s_breath_level > 0.04f) {
    snprintf(line, sizeof(line), "breath %.0f%% pad %s", static_cast<double>(s_breath_level * 100.f),
             kHoles[s_selected_idx].degree);
    pm_face_draw_centered_line(line, 414, pm_gfx->color565(154, 194, 188), 1, 2);
  } else {
    pm_face_draw_centered_line("3 top / 3 bottom", 414, pm_gfx->color565(154, 194, 188), 1, 2);
  }
  pm_face_draw_centered_line("swipe up/down key", 440, pm_gfx->color565(88, 124, 124), 1, 1);
}

bool pm_face_ocarina_play_at(int16_t x, int16_t y) {
  int idx = -1;
  if (!hit_hole(x, y, &idx)) {
    idx = (s_note_idx + 1) % kHoleCount;
  }
  s_selected_idx = idx;
  midi_note_off_current();
  s_note_idx = idx;
  s_note_start_ms = millis();
  s_last_anim_ms = 0;
  s_phase = 0.f;
  s_midi_note_on = pm_midi_has_sink() && pm_midi_note_on(PmMidiInstrument::Ocarina, note_midi(idx), 124);
  const bool ok = pm_speaker_play_synth_loop_begin(note_hz(idx), PmSynthPatch::Ocarina, 1.25f);
  s_breath_sounding = ok;
  s_breath_phrase_until_ms = millis() + 1800u;
  return ok;
}

bool pm_face_ocarina_touch_tick(int16_t x, int16_t y, bool down, uint32_t now_ms) {
  (void)now_ms;
  if (!down) {
    if (s_touch_direct_active) {
      s_touch_direct_active = false;
      s_touch_direct_idx = -1;
    }
    return false;
  }

  int idx = -1;
  if (!hit_hole(x, y, &idx)) {
    return false;
  }
  if (s_touch_direct_active && idx == s_touch_direct_idx) {
    return false;
  }
  s_touch_direct_active = true;
  s_touch_direct_idx = idx;
  const bool changed = idx != s_selected_idx;
  s_selected_idx = idx;
  if (changed && s_breath_sounding && pm_speaker_is_playing()) {
    pm_speaker_tone_stop();
    midi_note_off_current();
    s_breath_sounding = false;
    const OcarinaHole &h = kHoles[s_selected_idx];
    return pm_face_ocarina_play_at(h.x, h.y);
  }
  return true;
}

bool pm_face_ocarina_breath_tick(uint32_t now_ms) {
  if (s_breath_sounding && pm_speaker_is_playing()) {
    if (static_cast<int32_t>(now_ms - s_breath_phrase_until_ms) < 0) {
      return true;
    }
    pm_speaker_tone_stop();
    midi_note_off_current();
    s_breath_sounding = false;
    s_last_mic_ms = 0;
  } else if (pm_speaker_is_playing()) {
    return false;
  }
  if (s_breath_arm_ms != 0 && static_cast<int32_t>(now_ms - s_breath_arm_ms) < 0) {
    return false;
  }
  if (now_ms - s_last_mic_ms < 35u) {
    return false;
  }
  s_last_mic_ms = now_ms;
  if (!pm_mic_begin()) {
    return false;
  }

  const size_t ns = pm_mic_frame_samples();
  const int channels = pm_mic_i2s_channels();
  if (ns == 0 || ns > 512 || channels <= 0 || channels > 4) {
    return false;
  }

  int16_t raw[512 * 4];
  size_t br = 0;
  if (!pm_mic_read_frame(raw, ns, &br)) {
    return false;
  }
  const float level = breath_level_from_frame(raw, ns, channels);
  s_breath_level = s_breath_level * 0.62f + level * 0.38f;
  if (s_breath_calibrate_until_ms != 0 &&
      static_cast<int32_t>(now_ms - s_breath_calibrate_until_ms) < 0) {
    if (s_breath_level > s_breath_floor) {
      s_breath_floor = s_breath_level;
    }
    s_breath_hot_frames = 0;
    return false;
  }

  const float trigger_floor = s_breath_floor + 0.34f > 0.72f ? s_breath_floor + 0.34f : 0.72f;
  if (level >= trigger_floor && s_breath_level >= trigger_floor - 0.24f) {
    if (s_breath_hot_frames < 4) {
      ++s_breath_hot_frames;
    }
  } else {
    s_breath_hot_frames = 0;
  }
  if (s_breath_hot_frames < 2 || now_ms - s_last_breath_ms < 180u) {
    if (s_breath_sounding && s_breath_level < 0.10f) {
      pm_speaker_tone_stop();
      midi_note_off_current();
      s_breath_sounding = false;
      s_note_idx = -1;
    }
    return false;
  }
  s_breath_hot_frames = 0;
  s_last_breath_ms = now_ms;
  const OcarinaHole &h = kHoles[s_selected_idx];
  return pm_face_ocarina_play_at(h.x, h.y);
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
    midi_note_off_current();
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
  midi_note_off_current();
  s_breath_sounding = false;
  s_breath_phrase_until_ms = 0;
  s_touch_direct_active = false;
  s_touch_direct_idx = -1;
  if (pm_speaker_is_playing()) {
    pm_speaker_abort();
    pm_speaker_tone_stop();
  }
  s_note_idx = -1;
}

void pm_face_ocarina_on_enter(void) {
  s_breath_level = 0.f;
  s_breath_floor = 0.f;
  s_breath_hot_frames = 0;
  s_breath_sounding = false;
  s_breath_phrase_until_ms = 0;
  s_last_breath_ms = 0;
  s_last_mic_ms = 0;
  s_breath_calibrate_until_ms = millis() + 2600u;
  s_breath_arm_ms = millis() + 3000u;
  (void)pm_mic_begin();
}

void pm_face_ocarina_on_leave(void) {
  pm_face_ocarina_stop();
  pm_mic_stop();
}

const char *pm_face_ocarina_key_label(void) { return kKeys[s_key_idx].name; }

int pm_face_ocarina_note_index(void) { return s_note_idx; }

int pm_face_ocarina_selected_index(void) { return s_selected_idx; }
