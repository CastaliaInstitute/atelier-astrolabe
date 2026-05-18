#include "pm_stars.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_log.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pm_config.h"
#include "pm_tls.h"

static const char *TAG = "pm_stars";

#ifndef MYNAH_STARS_CATALOG_URL
#define MYNAH_STARS_CATALOG_URL "https://ephemeris.castalia.institute/data/stars/bright-stars.json"
#endif

#ifndef MYNAH_STARS_CATALOG_MAX_BYTES
#define MYNAH_STARS_CATALOG_MAX_BYTES (16384)
#endif

typedef struct {
  char id[12];
  float ra;
  float dec;
  float mag;
} PmStarCatalogEntry;

static PmStarCatalogEntry s_catalog[PM_STARS_MAX];
static int s_catalog_count = 0;
static bool s_catalog_ready = false;
static bool s_catalog_from_net = false;

static const PmStarCatalogEntry kEmbedded[] = {
    {"Sirius", 101.287f, -16.716f, -1.46f},     {"Canopus", 95.988f, -52.696f, -0.74f},
    {"Arcturus", 213.915f, 19.182f, -0.05f},    {"Vega", 279.235f, 38.784f, 0.03f},
    {"Capella", 79.172f, 45.998f, 0.08f},       {"Rigel", 78.634f, -8.202f, 0.13f},
    {"Procyon", 114.825f, 5.225f, 0.34f},       {"Betelgeuse", 88.793f, 7.407f, 0.42f},
    {"Achernar", 24.428f, -57.237f, 0.46f},     {"Hadar", 210.956f, -60.373f, 0.61f},
    {"Altair", 297.695f, 8.868f, 0.76f},        {"Acrux", 186.65f, -63.099f, 0.76f},
    {"Aldebaran", 68.98f, 16.509f, 0.85f},      {"Antares", 247.352f, -26.432f, 0.96f},
    {"Spica", 201.298f, -11.161f, 0.97f},       {"Pollux", 116.329f, 28.026f, 1.14f},
    {"Fomalhaut", 344.413f, -29.622f, 1.16f},   {"Deneb", 310.358f, 45.28f, 1.25f},
    {"Regulus", 152.093f, 11.967f, 1.35f},      {"Adhara", 104.656f, -28.972f, 1.5f},
    {"Castor", 113.649f, 31.888f, 1.57f},       {"Bellatrix", 81.283f, 6.35f, 1.64f},
    {"Elnath", 81.573f, 28.607f, 1.65f},        {"Miaplacidus", 138.3f, -69.717f, 1.67f},
    {"Alnilam", 84.053f, -1.202f, 1.69f},       {"Alnitak", 85.19f, -1.943f, 1.74f},
    {"Alnair", 331.446f, -46.961f, 1.73f},      {"Alioth", 193.507f, 55.96f, 1.76f},
    {"Mirfak", 51.081f, 49.861f, 1.79f},        {"Dubhe", 165.932f, 61.751f, 1.81f},
    {"Wezen", 111.024f, -26.393f, 1.83f},      {"Alkaid", 206.885f, 49.313f, 1.85f},
    {"Sargas", 264.395f, -42.998f, 1.86f},     {"Avior", 125.628f, -59.509f, 1.86f},
    {"Menkalinan", 89.882f, 44.947f, 1.9f},     {"Atria", 252.166f, -69.028f, 1.91f},
    {"Alhena", 99.428f, 16.399f, 1.93f},       {"Peacock", 306.412f, -56.735f, 1.94f},
    {"Mirzam", 95.675f, -17.956f, 1.98f},     {"Alphard", 141.897f, -8.659f, 1.99f},
    {"Polaris", 37.954f, 89.264f, 1.98f},       {"Hamal", 31.793f, 23.462f, 2.01f},
    {"Algieba", 154.993f, 19.842f, 2.08f},     {"Diphda", 10.897f, -17.987f, 2.04f},
    {"Mizar", 200.981f, 54.925f, 2.23f},      {"Schedar", 24.733f, 56.537f, 2.24f},
    {"Denebola", 177.265f, 14.572f, 2.14f},   {"Merak", 165.46f, 56.382f, 2.34f},
};

static constexpr int kEmbeddedCount =
    static_cast<int>(sizeof(kEmbedded) / sizeof(kEmbedded[0]));

static double deg2rad(double d) { return d * (M_PI / 180.0); }
static double rad2deg(double r) { return r * (180.0 / M_PI); }

static void load_embedded_catalog(void) {
  s_catalog_count = kEmbeddedCount < PM_STARS_MAX ? kEmbeddedCount : PM_STARS_MAX;
  for (int i = 0; i < s_catalog_count; ++i) {
    s_catalog[i] = kEmbedded[i];
  }
}

static bool parse_json_string_after(const char *p, char *out, size_t cap) {
  const char *q = strchr(p, '"');
  if (!q) {
    return false;
  }
  ++q;
  const char *end = strchr(q, '"');
  if (!end || static_cast<size_t>(end - q) >= cap) {
    return false;
  }
  memcpy(out, q, static_cast<size_t>(end - q));
  out[end - q] = '\0';
  return true;
}

static bool parse_float_field(const char *block, const char *key, float *out) {
  char needle[16];
  snprintf(needle, sizeof(needle), "\"%s\":", key);
  const char *p = strstr(block, needle);
  if (!p) {
    return false;
  }
  p += strlen(needle);
  char *end = nullptr;
  const double v = strtod(p, &end);
  if (end == p) {
    return false;
  }
  *out = static_cast<float>(v);
  return true;
}

