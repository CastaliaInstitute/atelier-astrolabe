#include "faces/shared/pm_circadian_hue.h"

#include <Arduino_GFX_Library.h>
#include <cmath>
#include <cstddef>

#include "faces/shared/pm_face_draw.h"
#include "pm_display.h"
#include "pm_geo_tz.h"
#include "pm_wifi_ntp.h"

namespace {

struct CircadianHueStop {
  float second;
  float hue;
};

static constexpr float kSecondsPerDay = 86400.0f;

static constexpr CircadianHueStop kCircadianHueStops[] = {
    {0.0f, 250.0f},
    {3.0f * 3600.0f, 270.0f},
    {5.0f * 3600.0f, 310.0f},
    {6.0f * 3600.0f, 340.0f},
    {8.0f * 3600.0f, 40.0f},
    {10.0f * 3600.0f, 70.0f},
    {12.0f * 3600.0f, 120.0f},
    {15.0f * 3600.0f, 185.0f},
    {17.0f * 3600.0f, 215.0f},
    {19.0f * 3600.0f, 245.0f},
    {21.0f * 3600.0f, 270.0f},
    {kSecondsPerDay, 250.0f},
};

float wrap_hue_degrees(float hue) {
  hue = fmodf(hue, 360.0f);
  if (hue < 0.0f) {
    hue += 360.0f;
  }
  return hue;
}

float wrap_seconds_of_day(float seconds) {
  seconds = fmodf(seconds, kSecondsPerDay);
  if (seconds < 0.0f) {
    seconds += kSecondsPerDay;
  }
  return seconds;
}

float shortest_hue_delta(float from, float to) {
  float delta = wrap_hue_degrees(to) - wrap_hue_degrees(from);
  if (delta > 180.0f) {
    delta -= 360.0f;
  } else if (delta < -180.0f) {
    delta += 360.0f;
  }
  return delta;
}

float wrap_hour(float h) {
  while (h >= 24.f) {
    h -= 24.f;
  }
  while (h < 0.f) {
    h += 24.f;
  }
  return h;
}

float local_hour_from_unix(time_t unix_sec) {
  struct tm tm = {};
  const time_t local = unix_sec + static_cast<time_t>(pm_geo_tz_offset_sec());
  gmtime_r(&local, &tm);
  return static_cast<float>(tm.tm_hour) + static_cast<float>(tm.tm_min) / 60.f +
         static_cast<float>(tm.tm_sec) / 3600.f;
}

}  // namespace

float pm_circadian_hue_from_seconds(float seconds_of_day) {
  const float sec = wrap_seconds_of_day(seconds_of_day);
  const size_t n = sizeof(kCircadianHueStops) / sizeof(kCircadianHueStops[0]);
  for (size_t i = 0; i + 1 < n; ++i) {
    const CircadianHueStop &a = kCircadianHueStops[i];
    const CircadianHueStop &b = kCircadianHueStops[i + 1];
    if (sec >= a.second && sec <= b.second) {
      const float span = b.second - a.second;
      const float t = span > 0.0f ? (sec - a.second) / span : 0.0f;
      return wrap_hue_degrees(a.hue + shortest_hue_delta(a.hue, b.hue) * t);
    }
  }
  return kCircadianHueStops[0].hue;
}

float pm_circadian_hue_from_hour(float hour_local) {
  return pm_circadian_hue_from_seconds(wrap_hour(hour_local) * 3600.0f);
}

uint16_t pm_circadian_color565_at_hour(float hour_local) {
  const float hue = pm_circadian_hue_from_hour(hour_local);
  return pm_face_color565_from_hsl(pm_gfx, hue, pm_face_hsl_bg_s, pm_face_hsl_bg_l);
}

uint16_t pm_circadian_color565_at_unix(time_t unix_sec) {
  return pm_circadian_color565_at_hour(local_hour_from_unix(unix_sec));
}

uint16_t pm_circadian_accent565_at_hour(float hour_local) {
  const float hue = pm_circadian_hue_from_hour(hour_local);
  return pm_face_color565_from_hsl(pm_gfx, hue, pm_face_hsl_accent_s, pm_face_hsl_accent_l);
}

const char *pm_circadian_hue_name_at_hour(float hour_local) {
  hour_local = wrap_hour(hour_local);
  if (hour_local < 5.f) {
    return "Nocturne";
  }
  if (hour_local < 8.f) {
    return "Aurora";
  }
  if (hour_local < 12.f) {
    return "Solar";
  }
  if (hour_local < 15.f) {
    return "Meridian";
  }
  if (hour_local < 18.f) {
    return "Zephyr";
  }
  if (hour_local < 21.f) {
    return "Vesper";
  }
  return "Oracle";
}

const char *pm_circadian_hue_name_now(void) {
  if (!pm_time_valid()) {
    return "Hue";
  }
  struct tm tm = {};
  pm_time_local(&tm);
  const float h = static_cast<float>(tm.tm_hour) + static_cast<float>(tm.tm_min) / 60.f;
  return pm_circadian_hue_name_at_hour(h);
}
