#include "pm_side_buttons.h"

#include <Arduino.h>
#include <Wire.h>

#include "pin_config.h"
#if !defined(ASTROLABE_PLATFORM_185B)
#include "XPowersLib.h"

static XPowersPMU s_pmu;
#endif
static bool s_pmu_ok = false;
static uint32_t s_last_pmu_scan = 0;
/** Latched from AXP2101 PEK negative/positive edge IRQs (true while user is holding PWR). */
static bool s_pek_pressed = false;
static uint8_t s_qa_inject_ev = 0;

bool pm_side_buttons_begin() {
  pinMode(MYNAH_BOOT_BUTTON_GPIO, INPUT_PULLUP);

#if defined(ASTROLABE_PLATFORM_185B)
  s_pmu_ok = false;
#else
  s_pmu_ok = s_pmu.begin(Wire, AXP2101_SLAVE_ADDRESS, IIC_SDA, IIC_SCL);
  if (s_pmu_ok) {
    s_pmu.enableBattDetection();
    s_pmu.enableVbusVoltageMeasure();
    s_pmu.enableBattVoltageMeasure();
    s_pmu.enableSystemVoltageMeasure();
    s_pmu.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
    s_pmu.clearIrqStatus();
    s_pmu.enableIRQ(XPOWERS_AXP2101_PKEY_SHORT_IRQ | XPOWERS_AXP2101_PKEY_NEGATIVE_IRQ |
                     XPOWERS_AXP2101_PKEY_POSITIVE_IRQ);
  }
#endif
  return true;
}

void pm_side_buttons_inject(uint8_t ev_mask) { s_qa_inject_ev |= ev_mask; }

void pm_side_buttons_inject_pek_hold(bool held) { s_pek_pressed = held; }

uint8_t pm_side_buttons_poll(uint32_t now_ms) {
  uint8_t ev = s_qa_inject_ev;
  s_qa_inject_ev = 0;

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

#if !defined(ASTROLABE_PLATFORM_185B)
  if (s_pmu_ok && (now_ms - s_last_pmu_scan >= 35)) {
    s_last_pmu_scan = now_ms;
    (void)s_pmu.getIrqStatus();
    bool pek_irq = false;
    if (s_pmu.isPekeyNegativeIrq()) {
      s_pek_pressed = true;
      pek_irq = true;
    }
    if (s_pmu.isPekeyPositiveIrq()) {
      s_pek_pressed = false;
      pek_irq = true;
    }
    if (s_pmu.isPekeyShortPressIrq()) {
      ev |= PM_SIDE_BTN_PWR;
      pek_irq = true;
    }
    if (pek_irq) {
      s_pmu.clearIrqStatus();
    }
  }
#else
  (void)s_last_pmu_scan;
#endif

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
  if (!s_pmu_ok) {
    return false;
  }
  return s_pek_pressed;
}

bool pm_pmu_status(PmPmuStatus *out) {
  if (!out) {
    return false;
  }
  *out = {};
  out->present = s_pmu_ok;
  if (!s_pmu_ok) {
    out->battery_percent = -1;
    return false;
  }
#if defined(ASTROLABE_PLATFORM_185B)
  return false;
#else
  out->battery_present = s_pmu.isBatteryConnect();
  out->vbus_in = s_pmu.isVbusIn();
  out->charging = s_pmu.isCharging();
  out->discharging = s_pmu.isDischarge();
  out->battery_percent = s_pmu.getBatteryPercent();
  out->battery_mv = s_pmu.getBattVoltage();
  out->vbus_mv = s_pmu.getVbusVoltage();
  out->system_mv = s_pmu.getSystemVoltage();
  return true;
#endif
}
