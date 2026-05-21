#include "faces/chakra/pm_face_chakra.h"

#include <Arduino_GFX_Library.h>
#include <cmath>
#include <cstring>

#include "faces/chakra/pm_chakra_glyphs.h"
#include "faces/shared/pm_face_draw.h"
#include "pin_config.h"
#include "pm_display.h"
#include "pm_speaker.h"

static constexpr int kCx = LCD_WIDTH / 2;
static constexpr int kCy = LCD_HEIGHT / 2 - 8;
static constexpr int kGemRadius = 130;

struct ChakraDef {
  const char *name;
  uint8_t r;
  uint8_t g;
  uint8_t b;
  float hz;
};

static const ChakraDef kChakras[] = {
    {"Root", 220, 20, 30, 396.f},
    {"Sacral", 255, 110, 0, 417.f},
    {"Solar", 255, 210, 0, 528.f},
    {"Heart", 30, 200, 80, 639.f},
    {"Throat", 40, 120, 255, 741.f},
    {"Third Eye", 90, 40, 200, 852.f},
    {"Crown", 200, 160, 255, 963.f},
};

static int s_index = 0;
static uint32_t s_ripple_start = 0;
static uint32_t s_last_anim_ms = 0;
static uint32_t s_wave_last_ms = 0;
static float s_wave_phase = 0.f;
static bool s_ripple_active = false;

static uint16_t chakra_color(const ChakraDef &c, float dim) {
  const float d = dim < 0.f ? 0.f : (dim > 1.f ? 1.f : dim);
  return pm_gfx->color565(static_cast<uint8_t>(c.r * d), static_cast<uint8_t>(c.g * d),
                          static_cast<uint8_t>(c.b * d));
}

static bool chakra_audio_active(void) {
  return s_ripple_active || pm_speaker_bowl_voice_active();
}

static void chakra_advance_wave(uint32_t now_ms, float hz) {
  if (s_wave_last_ms == 0) {
    s_wave_last_ms = now_ms;
    return;
  }
  const float dt = static_cast<float>(now_ms - s_wave_last_ms) * 0.001f;
  s_wave_last_ms = now_ms;
  if (dt <= 0.f || hz < 20.f) {
    return;
  }
  s_wave_phase += pm_face_k_two_pi * (hz / 528.f) * dt * 1.65f;
  if (s_wave_phase > pm_face_k_two_pi * 64.f) {
    s_wave_phase = fmodf(s_wave_phase, pm_face_k_two_pi);
  }
}

void pm_face_chakra_draw(void) {
  const ChakraDef &ch = kChakras[s_index];
  const uint16_t bg = pm_gfx->color565(6, 6, 10);
  pm_gfx->fillScreen(bg);

  const bool waves = chakra_audio_active();
  float pulse = 0.78f;
  if (waves) {
    pulse = 0.86f + 0.14f * sinf(s_wave_phase);
  }
  pm_face_draw_chakra_gem(kCx, kCy, kGemRadius, ch.r, ch.g, ch.b, pulse, s_wave_phase, ch.hz, waves);

  const bool playing = pm_speaker_bowl_voice_active();
  pm_chakra_draw_glyph(pm_gfx, kCx, kCy, s_index, chakra_color(ch, 1.f), playing);

  char label[24];
  snprintf(label, sizeof(label), "%s", ch.name);
  pm_face_draw_centered_line(label, 56, chakra_color(ch, 0.95f), 2, 2);

  char hz_line[16];
  snprintf(hz_line, sizeof(hz_line), "%.0f Hz", ch.hz);
  pm_face_draw_centered_line(hz_line, 400, chakra_color(ch, 0.75f), 1, 2);
}

static void chakra_stop_tone(void) {
  pm_speaker_bowl_voice_stop();
  s_ripple_active = false;
}

static bool chakra_strike_current(void) {
  const ChakraDef &ch = kChakras[s_index];
  s_ripple_active = true;
  s_ripple_start = millis();
  s_wave_phase = 0.f;
  s_wave_last_ms = 0;
  PmBowlVoiceCtrl strike = {};
  strike.target_hz = ch.hz;
  strike.center_strike = true;
  strike.excitation = 1.f;
  strike.brightness = 0.78f;
  strike.rim_quality = 0.8f;
  pm_speaker_bowl_voice_push(strike);
  return true;
}

int pm_face_chakra_cycle(int delta) {
  const int n = static_cast<int>(sizeof(kChakras) / sizeof(kChakras[0]));
  int v = (s_index + delta) % n;
  if (v < 0) {
    v += n;
  }
  if (v == s_index) {
    return s_index;
  }
  s_index = v;
  return s_index;
}

bool pm_face_chakra_strike(void) { return chakra_strike_current(); }

bool pm_face_chakra_anim_tick(uint32_t now_ms) {
  if (!chakra_audio_active()) {
    return false;
  }

  const bool playing = pm_speaker_bowl_voice_active();
  const uint32_t interval = playing ? 90u : 140u;
  if (now_ms - s_last_anim_ms < interval) {
    return false;
  }
  s_last_anim_ms = now_ms;

  if (playing) {
    chakra_advance_wave(now_ms, kChakras[s_index].hz);
    return true;
  }

  if (!s_ripple_active) {
    return false;
  }
  chakra_advance_wave(now_ms, kChakras[s_index].hz);
  const float elapsed = static_cast<float>(now_ms - s_ripple_start) * 0.0012f;
  if (elapsed > 2.8f) {
    s_ripple_active = false;
  }
  return true;
}

void pm_face_chakra_stop(void) { chakra_stop_tone(); }

int pm_face_chakra_index(void) { return s_index; }
