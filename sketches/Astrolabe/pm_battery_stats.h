#pragma once

#include <stdint.h>

#include "pm_side_buttons.h"

struct PmBatteryStats {
  bool tracking;
  bool estimating;
  float drain_pct_per_hour;
  float hours_remaining;
  uint32_t sample_seconds;
};

PmBatteryStats pm_battery_stats_update(const PmPmuStatus &status, uint32_t now_ms);
