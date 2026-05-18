#include "faces/faculty/pm_face_faculty.h"

#include <Arduino_GFX_Library.h>
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
  pm_faculty_draw_bust();

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
  if (pm_faculty_bust_status() == PmFacultyBustStatus::Working) {
    pm_face_draw_centered_line("fetching portrait", LCD_HEIGHT - 22, c_dim, 1, 1);
  } else if (pm_faculty_bust_size() == 0 && !pm_wifi_connected()) {
    pm_face_draw_centered_line("portrait needs WiFi", LCD_HEIGHT - 22, c_dim, 1, 1);
  } else {
    pm_face_draw_centered_line("voice-pipeline -> ask-faculty", LCD_HEIGHT - 22, c_dim, 1, 1);
  }
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
