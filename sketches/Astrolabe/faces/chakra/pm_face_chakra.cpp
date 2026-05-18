#include "faces/chakra/pm_face_chakra.h"

#include <Arduino_GFX_Library.h>
#include <cmath>
#include <cstring>

#include "faces/shared/pm_face_draw.h"
#include "pin_config.h"
#include "pm_display.h"
#include "pm_speaker.h"

static constexpr int kCx = LCD_WIDTH / 2;
static constexpr int kCy = LCD_HEIGHT / 2;

enum class ChakraSymbol : uint8_t {
  SquareLotus = 0,
  Crescent,
  TriangleDown,
  StarSix,
  Circle,
  Eye,
  Crown,
};

struct ChakraDef {
  const char *name;
  uint8_t r;
  uint8_t g;
  uint8_t b;
  float hz;
  ChakraSymbol symbol;
};

static const ChakraDef kChakras[] = {
    {"Root", 220, 20, 30, 396.f, ChakraSymbol::SquareLotus},
    {"Sacral", 255, 110, 0, 417.f, ChakraSymbol::Crescent},
    {"Solar", 255, 210, 0, 528.f, ChakraSymbol::TriangleDown},
    {"Heart", 30, 200, 80, 639.f, ChakraSymbol::StarSix},
    {"Throat", 40, 120, 255, 741.f, ChakraSymbol::Circle},
    {"Third Eye", 90, 40, 200, 852.f, ChakraSymbol::Eye},
    {"Crown", 200, 160, 255, 963.f, ChakraSymbol::Crown},
};

static int s_index = 3;
static float s_ripple = 0.f;
static uint32_t s_ripple_start = 0;
static uint32_t s_last_anim_ms = 0;
static bool s_ripple_active = false;
static bool s_chakra_tone_on = false;

static uint16_t chakra_color(const ChakraDef &c, float dim) {
  const float d = dim < 0.f ? 0.f : (dim > 1.f ? 1.f : dim);
  return pm_gfx->color565(static_cast<uint8_t>(c.r * d), static_cast<uint8_t>(c.g * d),
                          static_cast<uint8_t>(c.b * d));
}

static void draw_petals(int cx, int cy, int count, int radius, int petal_r, uint16_t color) {
  for (int i = 0; i < count; ++i) {
    const float a = static_cast<float>(i) * (2.f * 3.14159265f / static_cast<float>(count)) - 1.5707963f;
    const int px = cx + static_cast<int>(cosf(a) * static_cast<float>(radius));
    const int py = cy + static_cast<int>(sinf(a) * static_cast<float>(radius));
    pm_gfx->fillCircle(px, py, petal_r, color);
  }
}

