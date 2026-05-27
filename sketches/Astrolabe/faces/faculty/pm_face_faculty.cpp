#include "faces/faculty/pm_face_faculty.h"

#include <Arduino_GFX_Library.h>

#include "pin_config.h"
#include "pm_display.h"
#include "pm_faculty.h"

void pm_face_faculty_draw(void) {
  pm_faculty_ensure_demo_seed();
  pm_gfx->fillScreen(pm_gfx->color565(6, 8, 16));
  pm_faculty_draw_bust_fullscreen();
}

void pm_face_faculty_draw_voice_screen(const char *status, bool speaking, float thinking_progress) {
  (void)status;
  (void)speaking;
  (void)thinking_progress;
  pm_face_faculty_draw();
  pm_gfx->flush();
}
