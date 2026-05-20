#include "faces/notes/pm_face_notes.h"

#include <Arduino_GFX_Library.h>
#include <cstdio>

#include "faces/shared/pm_face_draw.h"
#include "pin_config.h"
#include "pm_castalia_auth.h"
#include "pm_commonplace.h"
#include "pm_display.h"
#include "pm_wifi_ntp.h"

void pm_face_notes_draw(void) {
  const uint16_t c_bg = pm_gfx->color565(8, 10, 16);
  const uint16_t c_panel = pm_gfx->color565(20, 22, 30);
  const uint16_t c_ink = pm_gfx->color565(238, 236, 226);
  const uint16_t c_dim = pm_gfx->color565(142, 154, 168);
  const uint16_t c_accent = pm_gfx->color565(116, 210, 190);
  const uint16_t c_gold = pm_gfx->color565(230, 190, 116);

  pm_gfx->fillScreen(c_bg);
  pm_gfx->fillRect(0, 0, LCD_WIDTH, 54, c_panel);
  pm_face_draw_centered_line("NOTES", 10, c_accent, 2, 2);

  pm_gfx->drawCircle(LCD_WIDTH / 2, LCD_HEIGHT / 2 - 14, 128, pm_gfx->color565(42, 56, 62));
  pm_gfx->drawCircle(LCD_WIDTH / 2, LCD_HEIGHT / 2 - 14, 118, pm_gfx->color565(30, 42, 50));
  pm_gfx->fillCircle(LCD_WIDTH / 2, LCD_HEIGHT / 2 - 14, 62, pm_gfx->color565(18, 28, 34));
  pm_gfx->drawCircle(LCD_WIDTH / 2, LCD_HEIGHT / 2 - 14, 62, c_accent);

  const int cx = LCD_WIDTH / 2;
  const int cy = LCD_HEIGHT / 2 - 14;
  pm_gfx->fillRect(cx - 18, cy - 34, 36, 68, c_ink);
  pm_gfx->fillRect(cx - 12, cy - 44, 24, 16, c_gold);
  pm_gfx->drawRect(cx - 18, cy - 34, 36, 68, c_accent);
  for (int y = cy - 18; y <= cy + 18; y += 14) {
    pm_gfx->drawLine(cx - 8, y, cx + 12, y, pm_gfx->color565(80, 94, 104));
  }

  pm_face_draw_centered_line("hold PWR to dictate", 338, c_ink, 1, 1);
  pm_face_draw_centered_line("offline queue to flash", 365, c_dim, 1, 1);

  char line[64];
  const size_t queued = pm_commonplace_offline_note_count();
  snprintf(line, sizeof(line), "queued %u  %s", static_cast<unsigned>(queued),
           pm_wifi_connected() ? (pm_castalia_has_session() ? "ready" : "sign in") : "offline");
  pm_face_draw_centered_line(line, 397, queued > 0 ? c_gold : c_dim, 1, 1);
}
