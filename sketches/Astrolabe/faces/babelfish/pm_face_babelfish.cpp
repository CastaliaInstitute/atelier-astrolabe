#include "faces/babelfish/pm_face_babelfish.h"

#include <Arduino_GFX_Library.h>
#include <cmath>
#include <cstdio>

#include "faces/shared/pm_face_draw.h"
#include "pin_config.h"
#include "pm_display.h"

namespace {

uint16_t col(uint8_t r, uint8_t g, uint8_t b) { return pm_gfx->color565(r, g, b); }

void draw_bubble_field(uint32_t now_ms) {
  const uint16_t faint = col(38, 86, 104);
  const uint16_t bright = col(102, 202, 214);
  for (int i = 0; i < 18; ++i) {
    const int phase = static_cast<int>((now_ms / 72u + i * 31u) % 190u);
    const int x = 44 + ((i * 67 + phase * 2) % (LCD_WIDTH - 88));
    const int y = LCD_HEIGHT - 34 - phase * 2;
    if (y > 40 && y < LCD_HEIGHT - 28) {
      const int r = 3 + ((i + phase) % 8);
      pm_gfx->drawCircle(x, y, r, (i % 4 == 0) ? bright : faint);
    }
  }
}

void draw_fish(uint32_t now_ms) {
  const int cx = LCD_WIDTH / 2;
  const int cy = LCD_HEIGHT / 2 + 2;
  const float pulse = 0.5f + 0.5f * sinf(static_cast<float>(now_ms) * 0.0032f);
  const int tail = 54 + static_cast<int>(pulse * 9.f);
  const uint16_t body = col(252, 204, 86);
  const uint16_t body_hi = col(255, 236, 142);
  const uint16_t fin = col(246, 122, 76);
  const uint16_t ink = col(5, 20, 28);
  const uint16_t aqua = col(96, 210, 214);

  pm_gfx->fillTriangle(cx - 118, cy, cx - 118 - tail, cy - 58, cx - 118 - tail, cy + 58, fin);
  pm_gfx->drawTriangle(cx - 118, cy, cx - 118 - tail, cy - 58, cx - 118 - tail, cy + 58,
                       col(148, 72, 58));
  pm_gfx->fillEllipse(cx - 8, cy, 126, 78, body);
  pm_gfx->fillEllipse(cx + 28, cy - 22, 76, 34, body_hi);
  pm_gfx->fillTriangle(cx - 8, cy - 62, cx + 28, cy - 118, cx + 52, cy - 48, fin);
  pm_gfx->fillTriangle(cx - 6, cy + 58, cx + 28, cy + 112, cx + 54, cy + 42, fin);
  pm_gfx->fillCircle(cx + 88, cy - 18, 12, RGB565_WHITE);
  pm_gfx->fillCircle(cx + 92, cy - 16, 5, ink);
  pm_gfx->drawLine(cx + 80, cy + 18, cx + 94, cy + 24, ink);
  pm_gfx->drawLine(cx + 94, cy + 24, cx + 112, cy + 16, ink);

  for (int i = 0; i < 6; ++i) {
    const int x = cx - 74 + i * 28;
    pm_gfx->drawLine(x - 8, cy - 44, x + 8, cy - 18, col(202, 144, 58));
    pm_gfx->drawLine(x + 8, cy - 18, x + 10, cy + 18, col(202, 144, 58));
    pm_gfx->drawLine(x + 10, cy + 18, x - 6, cy + 44, col(202, 144, 58));
  }

  pm_gfx->drawCircle(cx, cy, 168, col(18, 78, 96));
  pm_gfx->drawCircle(cx, cy, 172, aqua);
}

}  // namespace

void pm_face_babelfish_draw(void) {
  const uint32_t now = millis();
  pm_gfx->fillScreen(col(4, 18, 28));
  for (int r = 218; r > 32; r -= 18) {
    pm_gfx->drawCircle(LCD_WIDTH / 2, LCD_HEIGHT / 2, r, (r % 36 == 0) ? col(8, 54, 72) : col(6, 34, 50));
  }
  draw_bubble_field(now);
  draw_fish(now);
  pm_face_draw_centered_line("BABEL FISH", 30, col(156, 238, 222), 2, 2);
  pm_face_draw_centered_line("hold PWR to translate", 392, col(188, 218, 202), 1, 1);
}

void pm_face_babelfish_draw_voice_screen(const char *status, float thinking_progress, bool speaking) {
  pm_face_babelfish_draw();
  if (thinking_progress >= 0.f) {
    pm_face_draw_thinking_progress_ring(thinking_progress);
  }
  if (speaking) {
    pm_face_draw_voice_waves_overlay(true, millis());
  }
  if (status && status[0]) {
    pm_gfx->fillRect(0, 0, LCD_WIDTH, 52, col(4, 18, 28));
    pm_face_draw_centered_line(status, 16, col(156, 238, 222), 1, 1);
  }
  pm_gfx->flush();
}

bool pm_face_babelfish_build_system_prompt(char *out, size_t cap) {
  if (!out || cap == 0) {
    return false;
  }
  const int n = snprintf(
      out, cap,
      "You are the Castalia Astrolabe Babel Fish face, a pocket spoken-language translator. "
      "The device native spoken language is the TTS language selected for this device, normally English. "
      "Translate the user's utterance into the device native language. If the utterance is already in the "
      "device native language, translate it into the most likely other language requested or implied by the "
      "utterance; if none is implied, give a concise natural-language paraphrase and ask which language to "
      "translate into next. Preserve names, numbers, places, and intent. Reply with only the translated speech "
      "or the short clarification. Do not explain the translation process, mention STT, LLM, TTS, prompts, or "
      "the watch. Keep the spoken reply under 25 seconds.");
  return n > 0 && static_cast<size_t>(n) < cap;
}
