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

static int s_index = 3;
static uint32_t s_ripple_start = 0;
static uint32_t s_last_anim_ms = 0;
static bool s_ripple_active = false;
static bool s_chakra_tone_on = false;

static uint16_t chakra_color(const ChakraDef &c, float dim) {
  const float d = dim < 0.f ? 0.f : (dim > 1.f ? 1.f : dim);
  return pm_gfx->color565(static_cast<uint8_t>(c.r * d), static_cast<uint8_t>(c.g * d),
                          static_cast<uint8_t>(c.b * d));
}

static void draw_ripples(const ChakraDef &ch) {
  if (!s_chakra_tone_on && !s_ripple_active && !pm_speaker_is_playing()) {
    return;
  }
  const float s_ripple = static_cast<float>(millis() - s_ripple_start) * 0.0012f;
  const uint16_t ring = chakra_color(ch, 0.55f);
  for (int i = 0; i < 3; ++i) {
    const float phase = s_ripple + static_cast<float>(i) * 0.33f;
    const float t = phase - floorf(phase);
    const int r = 88 + static_cast<int>(t * 100.f);
    const float alpha = 1.f - t;
    if (alpha <= 0.05f) {
      continue;
    }
    const uint16_t c = chakra_color(ch, 0.12f + 0.4f * alpha);
    pm_gfx->drawCircle(kCx, kCy, r, c);
    pm_gfx->drawCircle(kCx, kCy, r + 1, ring);
  }
}

void pm_face_chakra_draw(void) {
  const ChakraDef &ch = kChakras[s_index];
  const uint16_t bg = pm_gfx->color565(6, 6, 10);
  pm_gfx->fillScreen(bg);

  const uint16_t glow = chakra_color(ch, 0.22f);
  pm_gfx->fillCircle(kCx, kCy, 130, glow);

  draw_ripples(ch);

  const bool playing = s_chakra_tone_on || pm_speaker_is_playing();
  pm_chakra_draw_glyph(pm_gfx, kCx, kCy, s_index, chakra_color(ch, 1.f), playing);

  char label[24];
  snprintf(label, sizeof(label), "%s", ch.name);
  pm_face_draw_centered_line(label, 56, chakra_color(ch, 0.95f), 2, 2);

  char hz_line[16];
  snprintf(hz_line, sizeof(hz_line), "%.0f Hz", ch.hz);
  pm_face_draw_centered_line(hz_line, 400, chakra_color(ch, 0.75f), 1, 2);
}

static void chakra_stop_tone(void) {
  if (s_chakra_tone_on || pm_speaker_is_playing()) {
    pm_speaker_tone_stop();
  }
  s_chakra_tone_on = false;
  s_ripple_active = false;
}

int pm_face_chakra_cycle(int delta) {
  chakra_stop_tone();
  const int n = static_cast<int>(sizeof(kChakras) / sizeof(kChakras[0]));
  int v = s_index + delta;
  v = (v % n + n) % n;
  s_index = v;
  return s_index;
}

bool pm_face_chakra_toggle_tone(void) {
  if (s_chakra_tone_on || pm_speaker_is_playing()) {
    chakra_stop_tone();
    return true;
  }
  const ChakraDef &ch = kChakras[s_index];
  s_ripple_active = true;
  s_ripple_start = millis();
  if (!pm_speaker_play_tone_loop_begin(ch.hz)) {
    s_ripple_active = false;
    return false;
  }
  s_chakra_tone_on = true;
  return true;
}

bool pm_face_chakra_anim_tick(uint32_t now_ms) {
  if (!s_chakra_tone_on && !s_ripple_active && !pm_speaker_is_playing()) {
    return false;
  }
  if (now_ms - s_last_anim_ms < 120u) {
    return false;
  }
  s_last_anim_ms = now_ms;
  if (!pm_speaker_is_playing()) {
    s_chakra_tone_on = false;
    if (!s_ripple_active) {
      return false;
    }
    const float elapsed = static_cast<float>(now_ms - s_ripple_start) * 0.0012f;
    if (elapsed > 2.8f) {
      s_ripple_active = false;
    }
    return true;
  }
  return true;
}

void pm_face_chakra_stop(void) { chakra_stop_tone(); }

int pm_face_chakra_index(void) { return s_index; }
