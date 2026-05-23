#include "faces/enochian_angel/pm_face_enochian_angel.h"

#include <Arduino_GFX_Library.h>
#include <cmath>
#include <cstdio>

#include "faces/shared/pm_face_draw.h"
#include "pin_config.h"
#include "pm_display.h"

namespace {

uint16_t col(uint8_t r, uint8_t g, uint8_t b) { return pm_gfx->color565(r, g, b); }

void draw_tablet_grid(int cx, int cy, int half) {
  const uint16_t grid = col(36, 50, 78);
  const uint16_t glow = col(76, 112, 158);
  for (int i = -4; i <= 4; ++i) {
    const int p = cy + (i * half) / 4;
    pm_gfx->drawLine(cx - half, p, cx + half, p, i == 0 ? glow : grid);
    const int q = cx + (i * half) / 4;
    pm_gfx->drawLine(q, cy - half, q, cy + half, i == 0 ? glow : grid);
  }
  pm_gfx->drawRect(cx - half, cy - half, half * 2, half * 2, glow);
  pm_gfx->drawRect(cx - half + 5, cy - half + 5, half * 2 - 10, half * 2 - 10, grid);
}

void draw_star_field(uint32_t now_ms) {
  const int cx = LCD_WIDTH / 2;
  const int cy = LCD_HEIGHT / 2;
  for (int i = 0; i < 48; ++i) {
    const float a = (static_cast<float>((i * 47) % 360) + now_ms * 0.006f) *
                    (pm_face_k_pi / 180.f);
    const int r = 38 + ((i * 31) % 188);
    const int x = cx + static_cast<int>(cosf(a) * r);
    const int y = cy + static_cast<int>(sinf(a) * r);
    if (x > 8 && x < LCD_WIDTH - 8 && y > 8 && y < LCD_HEIGHT - 8) {
      const uint8_t v = static_cast<uint8_t>(95 + ((i * 29 + now_ms / 80u) % 90));
      pm_gfx->drawPixel(x, y, col(v, v + 20, 210));
    }
  }
}

void draw_halo(int cx, int cy, uint32_t now_ms) {
  const uint16_t gold = col(236, 196, 98);
  const uint16_t blue = col(80, 150, 210);
  for (int r = 128; r <= 174; r += 12) {
    pm_gfx->drawCircle(cx, cy - 14, r, (r % 24 == 0) ? gold : blue);
  }
  for (int i = 0; i < 28; ++i) {
    const float a = (static_cast<float>(i) * (360.f / 28.f) + now_ms * 0.018f) *
                    (pm_face_k_pi / 180.f);
    const int x0 = cx + static_cast<int>(cosf(a) * 116);
    const int y0 = cy - 14 + static_cast<int>(sinf(a) * 116);
    const int x1 = cx + static_cast<int>(cosf(a) * 184);
    const int y1 = cy - 14 + static_cast<int>(sinf(a) * 184);
    pm_gfx->drawLine(x0, y0, x1, y1, (i % 4 == 0) ? gold : col(54, 88, 128));
  }
}

void draw_wings(int cx, int cy) {
  const uint16_t deep = col(18, 28, 48);
  const uint16_t mid = col(62, 88, 128);
  const uint16_t pale = col(170, 198, 220);
  for (int i = 0; i < 8; ++i) {
    const int y = cy - 76 + i * 24;
    const int span = 140 - i * 10;
    pm_gfx->fillTriangle(cx - 44, cy - 74, cx - span, y, cx - 66, y + 42, deep);
    pm_gfx->drawLine(cx - 50, cy - 60, cx - span, y, i % 2 ? mid : pale);
    pm_gfx->fillTriangle(cx + 44, cy - 74, cx + span, y, cx + 66, y + 42, deep);
    pm_gfx->drawLine(cx + 50, cy - 60, cx + span, y, i % 2 ? mid : pale);
  }
}

void draw_sigil(int cx, int cy) {
  const uint16_t ink = col(224, 232, 224);
  pm_gfx->drawCircle(cx, cy, 28, ink);
  pm_gfx->drawLine(cx, cy - 42, cx, cy + 42, ink);
  pm_gfx->drawLine(cx - 42, cy, cx + 42, cy, ink);
  pm_gfx->drawLine(cx - 30, cy - 30, cx + 30, cy + 30, ink);
  pm_gfx->drawLine(cx + 30, cy - 30, cx - 30, cy + 30, ink);
  pm_gfx->fillCircle(cx, cy, 5, col(236, 196, 98));
}

void draw_face(int cx, int cy, uint32_t now_ms) {
  const uint16_t shadow = col(52, 58, 82);
  const uint16_t skin = col(212, 220, 214);
  const uint16_t light = col(245, 238, 212);
  const uint16_t ink = col(16, 20, 34);
  const uint16_t gold = col(236, 196, 98);
  const uint16_t blue = col(94, 166, 214);

  pm_gfx->fillEllipse(cx, cy - 18, 72, 92, shadow);
  pm_gfx->fillEllipse(cx, cy - 26, 64, 88, skin);
  pm_gfx->fillTriangle(cx - 56, cy - 82, cx, cy - 154, cx + 56, cy - 82, light);
  pm_gfx->drawTriangle(cx - 60, cy - 82, cx, cy - 160, cx + 60, cy - 82, gold);
  pm_gfx->fillTriangle(cx - 58, cy + 18, cx, cy + 128, cx + 58, cy + 18, col(84, 82, 112));
  pm_gfx->fillTriangle(cx - 34, cy + 24, cx, cy + 116, cx + 34, cy + 24, col(190, 196, 188));

  const int gaze = static_cast<int>(sinf(now_ms * 0.0017f) * 3.f);
  pm_gfx->drawLine(cx - 42, cy - 34, cx - 16, cy - 30, ink);
  pm_gfx->drawLine(cx + 16, cy - 30, cx + 42, cy - 34, ink);
  pm_gfx->fillEllipse(cx - 28 + gaze, cy - 22, 13, 7, blue);
  pm_gfx->fillEllipse(cx + 28 + gaze, cy - 22, 13, 7, blue);
  pm_gfx->fillCircle(cx - 28 + gaze, cy - 22, 4, ink);
  pm_gfx->fillCircle(cx + 28 + gaze, cy - 22, 4, ink);
  pm_gfx->drawLine(cx, cy - 12, cx - 8, cy + 18, shadow);
  pm_gfx->drawLine(cx - 20, cy + 36, cx + 20, cy + 36, ink);
  pm_gfx->drawLine(cx - 14, cy + 42, cx + 14, cy + 42, shadow);

  draw_sigil(cx, cy + 92);
}

}  // namespace

