#include "faces/wand/pm_face_wand.h"

#include "faces/faculty/pm_face_faculty.h"
#include "pm_display.h"

void pm_face_wand_draw(void) {
  pm_face_faculty_draw();
}

void pm_face_wand_draw_voice_screen(const char *status, bool speaking, float thinking_progress) {
  pm_face_faculty_draw_voice_screen(status, speaking, thinking_progress);
  if (status != NULL && status[0] != '\0' && pm_gfx != nullptr) {
    pm_gfx->setTextColor(pm_gfx->color565(160, 220, 255));
    pm_gfx->setTextSize(1);
    pm_gfx->setCursor(18, 430);
    pm_gfx->print("wand");
  }
  pm_gfx->flush();
}
