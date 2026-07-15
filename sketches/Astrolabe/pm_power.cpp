#include "pm_power.h"

#include <Preferences.h>
#include <Arduino.h>
#include <esp_sleep.h>

#include "pin_config.h"

static constexpr const char *kNvsNs = "mynah";
static constexpr const char *kEnabledKey = "pwr_en";
static constexpr const char *kDimKey = "pwr_dim_s";
static constexpr const char *kSleepKey = "pwr_slp_s";

static const uint16_t kDimOptions[] = {15, 30, 60, 120, 300};
static const uint16_t kSleepOptions[] = {0, 60, 120, 300, 600, 900};

#if defined(ASTROLABE_PLATFORM_185B)
static constexpr uint8_t kDefaultActiveBrightness = 255;
static constexpr uint8_t kDefaultDimBrightness = 192;
#else
static constexpr uint8_t kDefaultActiveBrightness = 200;
static constexpr uint8_t kDefaultDimBrightness = 24;
#endif

static PmPowerSettings s_settings = {true, 60, 300, kDefaultActiveBrightness, kDefaultDimBrightness};
static PmPowerBrightnessFn s_brightness_fn = nullptr;
static uint32_t s_last_activity_ms = 0;
static bool s_dimmed = false;
static bool s_sleeping = false;

static uint16_t nearest_option(const uint16_t *opts, int count, uint16_t value) {
  uint16_t best = opts[0];
  uint16_t best_delta = value > best ? value - best : best - value;
  for (int i = 1; i < count; ++i) {
    const uint16_t delta = value > opts[i] ? value - opts[i] : opts[i] - value;
    if (delta < best_delta) {
      best = opts[i];
      best_delta = delta;
    }
  }
  return best;
}

static uint16_t cycle_option(const uint16_t *opts, int count, uint16_t value, int delta) {
  int idx = 0;
  for (int i = 0; i < count; ++i) {
    if (opts[i] == value) {
      idx = i;
      break;
    }
  }
  idx = (idx + delta) % count;
  if (idx < 0) {
    idx += count;
  }
  return opts[idx];
}

static void save_settings(void) {
  Preferences pref;
  if (!pref.begin(kNvsNs, false)) {
    return;
  }
  pref.putBool(kEnabledKey, s_settings.enabled);
  pref.putUShort(kDimKey, s_settings.dim_timeout_s);
  pref.putUShort(kSleepKey, s_settings.sleep_timeout_s);
  pref.end();
}

static void apply_brightness(uint8_t brightness) {
  if (s_brightness_fn) {
    s_brightness_fn(brightness);
  }
}

void pm_power_begin(PmPowerBrightnessFn brightness_fn) {
  s_brightness_fn = brightness_fn;
  Preferences pref;
  if (pref.begin(kNvsNs, false)) {
    s_settings.enabled = pref.getBool(kEnabledKey, s_settings.enabled);
    s_settings.dim_timeout_s = nearest_option(kDimOptions, static_cast<int>(sizeof(kDimOptions) / sizeof(kDimOptions[0])),
                                              pref.getUShort(kDimKey, s_settings.dim_timeout_s));
    s_settings.sleep_timeout_s =
        nearest_option(kSleepOptions, static_cast<int>(sizeof(kSleepOptions) / sizeof(kSleepOptions[0])),
                       pref.getUShort(kSleepKey, s_settings.sleep_timeout_s));
    pref.end();
  }
  s_last_activity_ms = 0;
  s_dimmed = false;
  s_sleeping = false;
  apply_brightness(s_settings.active_brightness);
}

void pm_power_note_activity(uint32_t now_ms) {
  s_last_activity_ms = now_ms;
  if (s_dimmed || s_sleeping) {
    s_dimmed = false;
    s_sleeping = false;
    apply_brightness(s_settings.active_brightness);
  }
}

bool pm_power_tick(uint32_t now_ms) {
  if (s_last_activity_ms == 0) {
    s_last_activity_ms = now_ms;
  }
  if (!s_settings.enabled) {
    if (s_dimmed || s_sleeping) {
      s_dimmed = false;
      s_sleeping = false;
      apply_brightness(s_settings.active_brightness);
      return true;
    }
    return false;
  }
  const uint32_t idle_s = (now_ms - s_last_activity_ms) / 1000u;
  const bool should_sleep = s_settings.sleep_timeout_s > 0 && idle_s >= s_settings.sleep_timeout_s;
  const bool should_dim = !should_sleep && idle_s >= s_settings.dim_timeout_s;
  if (should_sleep != s_sleeping || should_dim != s_dimmed) {
    s_sleeping = should_sleep;
    s_dimmed = should_dim;
    apply_brightness(s_sleeping ? 0 : (s_dimmed ? s_settings.dim_brightness : s_settings.active_brightness));
    return true;
  }
  return false;
}

PmPowerState pm_power_state(uint32_t now_ms) {
  PmPowerState st = {};
  st.settings = s_settings;
  st.idle_ms = s_last_activity_ms == 0 ? 0 : now_ms - s_last_activity_ms;
  st.dimmed = s_dimmed;
  st.sleeping = s_sleeping;
  return st;
}

void pm_power_cycle_dim_timeout(int delta) {
  s_settings.dim_timeout_s =
      cycle_option(kDimOptions, static_cast<int>(sizeof(kDimOptions) / sizeof(kDimOptions[0])),
                   s_settings.dim_timeout_s, delta);
  if (s_settings.sleep_timeout_s > 0 && s_settings.dim_timeout_s >= s_settings.sleep_timeout_s) {
    s_settings.sleep_timeout_s =
        cycle_option(kSleepOptions, static_cast<int>(sizeof(kSleepOptions) / sizeof(kSleepOptions[0])),
                     s_settings.sleep_timeout_s, 1);
  }
  save_settings();
}

void pm_power_cycle_sleep_timeout(int delta) {
  s_settings.sleep_timeout_s =
      cycle_option(kSleepOptions, static_cast<int>(sizeof(kSleepOptions) / sizeof(kSleepOptions[0])),
                   s_settings.sleep_timeout_s, delta);
  save_settings();
}

void pm_power_toggle_enabled(void) {
  s_settings.enabled = !s_settings.enabled;
  if (!s_settings.enabled) {
    s_dimmed = false;
    s_sleeping = false;
    apply_brightness(s_settings.active_brightness);
  }
  save_settings();
}

bool pm_power_should_deep_sleep(uint32_t now_ms) {
  const PmPowerState st = pm_power_state(now_ms);
  return st.settings.enabled && st.sleeping;
}

void pm_power_enter_deep_sleep(uint64_t sleep_us) {
  apply_brightness(0);
  if (sleep_us > 0) {
    esp_sleep_enable_timer_wakeup(sleep_us);
  }
#if !defined(ASTROLABE_QEMU)
  esp_sleep_enable_ext0_wakeup(static_cast<gpio_num_t>(MYNAH_BOOT_BUTTON_GPIO), 0);
#endif
  Serial.flush();
  delay(50);
  esp_deep_sleep_start();
}
