#include "pm_battery_stats.h"

static bool s_have_anchor = false;
static int s_anchor_pct = -1;
static uint32_t s_anchor_ms = 0;
static float s_drain_pct_per_hour = 0.f;

PmBatteryStats pm_battery_stats_update(const PmPmuStatus &status, uint32_t now_ms) {
  PmBatteryStats out = {};
  if (!status.present || !status.battery_present || status.battery_percent < 0 || status.charging ||
      status.vbus_in) {
    s_have_anchor = false;
    return out;
  }

  out.tracking = true;
  if (!s_have_anchor || status.battery_percent > s_anchor_pct) {
    s_have_anchor = true;
    s_anchor_pct = status.battery_percent;
    s_anchor_ms = now_ms;
    return out;
  }

  const uint32_t elapsed_ms = now_ms - s_anchor_ms;
  out.sample_seconds = elapsed_ms / 1000u;
  const int drop = s_anchor_pct - status.battery_percent;
  if (drop <= 0 || elapsed_ms < 5u * 60u * 1000u) {
    return out;
  }

  const float hours = static_cast<float>(elapsed_ms) / 3600000.f;
  const float rate = static_cast<float>(drop) / hours;
  if (rate > 0.01f && rate < 100.f) {
    s_drain_pct_per_hour = s_drain_pct_per_hour <= 0.f ? rate : (s_drain_pct_per_hour * 0.72f + rate * 0.28f);
    out.estimating = true;
    out.drain_pct_per_hour = s_drain_pct_per_hour;
    out.hours_remaining = static_cast<float>(status.battery_percent) / s_drain_pct_per_hour;
  }
  return out;
}