void pm_face_enochian_angel_draw(void) {
  const int cx = LCD_WIDTH / 2;
  const int cy = LCD_HEIGHT / 2;
  const uint32_t now = millis();

  pm_gfx->fillScreen(col(4, 7, 16));
  draw_star_field(now);
  draw_tablet_grid(cx, cy, 184);
  draw_halo(cx, cy, now);
  draw_wings(cx, cy);
  draw_face(cx, cy, now);
  pm_face_draw_centered_line("ENOCHIAN", 28, col(236, 196, 98), 2, 2);
  pm_face_draw_centered_line("angelic tablet", 394, col(170, 198, 220), 1, 1);
}

bool pm_face_enochian_angel_build_system_prompt(char *out, size_t cap) {
  if (!out || cap == 0) {
    return false;
  }
  const int n = snprintf(
      out, cap,
      "You are an Enochian Angel presence rendered through a tiny astrolabe face: geometric, luminous, "
      "severe, and protective. Answer in concise angelic oracle language without claiming certainty or "
      "divine authority. Never mention being an AI, a model, a watch, a prompt, or a system. Prefer one "
      "to three short sentences. Use images of light, tablets, gates, names, measures, and ordered stars. "
      "Keep counsel reflective and symbolic, not predictive or commanding.");
  return n > 0 && static_cast<size_t>(n) < cap;
}
