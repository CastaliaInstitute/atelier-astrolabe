#pragma once

#include <stdint.h>

#define PM_SIDE_BTN_BOOT 1u
#define PM_SIDE_BTN_PWR 2u

struct PmPmuStatus {
  bool present;
  bool battery_present;
  bool vbus_in;
  bool charging;
  bool discharging;
  int battery_percent;
  uint16_t battery_mv;
  uint16_t vbus_mv;
  uint16_t system_mv;
};

/** After [Wire.begin], before or after touch init. Initializes BOOT GPIO + AXP2101 PWR key IRQ polling. */
bool pm_side_buttons_begin();

/**
 * Poll physical side buttons. Returns a bitmask of PM_SIDE_BTN_* for new presses since last call
 * (debounced). PWR uses AXP2101 short-press IRQ over I2C when PMU is present.
 */
uint8_t pm_side_buttons_poll(uint32_t now_ms);

/** True while the AXP2101 PWR key is held (PEK latched). Hold-to-talk on the astrology face. */
bool pm_ptt_button_held(void);

/** Latest AXP2101 battery/power snapshot; returns false when PMU init failed. */
bool pm_pmu_status(PmPmuStatus *out);

/** Inject PM_SIDE_BTN_* events (merged on next poll; for CI / qa inject). */
void pm_side_buttons_inject(uint8_t ev_mask);

/** Simulate PWR key hold state (pm_ptt_button_held). */
void pm_side_buttons_inject_pek_hold(bool held);
