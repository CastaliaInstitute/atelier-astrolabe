#include "faces/cycle/pm_face_cycle.h"

#include <Arduino.h>
#include <cmath>
#include <cstdio>

#include <esp_task_wdt.h>

#include "faces/shared/pm_face_draw.h"
#include "pin_config.h"
#include "pm_cycle_nvs.h"
#include "pm_display.h"

namespace {

uint32_t s_confirm_until_ms = 0;
bool s_defer_full_ring = false;
uint32_t s_full_ring_at_ms = 0;

float cycle_angle_for_day(float day0, float cycle_len) {
  return (day0 / cycle_len) * pm_face_k_two_pi - pm_face_k_pi * 0.5f;
}

void draw_cycle_band(int cx, int cy, int r_inner, int r_outer, uint8_t cycle_len, float start_day0,
                     float day_count, uint16_t col, int half_w) {
  if (cycle_len == 0 || day_count <= 0.f) {
    return;
  }
  while (start_day0 < 0.f) {
    start_day0 += static_cast<float>(cycle_len);
  }
  while (start_day0 >= static_cast<float>(cycle_len)) {
    start_day0 -= static_cast<float>(cycle_len);
  }

  const float max_count = static_cast<float>(cycle_len);
  if (day_count > max_count) {
    day_count = max_count;
  }
  /** Cap slices — uncapped (~400/band) tripped WDT when repainting every second. */
  const int steps = static_cast<int>(ceilf(day_count * 3.f));
  const int n = steps < 4 ? 4 : (steps > 48 ? 48 : steps);
  for (int i = 0; i <= n; ++i) {
    float day = start_day0 + day_count * (static_cast<float>(i) / static_cast<float>(n));
    while (day >= static_cast<float>(cycle_len)) {
      day -= static_cast<float>(cycle_len);
    }
    pm_face_draw_radial_annulus_slice(cx, cy, cycle_angle_for_day(day, static_cast<float>(cycle_len)),
                                      r_inner, r_outer, col, half_w);
    if ((i & 15) == 0) {
      yield();
    }
  }
}

void draw_cycle_marker(int cx, int cy, int r_mid, float ang, bool pulse) {
  const float ux = cosf(ang);
  const float uy = sinf(ang);
  const int x = cx + static_cast<int>(lrintf(ux * static_cast<float>(r_mid)));
  const int y = cy + static_cast<int>(lrintf(uy * static_cast<float>(r_mid)));
  const int pulse_px = pulse ? (2 + static_cast<int>((millis() / 110u) % 3u)) : 0;
  const uint16_t c_marker = pm_gfx->color565(252, 248, 230);
  const uint16_t c_halo = pulse ? pm_gfx->color565(120, 210, 205) : pm_gfx->color565(70, 76, 92);
  pm_gfx->fillCircle(x, y, 11 + pulse_px, c_halo);
  pm_gfx->fillCircle(x, y, 6 + pulse_px / 2, c_marker);
  pm_gfx->drawLine(cx + static_cast<int>(lrintf(ux * static_cast<float>(r_mid + 12))),
                   cy + static_cast<int>(lrintf(uy * static_cast<float>(r_mid + 12))),
                   cx + static_cast<int>(lrintf(ux * static_cast<float>(r_mid + 25))),
                   cy + static_cast<int>(lrintf(uy * static_cast<float>(r_mid + 25))), c_marker);
}

}  // namespace

void pm_face_cycle_flash_confirm(uint32_t until_ms) { s_confirm_until_ms = until_ms; }

bool pm_face_cycle_confirm_active(uint32_t now_ms) {
  return s_confirm_until_ms != 0 && now_ms < s_confirm_until_ms;
}

void pm_face_cycle_on_period_logged(void) { s_defer_full_ring = true; }

void pm_face_cycle_schedule_full_ring(uint32_t at_ms) { s_full_ring_at_ms = at_ms; }

bool pm_face_cycle_take_full_ring_scheduled(uint32_t now_ms) {
  if (s_full_ring_at_ms == 0 || now_ms < s_full_ring_at_ms) {
    return false;
  }
  s_full_ring_at_ms = 0;
  return true;
}

