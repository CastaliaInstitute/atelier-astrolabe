#include "faces/pythia/pm_face_pythia.h"

#include <Arduino_GFX_Library.h>
#include <cstdio>

#include "faces/shared/pm_face_draw.h"
#include "pin_config.h"
#include "pm_display.h"

namespace {

uint16_t col(uint8_t r, uint8_t g, uint8_t b) { return pm_gfx->color565(r, g, b); }

void draw_laurel(int cx, int cy, int r) {
  const uint16_t leaf = col(138, 170, 108);
  const uint16_t dim = col(54, 74, 58);
  for (int i = 0; i < 13; ++i) {
    const float t = static_cast<float>(i) / 12.f;
    const int y = cy - r + 18 + static_cast<int>(t * (r * 1.18f));
    const int dx = 24 + static_cast<int>(t * 28.f);
    pm_gfx->drawLine(cx - dx, y, cx - dx - 18, y - 8, dim);
    pm_gfx->fillEllipse(cx - dx - 21, y - 10, 9, 4, leaf);
    pm_gfx->drawLine(cx + dx, y, cx + dx + 18, y - 8, dim);
    pm_gfx->fillEllipse(cx + dx + 21, y - 10, 9, 4, leaf);
  }
}

void draw_vapor(uint32_t now_ms) {
  const uint16_t smoke = col(118, 150, 154);
  for (int i = 0; i < 9; ++i) {
    const int phase = static_cast<int>((now_ms / 85u + i * 19u) % 120u);
    const int y = 384 - phase * 2;
    const int x = LCD_WIDTH / 2 - 86 + i * 22 + ((phase % 18) - 9);
    if (y > 54 && y < LCD_HEIGHT - 22) {
      pm_gfx->drawCircle(x, y, 6 + (phase % 9), smoke);
    }
  }
}

void draw_bust(void) {
  const int cx = LCD_WIDTH / 2;
  const int cy = 214;
  const uint16_t marble = col(214, 206, 190);
  const uint16_t shadow = col(120, 112, 112);
  const uint16_t ink = col(54, 45, 58);
  const uint16_t gold = col(224, 176, 86);
  const uint16_t robe = col(92, 58, 92);
  const uint16_t dark_robe = col(42, 28, 52);

  pm_gfx->fillCircle(cx, cy - 56, 66, shadow);
  pm_gfx->fillCircle(cx, cy - 60, 62, marble);
  pm_gfx->fillTriangle(cx - 70, cy - 64, cx - 24, cy - 128, cx - 8, cy - 62, shadow);
  pm_gfx->fillTriangle(cx + 70, cy - 64, cx + 24, cy - 128, cx + 8, cy - 62, shadow);
  pm_gfx->fillCircle(cx - 28, cy - 74, 18, col(236, 226, 208));
  pm_gfx->fillCircle(cx + 28, cy - 74, 18, col(236, 226, 208));
  pm_gfx->drawLine(cx - 22, cy - 56, cx - 48, cy - 52, ink);
  pm_gfx->drawLine(cx + 22, cy - 56, cx + 48, cy - 52, ink);
  pm_gfx->fillCircle(cx - 24, cy - 48, 4, ink);
  pm_gfx->fillCircle(cx + 24, cy - 48, 4, ink);
  pm_gfx->drawLine(cx, cy - 42, cx - 6, cy - 20, shadow);
  pm_gfx->drawLine(cx - 16, cy - 4, cx + 16, cy - 4, ink);

  pm_gfx->fillRect(cx - 32, cy + 2, 64, 58, marble);
  pm_gfx->fillEllipse(cx, cy + 108, 132, 76, dark_robe);
  pm_gfx->fillEllipse(cx, cy + 92, 106, 64, robe);
  pm_gfx->fillTriangle(cx - 96, cy + 84, cx, cy + 34, cx + 96, cy + 84, robe);
  pm_gfx->drawLine(cx - 64, cy + 58, cx - 16, cy + 150, gold);
  pm_gfx->drawLine(cx + 64, cy + 58, cx + 16, cy + 150, gold);
  pm_gfx->fillEllipse(cx, cy + 151, 128, 18, col(112, 98, 82));
  pm_gfx->drawEllipse(cx, cy + 151, 128, 18, gold);

  draw_laurel(cx, cy - 36, 112);
  pm_gfx->drawCircle(cx, cy - 60, 84, col(194, 158, 90));
  pm_gfx->drawCircle(cx, cy - 60, 88, col(82, 70, 48));
}

}  // namespace

void pm_face_pythia_draw(void) {
  pm_gfx->fillScreen(col(8, 11, 18));
  for (int r = 210; r > 24; r -= 18) {
    const uint16_t ring = (r % 36 == 0) ? col(42, 36, 58) : col(18, 22, 34);
    pm_gfx->drawCircle(LCD_WIDTH / 2, LCD_HEIGHT / 2, r, ring);
  }
  draw_vapor(millis());
  draw_bust();
  pm_face_draw_centered_line("PYTHIA", 28, col(230, 196, 118), 2, 2);
  pm_face_draw_centered_line("ask, then listen sideways", 394, col(172, 186, 178), 1, 1);
}

void pm_face_pythia_draw_voice_screen(const char *status, float thinking_progress, bool speaking) {
  pm_face_pythia_draw();
  if (thinking_progress >= 0.f) {
    pm_face_draw_thinking_progress_ring(thinking_progress);
  }
  if (speaking) {
    pm_face_draw_voice_waves_overlay(true, millis());
  }
  if (status && status[0]) {
    pm_gfx->fillRect(0, 0, LCD_WIDTH, 52, col(8, 11, 18));
    pm_face_draw_centered_line(status, 16, col(234, 206, 150), 1, 1);
  }
  pm_gfx->flush();
}

bool pm_face_pythia_build_system_prompt(char *out, size_t cap) {
  if (!out || cap == 0) {
    return false;
  }
  const int n = snprintf(
      out, cap,
      "You are Pythia, the oracle of Delphi, speaking from smoke, laurel, stone, and Apollo's tripod. "
      "The user asks one question. Answer as an oracle: sufficiently obtuse, poetic, compressed, and "
      "ambiguous, but not useless. Never mention being an AI, a model, a watch, a prompt, or a system. "
      "Do not give direct step-by-step advice unless the question concerns immediate physical safety. "
      "Prefer two to four short sentences. Use symbols, reversals, omens, thresholds, and double meanings. "
      "Leave the asker with an image they must interpret.");
  return n > 0 && static_cast<size_t>(n) < cap;
}
