#include "pm_face_safe.h"

#include <Arduino_GFX_Library.h>
#include <WiFi.h>
#include <cstdio>
#include <ctime>

#include "pin_config.h"
#include "pm_diag.h"
#include "pm_ota.h"
#include "pm_runtime_version.h"
#include "pm_wifi_ntp.h"

static void safe_centered(Arduino_GFX *gfx, const char *text, int y, uint16_t fg, uint8_t sx, uint8_t sy) {
  if (!text || !text[0]) {
    return;
  }
  gfx->setTextSize(sx, sy);
  int16_t x1 = 0;
  int16_t y1 = 0;
  uint16_t tw = 0;
  uint16_t th = 0;
  gfx->getTextBounds(text, 0, y, &x1, &y1, &tw, &th);
  const int x = (LCD_WIDTH - static_cast<int>(tw)) / 2;
  gfx->setCursor(x, y);
  gfx->setTextColor(fg);
  gfx->print(text);
}

void pm_face_safe_draw(Arduino_GFX *gfx, const char *status_line) {
  if (!gfx) {
    return;
  }
  gfx->fillScreen(gfx->color565(8, 10, 18));

  struct tm tm = {};
  char tbuf[32] = "time --:--";
  if (pm_time_valid()) {
    pm_time_local(&tm);
    snprintf(tbuf, sizeof(tbuf), "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
  }

  char line[80];
  snprintf(line, sizeof(line), "SAFE  %s", MYNAH_RUNTIME_VERSION);
  safe_centered(gfx, line, 60, gfx->color565(255, 200, 120), 2, 2);
  safe_centered(gfx, tbuf, 110, gfx->color565(220, 220, 240), 2, 2);

  snprintf(line, sizeof(line), "WiFi %s", WiFi.isConnected() ? "on" : "off");
  safe_centered(gfx, line, 160, gfx->color565(180, 200, 220), 1, 2);

  const int batt = 0;  // TODO: PMIC read when wired
  snprintf(line, sizeof(line), "Battery %d%%", batt);
  safe_centered(gfx, line, 190, gfx->color565(160, 180, 200), 1, 2);

  char ota_line[96];
  pm_ota_status_line(ota_line, sizeof(ota_line));
  safe_centered(gfx, ota_line, 230, gfx->color565(140, 180, 255), 1, 1);

  if (pm_diag_safe_mode_reason()[0]) {
    safe_centered(gfx, pm_diag_safe_mode_reason(), 280, gfx->color565(255, 120, 120), 1, 1);
  }
  if (status_line && status_line[0]) {
    safe_centered(gfx, status_line, 320, gfx->color565(200, 200, 200), 1, 1);
  }
  safe_centered(gfx, "serial: safe | ota status", 380, gfx->color565(120, 130, 150), 1, 1);
}
