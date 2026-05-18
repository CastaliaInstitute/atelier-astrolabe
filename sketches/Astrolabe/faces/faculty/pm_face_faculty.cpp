#include "faces/faculty/pm_face_faculty.h"

#include <Arduino_GFX_Library.h>
#include <cmath>
#include <stdio.h>
#include <string.h>

#include "faces/shared/pm_face_draw.h"
#include "pin_config.h"
#include "pm_display.h"
#include "pm_faculty.h"
#include "pm_wifi_ntp.h"

static void snippet(const char *in, char *out, size_t cap, size_t max_chars) {
  if (!out || cap == 0) {
    return;
  }
  out[0] = '\0';
  if (!in || in[0] == '\0') {
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

static void initials_for_name(const char *name, char *out, size_t cap) {
  if (!out || cap == 0) {
    return;
  }
  out[0] = '\0';
  if (!name || name[0] == '\0') {
    strncpy(out, "?", cap - 1);
    out[cap - 1] = '\0';
    return;
  }
  size_t o = 0;
  bool word_start = true;
  for (const char *p = name; *p && o + 1 < cap; ++p) {
    if (*p == ' ' || *p == '-' || *p == '_') {
      word_start = true;
      continue;
    }
    if (word_start) {
      out[o++] = *p;
      word_start = false;
    }
  }
  if (o == 0) {
    out[o++] = name[0];
  }
  out[o] = '\0';
}

static void draw_arc_lines(int cx, int cy, int rx, int ry, int deg0, int deg1, uint16_t col) {
  int prev_x = 0;
  int prev_y = 0;
  bool have = false;
  for (int deg = deg0; deg <= deg1; deg += 4) {
    const float a = static_cast<float>(deg) * pm_face_k_pi / 180.f;
    const int x = cx + static_cast<int>(lrintf(cosf(a) * static_cast<float>(rx)));
    const int y = cy + static_cast<int>(lrintf(sinf(a) * static_cast<float>(ry)));
    if (have) {
      pm_gfx->drawLine(prev_x, prev_y, x, y, col);
    }
    prev_x = x;
    prev_y = y;
    have = true;
  }
}

static void draw_bust_placeholder(const PmFacultyProfile &faculty) {
  const int cx = LCD_WIDTH / 2;
  const int cy = 171;
  const uint16_t c_outer = pm_gfx->color565(132, 108, 190);
  const uint16_t c_mid = pm_gfx->color565(42, 36, 70);
  const uint16_t c_inner = pm_gfx->color565(18, 18, 34);
  const uint16_t c_line = pm_gfx->color565(210, 196, 255);
  pm_gfx->fillCircle(cx, cy, 84, c_outer);
  pm_gfx->fillCircle(cx, cy, 80, c_mid);
  pm_gfx->fillCircle(cx, cy, 72, c_inner);

  pm_gfx->drawCircle(cx, cy - 12, 31, c_line);
  pm_gfx->drawCircle(cx, cy - 12, 32, c_line);
  draw_arc_lines(cx, cy + 68, 63, 38, 205, 335, c_line);
  draw_arc_lines(cx, cy + 69, 64, 39, 205, 335, c_line);

  char initials[8];
  initials_for_name(faculty.name, initials, sizeof(initials));
  pm_face_draw_centered_line(initials, cy - 26, pm_gfx->color565(245, 240, 255), 3, 3);

  char line[64];
  const bool cached = pm_faculty_bust_size() > 0 && strcmp(pm_faculty_bust_slug(), faculty.slug) == 0;
  if (cached) {
    snprintf(line, sizeof(line), "bust cached %u KB",
             static_cast<unsigned>((pm_faculty_bust_size() + 1023u) / 1024u));
  } else if (pm_faculty_bust_status() == PmFacultyBustStatus::Working) {
    snprintf(line, sizeof(line), "fetching portrait");
  } else if (!pm_wifi_connected()) {
    snprintf(line, sizeof(line), "portrait needs WiFi");
  } else {
    snprintf(line, sizeof(line), "portrait pending");
  }
  pm_face_draw_centered_line(line, cy + 96, pm_gfx->color565(150, 158, 190), 1, 1);
}

void pm_face_faculty_draw(void) {
  pm_faculty_ensure_seed();
  PmFacultyProfile faculty = {};
  const uint16_t c_hi = pm_gfx->color565(235, 226, 255);
  const uint16_t c_dim = pm_gfx->color565(142, 150, 178);
  const uint16_t c_panel = pm_gfx->color565(10, 12, 24);

  pm_gfx->fillRect(0, 0, LCD_WIDTH, 48, c_panel);
  pm_face_draw_centered_line("FACULTY", 9, c_hi, 2, 2);

  if (!pm_faculty_active(&faculty)) {
    pm_face_draw_centered_line("No faculty saved", 190, c_hi, 2, 2);
    pm_face_draw_centered_line("serial: faculty use einstein", 224, c_dim, 1, 1);
    return;
  }

  pm_face_draw_centered_line(faculty.name, 34, c_dim, 1, 1);
  draw_bust_placeholder(faculty);

  pm_gfx->fillRect(0, LCD_HEIGHT - 104, LCD_WIDTH, 104, c_panel);
  char line[92];
  if (faculty.last_user[0] != '\0') {
    char q[64];
    snippet(faculty.last_user, q, sizeof(q), 54);
    snprintf(line, sizeof(line), "you: %s", q);
    pm_face_draw_centered_line(line, LCD_HEIGHT - 91, pm_gfx->color565(210, 222, 255), 1, 1);
  } else {
    pm_face_draw_centered_line("PWR hold to ask aloud", LCD_HEIGHT - 91, pm_gfx->color565(210, 222, 255), 1, 1);
  }
  if (faculty.last_reply[0] != '\0') {
    char a[64];
    snippet(faculty.last_reply, a, sizeof(a), 54);
    snprintf(line, sizeof(line), "%s: %s", faculty.name, a);
    pm_face_draw_centered_line(line, LCD_HEIGHT - 68, c_hi, 1, 1);
  } else {
    pm_face_draw_centered_line("router chooses a named faculty", LCD_HEIGHT - 68, c_hi, 1, 1);
  }
  snprintf(line, sizeof(line), "up/down recent  slug: %s", faculty.slug);
  pm_face_draw_centered_line(line, LCD_HEIGHT - 43, c_dim, 1, 1);
  pm_face_draw_centered_line("voice-pipeline -> ask-faculty + commonplace", LCD_HEIGHT - 22, c_dim, 1, 1);
}

void pm_face_faculty_draw_voice_screen(const char *status, bool speaking, float thinking_progress) {
  pm_face_faculty_draw();
  if (thinking_progress >= 0.f) {
    pm_face_draw_thinking_progress_ring(thinking_progress);
  }
  pm_face_draw_voice_waves_overlay(speaking, millis());
  pm_gfx->fillRect(0, 0, LCD_WIDTH, 40, pm_gfx->color565(10, 12, 24));
  pm_face_draw_centered_line(status ? status : (speaking ? "speaking" : "thinking"), 12,
                             pm_gfx->color565(230, 218, 255), 1, 1);
  pm_gfx->flush();
}
