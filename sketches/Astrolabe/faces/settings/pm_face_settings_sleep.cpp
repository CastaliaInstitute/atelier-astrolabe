#include "faces/settings/pm_face_settings_sleep.h"

#include <cstdio>

#include "faces/shared/pm_face_draw.h"
#include "pm_display.h"
#include "pm_power.h"

static void fmt_timeout(char *buf, size_t cap, uint16_t seconds) {
  if (seconds == 0) {
    snprintf(buf, cap, "off");
  } else if (seconds < 60) {
    snprintf(buf, cap, "%us", static_cast<unsigned>(seconds));
  } else {
    snprintf(buf, cap, "%um", static_cast<unsigned>(seconds / 60u));
  }
}

void pm_face_settings_sleep_draw(void) {
  const uint16_t c_hi = pm_gfx->color565(220, 226, 236);
  const uint16_t c_dim = pm_gfx->color565(128, 138, 154);
  const uint16_t c_accent = pm_gfx->color565(135, 205, 255);
  const uint16_t c_warn = pm_gfx->color565(255, 198, 104);
  const PmPowerState st = pm_power_state(millis());

  char line[64];
  pm_face_draw_centered_line("Sleep", 56, c_hi, 2, 2);
  pm_face_draw_centered_line(st.settings.enabled ? "enabled" : "disabled", 114,
                             st.settings.enabled ? c_accent : c_warn, 2, 2);

  char dim_s[16];
  char sleep_s[16];
  fmt_timeout(dim_s, sizeof(dim_s), st.settings.dim_timeout_s);
  fmt_timeout(sleep_s, sizeof(sleep_s), st.settings.sleep_timeout_s);
  snprintf(line, sizeof(line), "dim after %s", dim_s);
  pm_face_draw_centered_line(line, 188, c_hi, 1, 2);
  snprintf(line, sizeof(line), "sleep after %s", sleep_s);
  pm_face_draw_centered_line(line, 224, c_hi, 1, 2);
  snprintf(line, sizeof(line), "idle %lus", static_cast<unsigned long>(st.idle_ms / 1000u));
  pm_face_draw_centered_line(line, 278, c_dim, 1, 1);
  pm_face_draw_centered_line(st.sleeping ? "display off" : (st.dimmed ? "dimmed" : "awake"), 308,
                             st.sleeping ? c_warn : c_accent, 1, 1);
  pm_face_draw_centered_line("tap sleep  long toggle", 360, c_dim, 1, 1);
  pm_face_draw_centered_line("two-finger dim", 384, c_dim, 1, 1);
}
