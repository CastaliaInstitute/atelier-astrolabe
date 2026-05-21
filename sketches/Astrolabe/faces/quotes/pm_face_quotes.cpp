#include "faces/quotes/pm_face_quotes.h"

#include <Arduino_GFX_Library.h>
#include <cstdio>
#include <cstring>

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

}  // namespace

void pm_face_quotes_draw(void) {
  const uint16_t c_bg = pm_gfx->color565(6, 8, 16);
  const uint16_t c_panel = pm_gfx->color565(12, 14, 25);
  const uint16_t c_hi = pm_gfx->color565(248, 238, 218);
  const uint16_t c_quote = pm_gfx->color565(238, 232, 222);
  const uint16_t c_dim = pm_gfx->color565(142, 150, 170);
  const uint16_t c_accent = pm_gfx->color565(215, 185, 118);

  pm_gfx->fillScreen(c_bg);
  pm_gfx->fillRect(0, 0, LCD_WIDTH, 52, c_panel);
  pm_face_draw_centered_line("QUOTES", 9, c_accent, 2, 2);

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

  pm_face_draw_centered_line(faculty.name, 36, c_hi, 1, 1);
  pm_faculty_draw_bust_for(&faculty);
  if (pm_faculty_bust_status() == PmFacultyBustStatus::Working) {
    pm_face_draw_centered_line("fetching portrait", 63, c_dim, 1, 1);
  } else if (!pm_faculty_bust_ready_for(faculty.slug) && pm_wifi_connected()) {
    pm_face_draw_centered_line(pm_faculty_bust_last_error(), 63, c_dim, 1, 1);
  }

  pm_gfx->fillRect(0, LCD_HEIGHT - 150, LCD_WIDTH, 150, c_panel);
  draw_wrapped_center(g_quotes_ui.quote, LCD_HEIGHT - 137, c_quote, 38, 4);

  char source[92];
  if (g_quotes_ui.passage[0]) {
    short_line(g_quotes_ui.passage, source, sizeof(source), 50);
  } else if (g_quotes_ui.book_title[0]) {
    short_line(g_quotes_ui.book_title, source, sizeof(source), 50);
  } else {
    snprintf(source, sizeof(source), "quote of the day");
  }
  pm_face_draw_centered_line(source, LCD_HEIGHT - 39, c_accent, 1, 1);

  char meta[72];
  if (g_quotes_ui.book_author[0]) {
    char author[48];
    short_line(g_quotes_ui.book_author, author, sizeof(author), 32);
    snprintf(meta, sizeof(meta), "by %s", author);
  } else if (g_quotes_ui.total > 0) {
    snprintf(meta, sizeof(meta), "%s  %d/%d", g_quotes_ui.date, g_quotes_ui.index, g_quotes_ui.total);
  } else {
    snprintf(meta, sizeof(meta), "%s", g_quotes_ui.date);
  }
  pm_face_draw_centered_line(meta, LCD_HEIGHT - 18, c_dim, 1, 1);
}
