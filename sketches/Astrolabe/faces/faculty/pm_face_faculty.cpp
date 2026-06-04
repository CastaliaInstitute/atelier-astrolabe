#include "faces/faculty/pm_face_faculty.h"

#include <Arduino_GFX_Library.h>

#include "pin_config.h"
#include "pm_display.h"
#include "pm_faculty.h"

void pm_face_faculty_draw(void) {
  pm_faculty_ensure_demo_seed();
  PmFacultyProfile faculty = {};
  if (pm_faculty_active(&faculty) && !pm_faculty_bust_ready_for(faculty.slug) &&
      pm_faculty_bust_status() != PmFacultyBustStatus::Working) {
    (void)pm_faculty_request_bust(faculty.slug);
    (void)pm_faculty_tick_bust_fetch();
  }
  pm_gfx->fillScreen(RGB565_BLACK);
  pm_faculty_draw_bust_fullscreen();
  pm_faculty_draw_name_label();
}

void pm_face_faculty_draw_voice_screen(const char *status, bool speaking, float thinking_progress) {
  (void)status;
  (void)speaking;
  (void)thinking_progress;
  pm_face_faculty_draw();
  pm_gfx->flush();
}