static void draw_symbol(const ChakraDef &ch) {
  const uint16_t main = chakra_color(ch, 1.f);
  const uint16_t soft = chakra_color(ch, 0.35f);
  const int cx = kCx;
  const int cy = kCy - 8;

  pm_gfx->fillCircle(cx, cy, 118, soft);

  switch (ch.symbol) {
    case ChakraSymbol::SquareLotus:
      draw_petals(cx, cy, 4, 52, 22, main);
      pm_gfx->drawRect(cx - 28, cy - 28, 56, 56, main);
      break;
    case ChakraSymbol::Crescent:
      draw_petals(cx, cy, 6, 48, 18, main);
      pm_gfx->fillCircle(cx + 10, cy - 6, 34, pm_gfx->color565(8, 8, 12));
      pm_gfx->drawCircle(cx, cy, 36, main);
      break;
    case ChakraSymbol::TriangleDown: {
      draw_petals(cx, cy, 10, 44, 14, soft);
      pm_gfx->fillTriangle(cx, cy - 40, cx - 38, cy + 28, cx + 38, cy + 28, main);
      for (int i = -2; i <= 2; ++i) {
        pm_gfx->drawFastHLine(cx - 30, cy + 4 + i * 10, 60, soft);
      }
      break;
    }
    case ChakraSymbol::StarSix:
      draw_petals(cx, cy, 12, 50, 16, soft);
      for (int i = 0; i < 6; ++i) {
        const float a = static_cast<float>(i) * (3.14159265f / 3.f);
        const int x1 = cx + static_cast<int>(cosf(a) * 44.f);
        const int y1 = cy + static_cast<int>(sinf(a) * 44.f);
        const float a2 = a + 3.14159265f;
        const int x2 = cx + static_cast<int>(cosf(a2) * 44.f);
        const int y2 = cy + static_cast<int>(sinf(a2) * 44.f);
        pm_gfx->drawLine(x1, y1, x2, y2, main);
      }
      pm_gfx->fillCircle(cx, cy, 14, main);
      break;
    case ChakraSymbol::Circle:
      draw_petals(cx, cy, 16, 46, 12, soft);
      pm_gfx->drawCircle(cx, cy, 42, main);
      pm_gfx->fillCircle(cx, cy, 18, main);
      break;
    case ChakraSymbol::Eye:
      pm_gfx->fillTriangle(cx, cy - 34, cx - 48, cy + 6, cx + 48, cy + 6, main);
      pm_gfx->fillTriangle(cx, cy + 34, cx - 48, cy - 6, cx + 48, cy - 6, main);
      pm_gfx->fillCircle(cx, cy, 20, pm_gfx->color565(8, 8, 12));
      pm_gfx->fillCircle(cx, cy, 10, main);
      break;
    case ChakraSymbol::Crown:
      draw_petals(cx, cy, 12, 54, 14, soft);
      for (int i = 0; i < 7; ++i) {
        const float a = -1.2f + static_cast<float>(i) * 0.4f;
        const int x = cx + static_cast<int>(sinf(a) * 50.f);
        const int y = cy - 20 + static_cast<int>(cosf(a) * 12.f);
        pm_gfx->fillTriangle(x, y - 18, x - 10, y + 8, x + 10, y + 8, main);
      }
      pm_gfx->fillCircle(cx, cy + 8, 12, main);
      break;
  }
}

static void draw_ripples(const ChakraDef &ch) {
  if (!s_chakra_tone_on && !s_ripple_active && !pm_speaker_is_playing()) {
    return;
  }
  for (int i = 0; i < 3; ++i) {
    const float phase = s_ripple + static_cast<float>(i) * 0.33f;
    const float t = phase - floorf(phase);
    const int r = 70 + static_cast<int>(t * 90.f);
    const float alpha = 1.f - t;
    if (alpha <= 0.05f) {
      continue;
    }
    const uint16_t c = chakra_color(ch, 0.15f + 0.45f * alpha);
    pm_gfx->drawCircle(kCx, kCy - 8, r, c);
    pm_gfx->drawCircle(kCx, kCy - 8, r + 1, c);
  }
}

void pm_face_chakra_draw(void) {
  const ChakraDef &ch = kChakras[s_index];
  const uint16_t bg = pm_gfx->color565(6, 6, 10);
  pm_gfx->fillScreen(bg);

  draw_ripples(ch);
  draw_symbol(ch);

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
  int n = static_cast<int>(sizeof(kChakras) / sizeof(kChakras[0]));
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
  s_ripple = 0.f;
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
  if (now_ms - s_last_anim_ms < 40u) {
    return s_chakra_tone_on || pm_speaker_is_playing() || s_ripple_active;
  }
  s_last_anim_ms = now_ms;
  s_ripple = static_cast<float>(now_ms - s_ripple_start) * 0.0012f;
  if (!pm_speaker_is_playing()) {
    s_chakra_tone_on = false;
    s_ripple_active = false;
  }
  return s_chakra_tone_on || pm_speaker_is_playing() || s_ripple_active;
}

void pm_face_chakra_stop(void) { chakra_stop_tone(); }

int pm_face_chakra_index(void) { return s_index; }
