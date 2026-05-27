#include "faces/pitch_pipe/pm_face_pitch_pipe.h"

#include <Arduino_GFX_Library.h>
#include <cmath>
#include <cstdio>

#include "faces/shared/pm_face_draw.h"
#include "pm_display.h"
#include "pm_mic.h"
#include "pm_speaker.h"

static constexpr int kCx = pm_face_lcd_cx;
static constexpr int kCy = pm_face_lcd_cy;
static constexpr float kPi = 3.14159265358979323846f;

struct PitchNote {
  const char *label;
  float hz;
};

static const PitchNote kNotes[] = {
    {"C4", 261.63f},  {"C#4", 277.18f}, {"D4", 293.66f},  {"D#4", 311.13f},
    {"E4", 329.63f},  {"F4", 349.23f},  {"F#4", 369.99f}, {"G4", 392.00f},
    {"G#4", 415.30f}, {"A4", 440.00f},  {"A#4", 466.16f}, {"B4", 493.88f},
};
static constexpr int kNoteCount = static_cast<int>(sizeof(kNotes) / sizeof(kNotes[0]));

static int s_note_idx = 9;
static bool s_sounding = false;
static bool s_tap_latched = false;
static uint32_t s_started_ms = 0;
static uint32_t s_last_anim_ms = 0;
static uint32_t s_last_mic_ms = 0;
static uint32_t s_breath_arm_ms = 0;
static uint32_t s_breath_calibrate_until_ms = 0;
static uint32_t s_breath_phrase_until_ms = 0;
static float s_breath_level = 0.f;
static float s_breath_floor = 0.f;
static uint8_t s_hot_frames = 0;
static float s_phase = 0.f;

