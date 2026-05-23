#pragma once

#include <stdint.h>

struct PmPowerSettings {
  bool enabled;
  uint16_t dim_timeout_s;
  uint16_t sleep_timeout_s;
  uint8_t active_brightness;
  uint8_t dim_brightness;
};

struct PmPowerState {
  PmPowerSettings settings;
  uint32_t idle_ms;
  bool dimmed;
  bool sleeping;
};

typedef void (*PmPowerBrightnessFn)(uint8_t brightness);

void pm_power_begin(PmPowerBrightnessFn brightness_fn);
void pm_power_note_activity(uint32_t now_ms);
bool pm_power_tick(uint32_t now_ms);
PmPowerState pm_power_state(uint32_t now_ms);
void pm_power_cycle_dim_timeout(int delta);
void pm_power_cycle_sleep_timeout(int delta);
void pm_power_toggle_enabled(void);
