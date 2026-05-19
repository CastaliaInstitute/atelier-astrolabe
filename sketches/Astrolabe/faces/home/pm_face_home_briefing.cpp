#include "faces/home/pm_face_home_briefing.h"

#include "faces/shared/pm_face_draw.h"

void pm_face_home_briefing_draw_thinking(float progress) {
  pm_face_draw_home_briefing_screen(false, millis(), progress, "Preparing…");
}

void pm_face_home_briefing_draw_speaking(uint32_t now_ms) {
  pm_face_draw_home_briefing_screen(true, now_ms, -1.f, nullptr);
}
