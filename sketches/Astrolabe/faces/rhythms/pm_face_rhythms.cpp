#include "faces/rhythms/pm_face_rhythms.h"

#include <cstring>

#include "faces/shared/pm_face_draw.h"
#include "pin_config.h"
#include "pm_display.h"
#include "pm_rhythms.h"
#include "pm_wifi_ntp.h"

static void draw_centered_text(const char *text, int y, uint16_t color, uint8_t size = 1) {
  pm_face_draw_centered_line(text, y, color, size, size);
}

static int draw_wrapped_text(const char *text, int x, int y, int max_chars, int max_lines, uint16_t color) {
  if (!text || max_chars < 8 || max_lines < 1) {
    return y;
  }
  pm_gfx->setTextSize(1, 1);
  pm_gfx->setTextColor(color);
  char line[72] = "";
  int line_len = 0;
  int lines = 0;
  const char *p = text;
  while (*p && lines < max_lines) {
    while (*p == ' ') {
      ++p;
    }
    char word[28] = "";
    int wi = 0;
    while (*p && *p != ' ' && wi + 1 < static_cast<int>(sizeof(word))) {
      word[wi++] = *p++;
    }
    word[wi] = '\0';
    if (wi == 0) {
      break;
    }
    const int need = line_len == 0 ? wi : wi + 1;
    if (line_len > 0 && line_len + need > max_chars) {
      pm_gfx->setCursor(x, y);
      pm_gfx->print(line);
      y += 14;
      ++lines;
      line[0] = '\0';
      line_len = 0;
      if (lines >= max_lines) {
        break;
      }
    }
    if (line_len > 0) {
      strncat(line, " ", sizeof(line) - strlen(line) - 1);
      ++line_len;
    }
    strncat(line, word, sizeof(line) - strlen(line) - 1);
    line_len += wi;
  }
  if (line_len > 0 && lines < max_lines) {
    pm_gfx->setCursor(x, y);
    pm_gfx->print(line);
    y += 14;
  }
  return y;
}

void pm_face_rhythms_draw(void) {
  pm_gfx->fillScreen(pm_gfx->color565(9, 10, 18));
  const int cx = LCD_WIDTH / 2;
  const int cy = LCD_HEIGHT / 2;
  const int r = min(LCD_WIDTH, LCD_HEIGHT) / 2 - 18;
  const uint16_t c_edge = pm_gfx->color565(78, 58, 106);
  const uint16_t c_panel = pm_gfx->color565(18, 18, 32);
  const uint16_t c_title = pm_gfx->color565(245, 222, 168);
  const uint16_t c_text = pm_gfx->color565(220, 226, 236);
  const uint16_t c_dim = pm_gfx->color565(144, 150, 170);
  const uint16_t c_accent = pm_gfx->color565(178, 145, 255);

  pm_gfx->drawCircle(cx, cy, r, c_edge);
  pm_gfx->drawCircle(cx, cy, r - 1, pm_gfx->color565(40, 34, 58));
  pm_gfx->fillRoundRect(39, 78, LCD_WIDTH - 78, 314, 24, c_panel);
  pm_gfx->drawRoundRect(39, 78, LCD_WIDTH - 78, 314, 24, pm_gfx->color565(58, 50, 78));

  draw_centered_text("CASTALIAN", 52, c_dim, 1);
  draw_centered_text("RHYTHMS", 65, c_accent, 1);

  if (!pm_time_valid()) {
    draw_centered_text("daily card", 172, c_title, 2);
    draw_centered_text("waiting for", 214, c_dim, 2);
    draw_centered_text("NTP time", 240, c_dim, 2);
    return;
  }
  if (!pm_rhythms_prefetch_daily_card()) {
    draw_centered_text("daily card", 172, c_title, 2);
    draw_centered_text("card pending", 224, c_dim, 2);
    return;
  }
  const PmRhythmsDailyCard *card = pm_rhythms_daily_card_cached();
  if (!card) {
    draw_centered_text("card unavailable", 224, c_dim, 2);
    return;
  }

  draw_centered_text(card->date_label, 94, c_dim, 1);
  draw_wrapped_text(card->title, 74, 122, 26, 2, c_title);
  pm_gfx->drawFastHLine(72, 166, LCD_WIDTH - 144, pm_gfx->color565(70, 58, 96));

  int y = 184;
  y = draw_wrapped_text(card->theme, 63, y, 47, 3, c_text);
  y += 6;
  y = draw_wrapped_text(card->guidance, 63, y, 47, 3, pm_gfx->color565(198, 210, 225));

  pm_gfx->fillRoundRect(58, 318, LCD_WIDTH - 116, 38, 14, pm_gfx->color565(26, 24, 42));
  draw_centered_text(card->primary_symbol, 328, c_accent, 1);
  draw_centered_text(card->secondary_symbol, 343, c_dim, 1);
  draw_wrapped_text(card->precision, 74, 366, 44, 2, pm_gfx->color565(132, 138, 154));
}

void pm_face_rhythms_draw_voice_screen(const char *status, float thinking_progress) {
  pm_face_rhythms_draw();
  if (thinking_progress >= 0.f) {
    pm_face_draw_thinking_progress_ring(thinking_progress);
  } else if (status && status[0] != '\0') {
    pm_gfx->fillRect(0, 0, LCD_WIDTH, 42, pm_gfx->color565(10, 10, 18));
    draw_centered_text(status, 12, pm_gfx->color565(220, 205, 255), 1);
  }
  pm_gfx->flush();
}
