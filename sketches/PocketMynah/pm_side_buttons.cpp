#include "pm_side_buttons.h"

#include <Arduino.h>
#include <Wire.h>

#include "pin_config.h"
#include "XPowersLib.h"

static XPowersPMU s_pmu;
static bool s_pmu_ok = false;
static uint32_t s_last_pmu_scan = 0;

bool pm_side_buttons_begin() {
  pinMode(MYNAH_BOOT_BUTTON_GPIO, INPUT_PULLUP);

  s_pmu_ok = s_pmu.begin(Wire, AXP2101_SLAVE_ADDRESS, IIC_SDA, IIC_SCL);
  if (s_pmu_ok) {
    s_pmu.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
    s_pmu.clearIrqStatus();
    s_pmu.enableIRQ(XPOWERS_AXP2101_PKEY_SHORT_IRQ);
  }
  return true;
}

uint8_t pm_side_buttons_poll(uint32_t now_ms) {
  uint8_t ev = 0;

  static bool s_boot_armed = true;
  static uint32_t s_boot_low_ms = 0;
  const bool boot_down = digitalRead(MYNAH_BOOT_BUTTON_GPIO) == LOW;
  if (boot_down) {
    if (s_boot_low_ms == 0) {
      s_boot_low_ms = now_ms;
    } else if (s_boot_armed && (now_ms - s_boot_low_ms >= 45)) {
      s_boot_armed = false;
      ev |= PM_SIDE_BTN_BOOT;
    }
  } else {
    s_boot_low_ms = 0;
    s_boot_armed = true;
  }

  if (s_pmu_ok && (now_ms - s_last_pmu_scan >= 35)) {
    s_last_pmu_scan = now_ms;
    (void)s_pmu.getIrqStatus();
    if (s_pmu.isPekeyShortPressIrq()) {
      ev |= PM_SIDE_BTN_PWR;
      s_pmu.clearIrqStatus();
    }
  }

  static uint32_t s_last_emit = 0;
  if (ev != 0 && (now_ms - s_last_emit < 350)) {
    return 0;
  }
  if (ev != 0) {
    s_last_emit = now_ms;
  }
  return ev;
}

bool pm_ptt_button_held(void) {
  return digitalRead(MYNAH_BOOT_BUTTON_GPIO) == LOW;
}
