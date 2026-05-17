#include "pm_circadian_hue.h"

#include <cmath>
#include <cstddef>

namespace {

struct CircadianHueStop {
  float second;
  float hue;
};

static constexpr float kSecondsPerDay = 86400.0f;
static constexpr CircadianHueStop kCircadianHueStops[] = {
    {0.0f, 250.0f},
    {6.0f * 3600.0f, 30.0f},
    {12.0f * 3600.0f, 120.0f},
    {18.0f * 3600.0f, 30.0f},
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

