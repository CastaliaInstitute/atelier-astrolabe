#pragma once

#include <stdint.h>

#define PM_SIDE_BTN_BOOT 1u
#define PM_SIDE_BTN_PWR 2u

/** After [Wire.begin], before or after touch init. Initializes BOOT GPIO + AXP2101 PWR key IRQ polling. */
bool pm_side_buttons_begin();

/**
 * Poll physical side buttons. Returns a bitmask of PM_SIDE_BTN_* for new presses since last call
 * (debounced). PWR uses AXP2101 short-press IRQ over I2C when PMU is present.
 */
uint8_t pm_side_buttons_poll(uint32_t now_ms);

/** True while the AXP2101 PWR key is held (PEK latched). Hold-to-talk on the astrology face. */
bool pm_ptt_button_held(void);
