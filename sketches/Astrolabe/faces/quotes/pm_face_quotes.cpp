#include "faces/quotes/pm_face_quotes.h"

#include <Arduino_GFX_Library.h>
#include <cstdio>
#include <cstring>

#include "faces/quotes/FreeSerifBoldItalic12pt7b.h"
#include "faces/shared/pm_face_draw.h"
#include "pin_config.h"
#include "pm_display.h"
#include "pm_faculty.h"
#include "pm_wifi_ntp.h"

PmQuoteOfDay g_quotes_ui = {};

namespace {

void short_line(const char *in, char *out, size_t cap, size_t max_chars) {
  if (!out || cap == 0) {
    return;
  }
  out[0] = '\0';
  if (!in) {
    return;
  }
  size_t n = strlen(in);
  if (n > max_chars) {
    n = max_chars;
  }
  if (n + 4 > cap) {
    n = cap > 4 ? cap - 4 : 0;
  }
  memcpy(out, in, n);
  out[n] = '\0';
  if (in[n] != '\0' && n + 3 < cap) {
    strcat(out, "...");
  }
}

void draw_wrapped_center(const char *text, int y, uint16_t color, int max_chars, int max_lines) {
  if (!text || !text[0] || max_lines <= 0) {
    return;
  }
  const char *p = text;
  char line[72];
  int line_no = 0;
  while (*p && line_no < max_lines) {
    while (*p == ' ') {
      ++p;
    }
    size_t n = 0;
    size_t last_space = 0;
    while (p[n] && n < static_cast<size_t>(max_chars)) {
      if (p[n] == ' ') {
        last_space = n;
      }
      ++n;
    }
    if (p[n] && last_space > 0) {
      n = last_space;
    }
    if (n >= sizeof(line)) {
      n = sizeof(line) - 1;
    }
    memcpy(line, p, n);
    line[n] = '\0';
    if (p[n] && line_no == max_lines - 1 && n > 3) {
      line[n - 3] = '.';
      line[n - 2] = '.';
      line[n - 1] = '.';
    }
    pm_face_draw_centered_line(line, y + line_no * 23, color, 1, 1);
    p += n;
    ++line_no;
  }
}

void draw_script_quote(const char *text, int y, uint16_t color) {
  pm_gfx->setFont(&FreeSerifBoldItalic12pt7b);
  pm_gfx->setTextSize(1, 1);
  draw_wrapped_center(text, y + 2, pm_gfx->color565(0, 0, 0), 28, 5);
  draw_wrapped_center(text, y, color, 28, 5);
  pm_gfx->setFont(static_cast<const GFXfont *>(nullptr));
}

}  // namespace

void pm_face_quotes_draw(void) {
  const uint16_t c_bg = pm_gfx->color565(6, 8, 16);
  const uint16_t c_panel = pm_gfx->color565(12, 14, 25);
  const uint16_t c_hi = pm_gfx->color565(248, 238, 218);
  const uint16_t c_quote = pm_gfx->color565(238, 232, 222);
  const uint16_t c_dim = pm_gfx->color565(142, 150, 170);

  pm_gfx->fillScreen(c_bg);

  if (!g_quotes_ui.ok) {
    pm_face_draw_centered_line("quote of the day", 180, c_hi, 2, 2);
    pm_face_draw_centered_line(g_quotes_ui.error[0] ? g_quotes_ui.error : "fetch pending", 218, c_dim, 1, 1);
    if (!pm_wifi_connected()) {
      pm_face_draw_centered_line("needs WiFi", 244, c_dim, 1, 1);
    }
    return;
  }

  PmFacultyProfile faculty = {};
  strncpy(faculty.slug, g_quotes_ui.faculty_slug, sizeof(faculty.slug) - 1);
  strncpy(faculty.name, g_quotes_ui.faculty_name, sizeof(faculty.name) - 1);
  faculty.valid = true;

  if (!pm_faculty_bust_ready_for(faculty.slug)) {
    (void)pm_faculty_request_bust(faculty.slug);
  }
  pm_faculty_draw_bust_for(&faculty);

  pm_gfx->fillRect(0, LCD_HEIGHT - 150, LCD_WIDTH, 150, c_panel);
  draw_script_quote(g_quotes_ui.quote, LCD_HEIGHT - 136, c_quote);
}
