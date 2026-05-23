#include "faces/settings/pm_face_settings_battery.h"

#include <cstdio>

#include "faces/shared/pm_face_draw.h"
#include "pm_battery_stats.h"
#include "pm_display.h"
#include "pm_side_buttons.h"

namespace {

void draw_battery_outline(int x, int y, int w, int h, int pct, uint16_t outline, uint16_t fill, uint16_t dim) {
  pm_gfx->drawRoundRect(x, y, w, h, 6, outline);
  pm_gfx->fillRoundRect(x + w, y + h / 3, 8, h / 3, 2, outline);
  const int inner_w = w - 10;
  const int fill_w = pct < 0 ? 0 : (inner_w * pct) / 100;
  pm_gfx->fillRoundRect(x + 5, y + 5, inner_w, h - 10, 4, dim);
  if (fill_w > 0) {
    pm_gfx->fillRoundRect(x + 5, y + 5, fill_w, h - 10, 4, fill);
  }
}

const char *battery_state_label(const PmPmuStatus &st) {
  if (!st.battery_present) {
    return "battery not detected";
  }
  if (st.charging) {
    return "charging";
  }
  if (st.discharging) {
    return "on battery";
  }
  if (st.vbus_in) {
    return "USB powered";
  }
  return "idle";
}

}  // namespace

void pm_face_settings_battery_draw(void) {
  const uint16_t c_hi = pm_gfx->color565(210, 215, 235);
  const uint16_t c_dim = pm_gfx->color565(120, 128, 145);
  const uint16_t c_panel = pm_gfx->color565(24, 28, 38);
  const uint16_t c_ok = pm_gfx->color565(120, 230, 170);
  const uint16_t c_warn = pm_gfx->color565(255, 198, 104);
  const uint16_t c_bad = pm_gfx->color565(235, 120, 115);
  const uint16_t c_charge = pm_gfx->color565(135, 205, 255);

  PmPmuStatus st = {};
  const bool ok = pm_pmu_status(&st);

  pm_face_draw_centered_line("Battery", 56, c_hi, 2, 2);
  if (!ok) {
    pm_face_draw_centered_line("PMU unavailable", 154, c_bad, 2, 2);
    pm_face_draw_centered_line("AXP2101 did not initialize", 210, c_dim, 1, 1);
    return;
  }

  int pct = st.battery_percent;
  if (!st.battery_present || pct < 0) {
    pct = 0;
  } else if (pct > 100) {
    pct = 100;
  }

  const uint16_t level_col = st.charging ? c_charge : (pct <= 15 ? c_bad : (pct <= 35 ? c_warn : c_ok));
  draw_battery_outline(126, 116, 206, 78, pct, c_hi, level_col, c_panel);

  char line[56];
  if (st.battery_present && st.battery_percent >= 0) {
    snprintf(line, sizeof(line), "%d%%", st.battery_percent);
  } else {
    snprintf(line, sizeof(line), "--%%");
  }
  pm_face_draw_centered_line(line, 222, level_col, 3, 3);
  pm_face_draw_centered_line(battery_state_label(st), 274, st.charging ? c_charge : c_hi, 1, 2);

  const PmBatteryStats stats = pm_battery_stats_update(st, millis());
  if (stats.estimating) {
    snprintf(line, sizeof(line), "%.1f%%/h  %.1fh left", static_cast<double>(stats.drain_pct_per_hour),
             static_cast<double>(stats.hours_remaining));
    pm_face_draw_centered_line(line, 306, c_warn, 1, 1);
  } else if (stats.tracking) {
    snprintf(line, sizeof(line), "tracking %lus", static_cast<unsigned long>(stats.sample_seconds));
    pm_face_draw_centered_line(line, 306, c_dim, 1, 1);
  } else {
    pm_face_draw_centered_line(st.charging || st.vbus_in ? "life estimate paused on USB" : "life estimate pending",
                               306, c_dim, 1, 1);
  }

  snprintf(line, sizeof(line), "battery %u mV", static_cast<unsigned>(st.battery_mv));
  pm_face_draw_centered_line(line, 334, c_dim, 1, 1);
  snprintf(line, sizeof(line), "USB %s  %u mV", st.vbus_in ? "in" : "off", static_cast<unsigned>(st.vbus_mv));
  pm_face_draw_centered_line(line, 360, c_dim, 1, 1);
  snprintf(line, sizeof(line), "system %u mV", static_cast<unsigned>(st.system_mv));
  pm_face_draw_centered_line(line, 386, c_dim, 1, 1);
}
