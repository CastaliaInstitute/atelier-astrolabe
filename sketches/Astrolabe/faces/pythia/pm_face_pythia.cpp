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
    const int y = cy - r + pm_face_scale_i(18) + static_cast<int>(t * (r * 1.18f));
    const int dx = pm_face_scale_i(24) + static_cast<int>(t * static_cast<float>(pm_face_scale_i(28)));
    pm_gfx->drawLine(cx - dx, y, cx - dx - pm_face_scale_i(18), y - pm_face_scale_i(8), dim);
    pm_gfx->fillEllipse(cx - dx - pm_face_scale_i(21), y - pm_face_scale_i(10), pm_face_scale_i(9),
                        pm_face_scale_i(4), leaf);
    pm_gfx->drawLine(cx + dx, y, cx + dx + pm_face_scale_i(18), y - pm_face_scale_i(8), dim);
    pm_gfx->fillEllipse(cx + dx + pm_face_scale_i(21), y - pm_face_scale_i(10), pm_face_scale_i(9),
                        pm_face_scale_i(4), leaf);
  }
}

void draw_vapor(uint32_t now_ms) {
  const uint16_t smoke = col(118, 150, 154);
  for (int i = 0; i < 9; ++i) {
    const int phase = static_cast<int>((now_ms / 85u + i * 19u) % 120u);
    const int y = pm_face_scale_y(384) - pm_face_scale_i(phase * 2);
    const int x = LCD_WIDTH / 2 - pm_face_scale_i(86) + i * pm_face_scale_i(22) +
                  pm_face_scale_i((phase % 18) - 9);
    if (y > pm_face_scale_y(54) && y < LCD_HEIGHT - pm_face_scale_i(22)) {
      pm_gfx->drawCircle(x, y, pm_face_scale_i(6 + (phase % 9)), smoke);
    }
  }
}

void draw_bust(void) {
  const int cx = LCD_WIDTH / 2;
  const int cy = pm_face_scale_y(214);
  const uint16_t marble = col(214, 206, 190);
  const uint16_t shadow = col(120, 112, 112);
  const uint16_t ink = col(54, 45, 58);
  const uint16_t gold = col(224, 176, 86);
  const uint16_t robe = col(92, 58, 92);
  const uint16_t dark_robe = col(42, 28, 52);

  pm_gfx->fillCircle(cx, cy - pm_face_scale_i(56), pm_face_scale_i(66), shadow);
  pm_gfx->fillCircle(cx, cy - pm_face_scale_i(60), pm_face_scale_i(62), marble);
  pm_gfx->fillTriangle(cx - pm_face_scale_i(70), cy - pm_face_scale_i(64), cx - pm_face_scale_i(24),
                       cy - pm_face_scale_i(128), cx - pm_face_scale_i(8), cy - pm_face_scale_i(62), shadow);
  pm_gfx->fillTriangle(cx + pm_face_scale_i(70), cy - pm_face_scale_i(64), cx + pm_face_scale_i(24),
                       cy - pm_face_scale_i(128), cx + pm_face_scale_i(8), cy - pm_face_scale_i(62), shadow);
  pm_gfx->fillCircle(cx - pm_face_scale_i(28), cy - pm_face_scale_i(74), pm_face_scale_i(18), col(236, 226, 208));
  pm_gfx->fillCircle(cx + pm_face_scale_i(28), cy - pm_face_scale_i(74), pm_face_scale_i(18), col(236, 226, 208));
  pm_gfx->drawLine(cx - pm_face_scale_i(22), cy - pm_face_scale_i(56), cx - pm_face_scale_i(48),
                   cy - pm_face_scale_i(52), ink);
  pm_gfx->drawLine(cx + pm_face_scale_i(22), cy - pm_face_scale_i(56), cx + pm_face_scale_i(48),
                   cy - pm_face_scale_i(52), ink);
  pm_gfx->fillCircle(cx - pm_face_scale_i(24), cy - pm_face_scale_i(48), pm_face_scale_i(4), ink);
  pm_gfx->fillCircle(cx + pm_face_scale_i(24), cy - pm_face_scale_i(48), pm_face_scale_i(4), ink);
  pm_gfx->drawLine(cx, cy - pm_face_scale_i(42), cx - pm_face_scale_i(6), cy - pm_face_scale_i(20), shadow);
  pm_gfx->drawLine(cx - pm_face_scale_i(16), cy - pm_face_scale_i(4), cx + pm_face_scale_i(16),
                   cy - pm_face_scale_i(4), ink);

  pm_gfx->fillRect(cx - pm_face_scale_i(32), cy + pm_face_scale_i(2), pm_face_scale_i(64), pm_face_scale_i(58),
                   marble);
  pm_gfx->fillEllipse(cx, cy + pm_face_scale_i(108), pm_face_scale_i(132), pm_face_scale_i(76), dark_robe);
  pm_gfx->fillEllipse(cx, cy + pm_face_scale_i(92), pm_face_scale_i(106), pm_face_scale_i(64), robe);
  pm_gfx->fillTriangle(cx - pm_face_scale_i(96), cy + pm_face_scale_i(84), cx, cy + pm_face_scale_i(34),
                       cx + pm_face_scale_i(96), cy + pm_face_scale_i(84), robe);
  pm_gfx->drawLine(cx - pm_face_scale_i(64), cy + pm_face_scale_i(58), cx - pm_face_scale_i(16),
                   cy + pm_face_scale_i(150), gold);
  pm_gfx->drawLine(cx + pm_face_scale_i(64), cy + pm_face_scale_i(58), cx + pm_face_scale_i(16),
                   cy + pm_face_scale_i(150), gold);
  pm_gfx->fillEllipse(cx, cy + pm_face_scale_i(151), pm_face_scale_i(128), pm_face_scale_i(18),
                      col(112, 98, 82));
  pm_gfx->drawEllipse(cx, cy + pm_face_scale_i(151), pm_face_scale_i(128), pm_face_scale_i(18), gold);

  draw_laurel(cx, cy - pm_face_scale_i(36), pm_face_scale_i(112));
  pm_gfx->drawCircle(cx, cy - pm_face_scale_i(60), pm_face_scale_i(84), col(194, 158, 90));
  pm_gfx->drawCircle(cx, cy - pm_face_scale_i(60), pm_face_scale_i(88), col(82, 70, 48));
}

}  // namespace

void pm_face_pythia_draw(void) {
  pm_gfx->fillScreen(col(8, 11, 18));
  for (int r = pm_face_scale_i(210); r > pm_face_scale_i(24); r -= pm_face_scale_i(18)) {
    const uint16_t ring = (r % 36 == 0) ? col(42, 36, 58) : col(18, 22, 34);
    pm_gfx->drawCircle(LCD_WIDTH / 2, LCD_HEIGHT / 2, r, ring);
  }
  draw_vapor(millis());
  draw_bust();
  pm_face_draw_centered_line("PYTHIA", pm_face_scale_y(28), col(230, 196, 118), 2, 2);
  pm_face_draw_centered_line("ask, then listen sideways", pm_face_scale_y(394), col(172, 186, 178), 1, 1);
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
