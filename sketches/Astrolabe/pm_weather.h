#pragma once

#include <stdint.h>

/** Precipitation for an hourly ring segment. */
enum class PmWeatherPrecip : uint8_t {
  None = 0,
  Rain,
  Snow,
};

struct PmWeatherHour {
  int8_t temp_c = 0;
  uint8_t humidity_pct = 0;
  PmWeatherPrecip precip = PmWeatherPrecip::None;
};

struct PmWeatherStatus {
  bool ok = false;
  /** True when using synthesized forecast (offline or API miss). */
  bool demo = false;
  int8_t current_temp_c = 0;
  int8_t hi_c = 0;
  int8_t lo_c = 0;
  char condition[48];
  char location[40];
  PmWeatherHour hourly[24];
  char error[64];
};

/** POST `weather-status` (Castalia Edge Function); falls back to demo hourly data. */
bool pm_weather_fetch(PmWeatherStatus *out);

/** Deterministic demo profile for QEMU / offline. */
void pm_weather_fill_demo(PmWeatherStatus *out, int local_hour);