void pm_face_cycle_draw(const struct tm *tm_local, bool valid_local) {
  const uint16_t c_dim = pm_gfx->color565(142, 150, 166);
  const uint16_t c_error = pm_gfx->color565(255, 155, 145);
  if (!valid_local || !tm_local) {
    pm_face_draw_centered_line("need time", 220, c_dim, 2, 2);
    return;
  }

  PmCycleProfile cycle = {};
  (void)pm_cycle_load(&cycle);

  const uint16_t year = static_cast<uint16_t>(tm_local->tm_year + 1900);
  const uint8_t month = static_cast<uint8_t>(tm_local->tm_mon + 1);
  const uint8_t day = static_cast<uint8_t>(tm_local->tm_mday);

  if (cycle.pregnancy_active) {
    if (!cycle.has_due_date) {
      pm_face_draw_centered_line("set due date", 220, c_dim, 2, 2);
      pm_face_draw_centered_line("serial: cycle", 260, c_dim, 1, 1);
      return;
    }
    const int32_t gest = pm_cycle_gestational_day(&cycle, year, month, day);
    const int32_t until = pm_cycle_days_until_due(&cycle, year, month, day);
    const int cx = LCD_WIDTH / 2;
    const int cy = LCD_HEIGHT / 2;
    const int R = min(LCD_WIDTH, LCD_HEIGHT) / 2;
    const int r_outer = R - 20;
    const int r_inner = r_outer - 32;
    const uint16_t c_track = pm_gfx->color565(40, 36, 52);
    const uint16_t c_prog = pm_gfx->color565(235, 175, 95);
    const int32_t gest_clamped = gest >= 0 ? gest : 0;
    int32_t week = (gest_clamped + 3) / 7;
    if (week < 0) {
      week = 0;
    }
    if (week > 40) {
      week = 40;
    }
    constexpr uint8_t kPregWeeks = 40;
    draw_cycle_band(cx, cy, r_inner, r_outer, kPregWeeks, 0.f, static_cast<float>(week), c_prog, 3);
    draw_cycle_band(cx, cy, r_inner, r_outer, kPregWeeks, static_cast<float>(week),
                    static_cast<float>(kPregWeeks - week), c_track, 2);
    char line[32];
    snprintf(line, sizeof(line), "week %ld", static_cast<long>(week));
    pm_face_draw_centered_line(line, cy - 24, pm_gfx->color565(250, 235, 210), 2, 2);
    if (until >= 0) {
      snprintf(line, sizeof(line), "due in %ld d", static_cast<long>(until));
    } else {
      snprintf(line, sizeof(line), "past due");
    }
    pm_face_draw_centered_line(line, cy + 12, c_dim, 1, 2);
    snprintf(line, sizeof(line), "%02u/%02u/%04u", cycle.due_month, cycle.due_day, cycle.due_year);
    pm_face_draw_centered_line(line, cy + 44, c_dim, 1, 1);
    pm_gfx->drawCircle(cx, cy, r_outer + 5, pm_gfx->color565(90, 70, 110));
    return;
  }

  if (!cycle.has_last_period) {
    s_defer_full_ring = false;
    pm_face_draw_centered_line("set cycle", 220, c_dim, 2, 2);
    pm_face_draw_centered_line("tap = day 1", 258, c_dim, 1, 1);
    return;
  }

  const int32_t day_idx = pm_cycle_day_index_for_date(&cycle, year, month, day);
  if (day_idx < 0) {
    pm_face_draw_centered_line("set cycle", 220, c_error, 2, 2);
    return;
  }

  const uint8_t cycle_len_preview =
      cycle.cycle_length_days ? cycle.cycle_length_days : PM_CYCLE_DEFAULT_LENGTH_DAYS;
  if (s_defer_full_ring) {
    s_defer_full_ring = false;
    const int cx = LCD_WIDTH / 2;
    const int cy = LCD_HEIGHT / 2;
    const int R = min(LCD_WIDTH, LCD_HEIGHT) / 2;
    const int r_mid = R - 36;
    const float today_ang = cycle_angle_for_day(0.f, static_cast<float>(cycle_len_preview));
    const bool confirm = pm_face_cycle_confirm_active(millis());
    draw_cycle_marker(cx, cy, r_mid, today_ang, confirm);
    pm_face_draw_centered_line("day 1 saved", cy - 16, pm_gfx->color565(245, 240, 255), 2, 2);
    char line[32];
    snprintf(line, sizeof(line), "day 1 of %u", cycle_len_preview);
    pm_face_draw_centered_line(line, cy + 20, pm_gfx->color565(142, 150, 166), 1, 2);
    return;
  }

  const int cx = LCD_WIDTH / 2;
  const int cy = LCD_HEIGHT / 2;
  const int R = min(LCD_WIDTH, LCD_HEIGHT) / 2;
  const int r_outer = R - 20;
  const int r_inner = r_outer - 32;
  const int r_mid = (r_inner + r_outer) / 2;
  const uint8_t cycle_len = cycle.cycle_length_days ? cycle.cycle_length_days : PM_CYCLE_DEFAULT_LENGTH_DAYS;
  const uint8_t period_len = cycle.period_length_days < cycle_len ? cycle.period_length_days
                                                                  : PM_CYCLE_DEFAULT_PERIOD_DAYS;
  int ov_day = static_cast<int>(cycle_len) - 14;
  if (ov_day < 1) {
    ov_day = 1;
  } else if (ov_day > static_cast<int>(cycle_len)) {
    ov_day = cycle_len;
  }

  const uint16_t c_track = pm_gfx->color565(33, 39, 55);
  const uint16_t c_luteal = pm_gfx->color565(105, 82, 148);
  const uint16_t c_fertile = pm_gfx->color565(44, 165, 140);
  const uint16_t c_period = pm_gfx->color565(205, 76, 118);
  const uint16_t c_ov = pm_gfx->color565(245, 195, 80);
  const uint16_t c_spoke = pm_gfx->color565(60, 68, 84);

  draw_cycle_band(cx, cy, r_inner, r_outer, cycle_len, 0.f, static_cast<float>(cycle_len), c_track, 2);
  esp_task_wdt_reset();
  draw_cycle_band(cx, cy, r_inner, r_outer, cycle_len, static_cast<float>(ov_day),
                  static_cast<float>(cycle_len - ov_day), c_luteal, 2);
  esp_task_wdt_reset();
  const int fertile_start = (ov_day > 4) ? (ov_day - 4) : 0;
  const float fertile_span =
      static_cast<float>(ov_day - fertile_start) < 7.f ? static_cast<float>(ov_day - fertile_start) : 7.f;
  if (fertile_span > 0.5f) {
    draw_cycle_band(cx, cy, r_inner, r_outer, cycle_len, static_cast<float>(fertile_start), fertile_span,
                    c_fertile, 2);
  }
  esp_task_wdt_reset();
  draw_cycle_band(cx, cy, r_inner, r_outer, cycle_len, 0.f, static_cast<float>(period_len), c_period, 2);
  esp_task_wdt_reset();

  for (uint8_t d = 0; d < cycle_len; ++d) {
    if (d % 7 != 0 && d != 0) {
      continue;
    }
    const float a = cycle_angle_for_day(static_cast<float>(d), static_cast<float>(cycle_len));
    const int t0 = d == 0 ? r_inner - 10 : r_inner - 5;
    const int t1 = r_inner - 1;
    pm_gfx->drawLine(cx + static_cast<int>(lrintf(cosf(a) * static_cast<float>(t0))),
                     cy + static_cast<int>(lrintf(sinf(a) * static_cast<float>(t0))),
                     cx + static_cast<int>(lrintf(cosf(a) * static_cast<float>(t1))),
                     cy + static_cast<int>(lrintf(sinf(a) * static_cast<float>(t1))), c_spoke);
  }

  const float start_ang = cycle_angle_for_day(0.f, static_cast<float>(cycle_len));
  const float ov_ang = cycle_angle_for_day(static_cast<float>(ov_day - 1), static_cast<float>(cycle_len));
  pm_face_draw_radial_annulus_slice(cx, cy, ov_ang, r_inner - 2, r_outer + 4, c_ov, 3);

  const uint32_t now_ms = millis();
  const bool confirm = s_confirm_until_ms != 0 && now_ms < s_confirm_until_ms;
  const float today_ang = cycle_angle_for_day(static_cast<float>(day_idx), static_cast<float>(cycle_len));
  draw_cycle_marker(cx, cy, r_mid, today_ang, confirm);

  pm_gfx->drawCircle(cx, cy, r_outer + 5, pm_gfx->color565(38, 45, 60));
  pm_gfx->drawCircle(cx, cy, r_inner - 8, pm_gfx->color565(30, 36, 50));
  if (confirm) {
    pm_gfx->drawCircle(cx, cy, r_outer + 9, pm_gfx->color565(90, 210, 190));
  }

  /** Day 1 / period start at 12:00 (top); cycle advances clockwise. */
  pm_face_draw_label_at_polar(cx, cy, r_outer + 20, start_ang, "day 1", c_period);
  pm_face_draw_label_at_polar(cx, cy, r_outer + 36, start_ang, "period", c_period);

  char line[40];
  snprintf(line, sizeof(line), "day %ld of %u", static_cast<long>(day_idx + 1), cycle_len);
  pm_face_draw_centered_line(line, cy + 4, pm_gfx->color565(245, 240, 255), 2, 2);
}
