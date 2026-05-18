#include "faces/shared/pm_circadian_hue.h"
#include <Arduino_GFX_Library.h>
#include "pm_geo_tz.h"
#include "pm_wifi_ntp.h"

static uint16_t rgb565_plain(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

namespace {

struct HueAnchor {
  float hour;
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

constexpr HueAnchor kAnchors[] = {
    {0.f, 0x1b, 0x12, 0x40},  {3.f, 0x14, 0x20, 0x50},  {6.f, 0xd9, 0x6f, 0x59},
    {9.f, 0xf2, 0xc1, 0x4e},  {12.f, 0xdf, 0xf0, 0xa8}, {15.f, 0x55, 0xc7, 0xd8},
    {18.f, 0x4b, 0x5b, 0xdc}, {21.f, 0x6d, 0x3f, 0xb5}, {24.f, 0x1b, 0x12, 0x40},
};

constexpr int kAnchorCount = static_cast<int>(sizeof(kAnchors) / sizeof(kAnchors[0]));

float wrap_hour(float h) {
  while (h >= 24.f) {
    h -= 24.f;
  }
  while (h < 0.f) {
    h += 24.f;
  }
  return h;
}

void interpolate_rgb(float hour, uint8_t *r, uint8_t *g, uint8_t *b) {
  hour = wrap_hour(hour);
  for (int i = 0; i < kAnchorCount - 1; ++i) {
    const float h0 = kAnchors[i].hour;
    const float h1 = kAnchors[i + 1].hour;
    if (hour >= h0 && hour < h1) {
      const float t = (hour - h0) / (h1 - h0);
      *r = static_cast<uint8_t>(kAnchors[i].r + t * static_cast<float>(kAnchors[i + 1].r - kAnchors[i].r));
      *g = static_cast<uint8_t>(kAnchors[i].g + t * static_cast<float>(kAnchors[i + 1].g - kAnchors[i].g));
      *b = static_cast<uint8_t>(kAnchors[i].b + t * static_cast<float>(kAnchors[i + 1].b - kAnchors[i].b));
      return;
    }
  }
  *r = kAnchors[kAnchorCount - 1].r;
  *g = kAnchors[kAnchorCount - 1].g;
  *b = kAnchors[kAnchorCount - 1].b;
}

float local_hour_from_unix(time_t unix_sec) {
  struct tm tm = {};
  const time_t local = unix_sec + static_cast<time_t>(pm_geo_tz_offset_sec());
  gmtime_r(&local, &tm);
  return static_cast<float>(tm.tm_hour) + static_cast<float>(tm.tm_min) / 60.f +
         static_cast<float>(tm.tm_sec) / 3600.f;
}

}  // namespace

uint16_t pm_circadian_color565_at_hour(float hour_local) {
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
  interpolate_rgb(hour_local, &r, &g, &b);
  return rgb565_plain(r, g, b);
}

uint16_t pm_circadian_color565_at_unix(time_t unix_sec) {
  return pm_circadian_color565_at_hour(local_hour_from_unix(unix_sec));
}

const char *pm_circadian_hue_name_at_hour(float hour_local) {
  hour_local = wrap_hour(hour_local);
  if (hour_local < 5.f) {
    return "Night";
  }
  if (hour_local < 7.f) {
    return "Dawn";
  }
  if (hour_local < 10.f) {
    return "Gold Hour";
  }
  if (hour_local < 13.f) {
    return "Midday";
  }
  if (hour_local < 16.f) {
    return "Cyan Hour";
  }
  if (hour_local < 19.f) {
    return "Indigo Hour";
  }
  if (hour_local < 21.f) {
    return "Violet Hour";
  }
  return "Late Night";
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