static int parse_stars_json(const char *json) {
  const char *arr = strstr(json, "\"stars\"");
  if (!arr) {
    return 0;
  }
  arr = strchr(arr, '[');
  if (!arr) {
    return 0;
  }
  int n = 0;
  const char *p = arr;
  while (n < PM_STARS_MAX) {
    p = strstr(p, "\"id\"");
    if (!p) {
      break;
    }
    PmStarCatalogEntry *e = &s_catalog[n];
    if (!parse_json_string_after(strchr(p, ':'), e->id, sizeof(e->id))) {
      break;
    }
    if (!parse_float_field(p, "ra", &e->ra) || !parse_float_field(p, "dec", &e->dec) ||
        !parse_float_field(p, "mag", &e->mag)) {
      break;
    }
    ++n;
    p = strchr(p, '}');
    if (!p) {
      break;
    }
    ++p;
  }
  return n;
}

bool pm_stars_ensure_catalog(void) {
  if (s_catalog_ready) {
    return s_catalog_count > 0;
  }
  load_embedded_catalog();
  s_catalog_ready = true;

#if MYNAH_EPHEMERIS_ENABLE
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClientSecure client;
    pm_tls_configure_client(client);
    HTTPClient http;
    http.setTimeout(static_cast<uint16_t>(MYNAH_EPHEMERIS_HTTP_MS));
    if (http.begin(client, MYNAH_STARS_CATALOG_URL)) {
      const int code = http.GET();
      if (code == HTTP_CODE_OK) {
        const int len = http.getSize();
        if (len > 0 && len < static_cast<int>(MYNAH_STARS_CATALOG_MAX_BYTES)) {
          char *buf = static_cast<char *>(malloc(static_cast<size_t>(len) + 1u));
          if (buf) {
            WiFiClient *stream = http.getStreamPtr();
            const int rd =
                stream ? stream->readBytes(buf, len) : static_cast<int>(http.getSize());
            buf[rd] = '\0';
            const int pn = parse_stars_json(buf);
            if (pn > 0) {
              s_catalog_count = pn;
              s_catalog_from_net = true;
              ESP_LOGI(TAG, "catalog from net (%d stars)", pn);
            }
            free(buf);
          }
        }
      } else {
        ESP_LOGW(TAG, "catalog GET %d", code);
      }
      http.end();
    }
  }
#endif

  if (!s_catalog_from_net) {
    ESP_LOGI(TAG, "catalog embedded (%d stars)", s_catalog_count);
  }
  return s_catalog_count > 0;
}

static double gmst_deg(time_t epoch) {
  const double jd = 2440587.5 + static_cast<double>(epoch) / 86400.0;
  const double t = (jd - 2451545.0) / 36525.0;
  double g = 280.46061837 + 360.98564736629 * (jd - 2451545.0) + 0.000387933 * t * t;
  g = fmod(g, 360.0);
  if (g < 0) {
    g += 360.0;
  }
  return g;
}

static void equatorial_to_horizon(double ra_deg, double dec_deg, float lat_deg, float lon_deg,
                                  time_t epoch, float *az_out, float *alt_out) {
  const double lst = gmst_deg(epoch) + lon_deg;
  double ha = lst - ra_deg;
  ha = fmod(ha, 360.0);
  if (ha < -180.0) {
    ha += 360.0;
  }
  if (ha > 180.0) {
    ha -= 360.0;
  }
  const double ha_r = deg2rad(ha);
  const double lat_r = deg2rad(lat_deg);
  const double dec_r = deg2rad(dec_deg);
  const double sin_alt =
      sin(lat_r) * sin(dec_r) + cos(lat_r) * cos(dec_r) * cos(ha_r);
  const double alt = asin(sin_alt);
  const double cos_az_n = (sin(dec_r) - sin(lat_r) * sin_alt) / (cos(lat_r) * cos(alt) + 1e-12);
  double az = acos(cos_az_n);
  if (sin(ha_r) > 0) {
    az = 2.0 * M_PI - az;
  }
  *alt_out = static_cast<float>(rad2deg(alt));
  *az_out = static_cast<float>(rad2deg(az));
  if (*az_out < 0.f) {
    *az_out += 360.f;
  }
}

bool pm_stars_ecliptic_lon_to_horizon(double ecliptic_lon_deg, time_t epoch_utc, float lat_deg,
                                      float lon_deg, float *az_out, float *alt_out) {
  if (!az_out || !alt_out) {
    return false;
  }
  const double lon_r = deg2rad(ecliptic_lon_deg);
  const double eps_r = deg2rad(23.4392911);
  double ra = atan2(sin(lon_r) * cos(eps_r), cos(lon_r));
  ra = rad2deg(ra);
  if (ra < 0) {
    ra += 360.0;
  }
  const double dec = rad2deg(asin(sin(lon_r) * sin(eps_r)));
  equatorial_to_horizon(ra, dec, lat_deg, lon_deg, epoch_utc, az_out, alt_out);
  return true;
}

bool pm_stars_compute_horizon(time_t epoch_utc, float lat_deg, float lon_deg, PmStarField *out) {
  if (!out || epoch_utc < 0) {
    return false;
  }
  if (!pm_stars_ensure_catalog()) {
    return false;
  }
  memset(out, 0, sizeof(*out));
  for (int i = 0; i < s_catalog_count; ++i) {
    PmStarHorizon *s = &out->star[out->count];
    strncpy(s->id, s_catalog[i].id, sizeof(s->id) - 1);
    s->mag = s_catalog[i].mag;
    equatorial_to_horizon(s_catalog[i].ra, s_catalog[i].dec, lat_deg, lon_deg, epoch_utc, &s->az,
                          &s->alt);
    s->above_horizon = s->alt > 8.f;
    if (s->above_horizon) {
      ++out->count;
    }
  }
  out->ok = out->count > 0;
  return out->ok;
}