static float clampf(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

static uint16_t pipe_color(float dim) {
  const float d = clampf(dim, 0.f, 1.f);
  return pm_gfx->color565(static_cast<uint8_t>(74.f * d), static_cast<uint8_t>(210.f * d),
                          static_cast<uint8_t>(222.f * d));
}

static float breath_level_from_frame(const int16_t *raw, size_t frame_samples, int channels) {
  if (!raw || frame_samples == 0 || channels <= 0) return 0.f;
  uint32_t peak = 0;
  uint64_t sum_sq = 0;
  for (size_t i = 0; i < frame_samples; ++i) {
    int32_t best = 0;
    for (int ch = 0; ch < channels; ++ch) {
      const int32_t v = raw[i * static_cast<size_t>(channels) + static_cast<size_t>(ch)];
      const int32_t a = v < 0 ? -v : v;
      if (a > best) best = a;
    }
    if (static_cast<uint32_t>(best) > peak) peak = static_cast<uint32_t>(best);
    sum_sq += static_cast<uint64_t>(best) * static_cast<uint64_t>(best);
  }
  const float rms = sqrtf(static_cast<float>(sum_sq) / static_cast<float>(frame_samples)) / 32768.f;
  const float pk = static_cast<float>(peak) / 32768.f;
  return clampf((rms - 0.040f) * 6.0f + (pk - 0.160f) * 1.1f, 0.f, 1.f);
}

static int note_at(int16_t x, int16_t y) {
  const float dx = static_cast<float>(x - kCx);
  const float dy = static_cast<float>(y - kCy);
  const float r = sqrtf(dx * dx + dy * dy);
  if (r < 80.f || r > 220.f) return -1;
  float a = atan2f(dy, dx) + kPi / 2.f;
  if (a < 0.f) a += 2.f * kPi;
  int idx = static_cast<int>(floorf((a / (2.f * kPi)) * kNoteCount + 0.5f)) % kNoteCount;
  return idx;
}

static bool start_pipe(bool latched) {
  if (s_sounding && s_tap_latched == latched && pm_speaker_is_playing()) return true;
  pm_face_pitch_pipe_stop();
  if (!pm_speaker_play_synth_loop_begin(kNotes[s_note_idx].hz, PmSynthPatch::Sine, 1.25f)) {
    return false;
  }
  s_sounding = true;
  s_tap_latched = latched;
  s_started_ms = millis();
  s_breath_phrase_until_ms = s_started_ms + 1800u;
  return true;
}

void pm_face_pitch_pipe_draw(void) {
  pm_gfx->fillScreen(pm_gfx->color565(2, 8, 12));
  const float energy = s_sounding ? 1.f : clampf(s_breath_level, 0.f, 1.f);
  const uint16_t hi = pipe_color(0.72f + 0.25f * energy);
  const uint16_t muted = pm_gfx->color565(48, 76, 82);

  pm_face_draw_centered_line("Pitch Pipe", 28, hi, 2, 2);
  pm_gfx->drawCircle(kCx, kCy, 162, muted);
  pm_gfx->drawCircle(kCx, kCy, 82, pm_gfx->color565(24, 44, 50));

  for (int i = 0; i < kNoteCount; ++i) {
    const float a = (static_cast<float>(i) / kNoteCount) * 2.f * kPi - kPi / 2.f;
    const int x = kCx + static_cast<int>(cosf(a) * 142.f);
    const int y = kCy + static_cast<int>(sinf(a) * 142.f);
    const bool selected = i == s_note_idx;
    const int r = selected ? 30 + static_cast<int>(energy * 8.f) : 22;
    pm_gfx->fillCircle(x, y, r + 4, selected ? hi : muted);
    pm_gfx->fillCircle(x, y, r, selected ? pm_gfx->color565(14, 72, 82) : pm_gfx->color565(10, 26, 32));
    pm_face_draw_centered_line(kNotes[i].label, y - 8, selected ? pm_gfx->color565(218, 252, 248)
                                                                : pm_gfx->color565(136, 168, 168),
                               1, 1);
  }

  pm_gfx->fillCircle(kCx, kCy, 58 + static_cast<int>(energy * 10.f), pm_gfx->color565(6, 22, 28));
  pm_gfx->drawCircle(kCx, kCy, 64 + static_cast<int>(energy * 12.f), hi);
  char line[32];
  snprintf(line, sizeof(line), "%s %.0f Hz", kNotes[s_note_idx].label, static_cast<double>(kNotes[s_note_idx].hz));
  pm_face_draw_centered_line(line, kCy - 10, hi, 2, 2);
  pm_face_draw_centered_line(s_sounding ? (s_tap_latched ? "tap tone" : "breath tone") : "tap or breathe",
                             kCy + 22, pm_gfx->color565(148, 190, 190), 1, 1);
  pm_face_draw_centered_line("swipe pitch", 438, pm_gfx->color565(92, 132, 134), 1, 1);
}

void pm_face_pitch_pipe_on_enter(void) {
  s_breath_level = 0.f;
  s_breath_floor = 0.f;
  s_hot_frames = 0;
  s_last_mic_ms = 0;
  s_breath_calibrate_until_ms = millis() + 1800u;
  s_breath_arm_ms = millis() + 2200u;
  (void)pm_mic_begin();
}

void pm_face_pitch_pipe_on_leave(void) {
  pm_face_pitch_pipe_stop();
  pm_mic_stop();
}

void pm_face_pitch_pipe_stop(void) {
  if (pm_speaker_is_playing()) {
    pm_speaker_abort();
    pm_speaker_tone_stop();
  }
  s_sounding = false;
  s_tap_latched = false;
}

bool pm_face_pitch_pipe_tap_at(int16_t x, int16_t y) {
  const int idx = note_at(x, y);
  if (idx >= 0) s_note_idx = idx;
  if (s_sounding && s_tap_latched) {
    pm_face_pitch_pipe_stop();
    return true;
  }
  return start_pipe(true);
}

bool pm_face_pitch_pipe_breath_tick(uint32_t now_ms) {
  if (s_sounding && pm_speaker_is_playing()) {
    if (s_tap_latched || static_cast<int32_t>(now_ms - s_breath_phrase_until_ms) < 0) return true;
    pm_face_pitch_pipe_stop();
    s_last_mic_ms = 0;
  } else if (pm_speaker_is_playing()) {
    return false;
  }
  if (s_breath_arm_ms != 0 && static_cast<int32_t>(now_ms - s_breath_arm_ms) < 0) return false;
  if (now_ms - s_last_mic_ms < 35u) return false;
  s_last_mic_ms = now_ms;
  if (!pm_mic_begin()) return false;

  const size_t ns = pm_mic_frame_samples();
  const int channels = pm_mic_i2s_channels();
  if (ns == 0 || ns > 512 || channels <= 0 || channels > 4) return false;

  int16_t raw[512 * 4];
  size_t br = 0;
  if (!pm_mic_read_frame(raw, ns, &br)) return false;
  const float level = breath_level_from_frame(raw, ns, channels);
  s_breath_level = s_breath_level * 0.62f + level * 0.38f;
  if (s_breath_calibrate_until_ms != 0 &&
      static_cast<int32_t>(now_ms - s_breath_calibrate_until_ms) < 0) {
    if (s_breath_level > s_breath_floor) s_breath_floor = s_breath_level;
    s_hot_frames = 0;
    return false;
  }

  const float trigger_floor = s_breath_floor + 0.30f > 0.66f ? s_breath_floor + 0.30f : 0.66f;
  if (level >= trigger_floor && s_breath_level >= trigger_floor - 0.22f) {
    if (s_hot_frames < 4) ++s_hot_frames;
  } else {
    s_hot_frames = 0;
  }
  if (s_hot_frames < 2) return false;
  s_hot_frames = 0;
  return start_pipe(false);
}

void pm_face_pitch_pipe_cycle(int delta) {
  s_note_idx = (s_note_idx + delta) % kNoteCount;
  if (s_note_idx < 0) s_note_idx += kNoteCount;
  if (s_sounding && pm_speaker_is_playing()) {
    const bool latched = s_tap_latched;
    pm_face_pitch_pipe_stop();
    (void)start_pipe(latched);
  }
}

bool pm_face_pitch_pipe_anim_tick(uint32_t now_ms) {
  if (!s_sounding && s_breath_level < 0.04f) return false;
  if (now_ms - s_last_anim_ms < 45u) return false;
  s_last_anim_ms = now_ms;
  s_phase += 0.25f;
  return true;
}

const char *pm_face_pitch_pipe_note_label(void) { return kNotes[s_note_idx].label; }

int pm_face_pitch_pipe_note_index(void) { return s_note_idx; }
