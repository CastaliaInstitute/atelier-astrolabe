#include "faces/digital/pm_face_digital.h"
#include "faces/shared/pm_face_draw.h"
#include <cstdio>
#include "pin_config.h"
#include "pm_display.h"

void pm_face_digital_draw(const struct tm *tm, bool valid) {
  char line1[16];
  if (valid) {
    snprintf(line1, sizeof(line1), "%02d:%02d", tm->tm_hour, tm->tm_min);
  } else {
    snprintf(line1, sizeof(line1), "--:--");
  }
  pm_face_draw_centered_line(line1, 210, RGB565_WHITE, 5, 5);
}


