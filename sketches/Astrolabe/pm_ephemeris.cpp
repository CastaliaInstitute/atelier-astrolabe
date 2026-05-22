#include "pm_ephemeris.h"

#include <WiFi.h>
#include <esp_log.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "pm_config.h"
#include "pm_heap.h"
#include "pm_http.h"

static const char *TAG = "pm_ephem";

static const char *const kBodyIds[kPmBodyCount] = {"sun", "moon", "mercury", "venus", "mars",
                                                   "jupiter", "saturn"};

static PmTransitPositions s_cache = {};
static time_t s_cache_epoch_min = -1;
static bool s_last_from_network = false;
static uint32_t s_fail_backoff_until_ms = 0;

static char s_month_key[8] = "";
static char *s_month_json = nullptr;
static size_t s_month_json_cap = 0;

bool pm_ephemeris_last_from_network(void) {
  const bool v = s_last_from_network;
  s_last_from_network = false;
  return v;
}

void pm_ephemeris_release_cache(void) {
  if (s_month_json) {
    free(s_month_json);
    s_month_json = nullptr;
  }
  s_month_json_cap = 0;
  s_month_key[0] = '\0';
  s_cache = {};
  s_cache_epoch_min = -1;
  s_last_from_network = false;
}

static double norm360(double lon) {
  lon = fmod(lon, 360.0);
  if (lon < 0.0) {
    lon += 360.0;
  }
  return lon;
}

static time_t utc_tm_to_epoch(const struct tm *utc) {
  struct tm t = *utc;
  t.tm_isdst = 0;
  const char *prev = getenv("TZ");
  char saved[48] = {};
  if (prev) {
    strncpy(saved, prev, sizeof(saved) - 1);
  }
  setenv("TZ", "UTC0", 1);
  tzset();
  const time_t e = mktime(&t);
  if (prev) {
    setenv("TZ", saved, 1);
  } else {
    unsetenv("TZ");
  }
  tzset();
  return e;
}

static void month_key_for_epoch(time_t epoch, char *out, size_t cap) {
  struct tm u = {};
  gmtime_r(&epoch, &u);
  snprintf(out, cap, "%04d-%02d", u.tm_year + 1900, u.tm_mon + 1);
}

static bool parse_int_field(const char *json, const char *key, int *out) {
  char needle[24];
  snprintf(needle, sizeof(needle), "\"%s\":", key);
  const char *p = strstr(json, needle);
  if (!p) {
    return false;
  }
  p += strlen(needle);
  *out = static_cast<int>(strtol(p, nullptr, 10));
  return true;
}

static bool parse_time_field(const char *json, const char *key, time_t *out) {
  char needle[24];
  snprintf(needle, sizeof(needle), "\"%s\":", key);
  const char *p = strstr(json, needle);
  if (!p) {
    return false;
  }
  p += strlen(needle);
  *out = static_cast<time_t>(strtoll(p, nullptr, 10));
  return true;
}

static bool nth_array_double(const char *json, const char *body, int idx, double *lon_out) {
  char needle[20];
  snprintf(needle, sizeof(needle), "\"%s\":[", body);
  const char *p = strstr(json, needle);
  if (!p) {
    return false;
  }
  p += strlen(needle);
  for (int i = 0; i < idx; ++i) {
    p = strchr(p, ',');
    if (!p) {
      return false;
    }
    ++p;
  }
  char *end = nullptr;
  const double v = strtod(p, &end);
  if (end == p) {
    return false;
  }
  *lon_out = norm360(v);
  return true;
}

static bool lookup_month_json(const char *json, time_t epoch, PmTransitPositions *out) {
  int step = 0;
  time_t t0 = 0;
  time_t t1 = 0;
  if (!parse_int_field(json, "step", &step) || step <= 0) {
    return false;
  }
  if (!parse_time_field(json, "t0", &t0) || !parse_time_field(json, "t1", &t1)) {
    return false;
  }
  if (epoch < t0 || epoch >= t1) {
    return false;
  }
  const int idx = static_cast<int>((epoch - t0) / step);
  for (int i = 0; i < kPmBodyCount; ++i) {
    if (!nth_array_double(json, kBodyIds[i], idx, &out->lon[i])) {
      return false;
    }
  }
  out->ok = true;
  return true;
}

static bool ensure_month_loaded(const char *month_key) {
  if (s_month_json && strcmp(s_month_key, month_key) == 0) {
    return true;
  }
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }
  if (!pm_heap_tls_ready(MYNAH_EPHEMERIS_MIN_FETCH_HEAP, "ephemeris")) {
    return false;
  }
  if (strlen(MYNAH_EPHEMERIS_DATA_BASE) == 0) {
    return false;
  }

  char url[192];
  snprintf(url, sizeof(url), "%s/%s.json", MYNAH_EPHEMERIS_DATA_BASE, month_key);

  if (!s_month_json || s_month_json_cap < MYNAH_EPHEMERIS_MONTH_MAX_BYTES + 1u) {
    if (s_month_json) {
      free(s_month_json);
      s_month_json = nullptr;
    }
    s_month_json_cap = MYNAH_EPHEMERIS_MONTH_MAX_BYTES + 1u;
    s_month_json = static_cast<char *>(pm_heap_alloc_response(s_month_json_cap));
  }
  if (!s_month_json) {
    return false;
  }
  PmHttpTextResult result = {};
  const bool ok = pm_http_request_text(url, "GET", nullptr, nullptr, 0, s_month_json, s_month_json_cap,
                                       MYNAH_EPHEMERIS_HTTP_MS, &result);
  if (!ok) {
    ESP_LOGW(TAG, "month GET %d %s (%u bytes)", result.status_code, month_key,
             static_cast<unsigned>(result.bytes_read));
    return false;
  }
  strncpy(s_month_key, month_key, sizeof(s_month_key) - 1);
  s_month_key[sizeof(s_month_key) - 1] = '\0';
  ESP_LOGI(TAG, "loaded %s (%u bytes)", month_key, static_cast<unsigned>(result.bytes_read));
  return true;
}

bool pm_ephemeris_fetch_utc(const struct tm *utc, PmTransitPositions *out) {
  if (!utc || !out) {
    return false;
  }
  out->ok = false;
#if !MYNAH_EPHEMERIS_ENABLE
  return false;
#endif
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }
  const uint32_t now_ms = millis();
  if (s_fail_backoff_until_ms != 0 && now_ms < s_fail_backoff_until_ms) {
    return false;
  }

  const time_t epoch = utc_tm_to_epoch(utc);
  if (epoch < 0) {
    return false;
  }
  const time_t bucket = epoch / 60;
  if (s_cache.ok && s_cache_epoch_min == bucket) {
    *out = s_cache;
    return true;
  }

  char month[8];
  month_key_for_epoch(epoch, month, sizeof(month));
  if (!ensure_month_loaded(month)) {
    s_fail_backoff_until_ms = now_ms + 30000u;
    return false;
  }
  s_fail_backoff_until_ms = 0;

  PmTransitPositions parsed = {};
  if (!lookup_month_json(s_month_json, epoch, &parsed)) {
    ESP_LOGW(TAG, "lookup failed %s", month);
    return false;
  }

  s_cache = parsed;
  s_cache_epoch_min = bucket;
  *out = parsed;
  s_last_from_network = true;
  return true;
}
