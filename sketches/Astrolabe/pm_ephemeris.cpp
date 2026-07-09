#include "pm_ephemeris.h"

#include <HTTPClient.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <esp_log.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "pm_config.h"
#include "pm_heap.h"

static const char *TAG = "pm_ephem";

static const char *const kBodyIds[kPmBodyCount] = {"sun", "moon", "mercury", "venus", "mars",
                                                   "jupiter", "saturn"};
static const char *const kHdBodyIds[kPmHdBodyCount] = {
    "sun",    "moon",    "mercury",  "venus",     "mars",      "jupiter",
    "saturn", "uranus",  "neptune",  "pluto",     "true_node", "mean_node",
};

static PmTransitPositions s_cache = {};
static time_t s_cache_epoch_min = -1;
static bool s_last_from_network = false;
static bool s_month_load_from_network = false;
static uint32_t s_fail_backoff_until_ms = 0;

static char s_month_key[8] = "";
static char *s_month_json = nullptr;
static size_t s_month_json_cap = 0;
static char s_hd_day_key[11] = "";
static char *s_hd_day_json = nullptr;
static size_t s_hd_day_json_cap = 0;
static bool s_fs_tried = false;
static bool s_fs_ready = false;

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
  if (s_hd_day_json) {
    free(s_hd_day_json);
    s_hd_day_json = nullptr;
  }
  s_month_json_cap = 0;
  s_hd_day_json_cap = 0;
  s_month_key[0] = '\0';
  s_hd_day_key[0] = '\0';
  s_cache = {};
  s_cache_epoch_min = -1;
  s_last_from_network = false;
  s_month_load_from_network = false;
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

static void day_key_for_epoch(time_t epoch, char *out, size_t cap) {
  struct tm u = {};
  gmtime_r(&epoch, &u);
  snprintf(out, cap, "%04d-%02d-%02d", u.tm_year + 1900, u.tm_mon + 1, u.tm_mday);
}

static time_t month_start_epoch(time_t epoch) {
  struct tm u = {};
  gmtime_r(&epoch, &u);
  u.tm_mday = 1;
  u.tm_hour = 0;
  u.tm_min = 0;
  u.tm_sec = 0;
  u.tm_isdst = 0;
  return utc_tm_to_epoch(&u);
}

static time_t next_month_start_epoch(time_t epoch) {
  struct tm u = {};
  gmtime_r(&epoch, &u);
  u.tm_mday = 1;
  u.tm_hour = 0;
  u.tm_min = 0;
  u.tm_sec = 0;
  u.tm_isdst = 0;
  ++u.tm_mon;
  return utc_tm_to_epoch(&u);
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

static bool lookup_human_design_json(const char *json, time_t epoch, PmHumanDesignPositions *out) {
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
  for (int i = 0; i < kPmHdBodyCount; ++i) {
    if (!nth_array_double(json, kHdBodyIds[i], idx, &out->lon[i])) {
      return false;
    }
  }
  out->ok = true;
  return true;
}

static bool ephemeris_fs_ready(void) {
  if (!s_fs_tried) {
    s_fs_tried = true;
    s_fs_ready = LittleFS.begin(true);
    if (s_fs_ready && !LittleFS.exists("/ephem")) {
      (void)LittleFS.mkdir("/ephem");
    }
  }
  return s_fs_ready;
}

static void month_cache_path(const char *month_key, char *out, size_t cap) {
  snprintf(out, cap, "/ephem/%s.json", month_key);
}

static void hd_day_cache_path(const char *day_key, char *out, size_t cap) {
  snprintf(out, cap, "/ephem/hd-%s.json", day_key);
}

static bool load_month_from_fs(const char *month_key) {
  if (!ephemeris_fs_ready()) {
    return false;
  }
  char path[32];
  month_cache_path(month_key, path, sizeof(path));
  if (!LittleFS.exists(path)) {
    return false;
  }
  File f = LittleFS.open(path, FILE_READ);
  if (!f) {
    return false;
  }
  const size_t len = f.size();
  if (len == 0 || len > static_cast<size_t>(MYNAH_EPHEMERIS_MONTH_MAX_BYTES)) {
    f.close();
    (void)LittleFS.remove(path);
    return false;
  }
  if (!s_month_json || s_month_json_cap < len + 1u) {
    if (s_month_json) {
      free(s_month_json);
      s_month_json = nullptr;
    }
    s_month_json_cap = len + 1u;
    s_month_json = static_cast<char *>(pm_heap_alloc_response(s_month_json_cap));
  }
  if (!s_month_json) {
    f.close();
    return false;
  }
  const size_t rd = f.readBytes(s_month_json, len);
  f.close();
  if (rd != len) {
    return false;
  }
  s_month_json[len] = '\0';
  strncpy(s_month_key, month_key, sizeof(s_month_key) - 1);
  s_month_key[sizeof(s_month_key) - 1] = '\0';
  ESP_LOGI(TAG, "loaded cached %s (%u bytes)", month_key, static_cast<unsigned>(len));
  return true;
}

static bool load_hd_day_from_fs(const char *day_key) {
  if (!ephemeris_fs_ready()) {
    return false;
  }
  char path[40];
  hd_day_cache_path(day_key, path, sizeof(path));
  if (!LittleFS.exists(path)) {
    return false;
  }
  File f = LittleFS.open(path, FILE_READ);
  if (!f) {
    return false;
  }
  const size_t len = f.size();
  if (len == 0 || len > static_cast<size_t>(MYNAH_EPHEMERIS_MONTH_MAX_BYTES)) {
    f.close();
    (void)LittleFS.remove(path);
    return false;
  }
  if (!s_hd_day_json || s_hd_day_json_cap < len + 1u) {
    if (s_hd_day_json) {
      free(s_hd_day_json);
      s_hd_day_json = nullptr;
    }
    s_hd_day_json_cap = len + 1u;
    s_hd_day_json = static_cast<char *>(pm_heap_alloc_response(s_hd_day_json_cap));
  }
  if (!s_hd_day_json) {
    f.close();
    return false;
  }
  const size_t rd = f.readBytes(s_hd_day_json, len);
  f.close();
  if (rd != len) {
    return false;
  }
  s_hd_day_json[len] = '\0';
  strncpy(s_hd_day_key, day_key, sizeof(s_hd_day_key) - 1);
  s_hd_day_key[sizeof(s_hd_day_key) - 1] = '\0';
  ESP_LOGI(TAG, "loaded cached hd %s (%u bytes)", day_key, static_cast<unsigned>(len));
  return true;
}

static void save_month_to_fs(const char *month_key, const char *json, size_t len) {
  if (!month_key || !json || len == 0 || !ephemeris_fs_ready()) {
    return;
  }
  char path[32];
  char tmp[40];
  month_cache_path(month_key, path, sizeof(path));
  snprintf(tmp, sizeof(tmp), "%s.tmp", path);
  File f = LittleFS.open(tmp, FILE_WRITE);
  if (!f) {
    return;
  }
  const size_t wr = f.write(reinterpret_cast<const uint8_t *>(json), len);
  f.close();
  if (wr != len) {
    (void)LittleFS.remove(tmp);
    return;
  }
  (void)LittleFS.remove(path);
  if (!LittleFS.rename(tmp, path)) {
    (void)LittleFS.remove(tmp);
    return;
  }
  ESP_LOGI(TAG, "cached %s (%u bytes)", month_key, static_cast<unsigned>(len));
}

static void save_hd_day_to_fs(const char *day_key, const char *json, size_t len) {
  if (!day_key || !json || len == 0 || !ephemeris_fs_ready()) {
    return;
  }
  char path[40];
  char tmp[48];
  hd_day_cache_path(day_key, path, sizeof(path));
  snprintf(tmp, sizeof(tmp), "%s.tmp", path);
  File f = LittleFS.open(tmp, FILE_WRITE);
  if (!f) {
    return;
  }
  const size_t wr = f.write(reinterpret_cast<const uint8_t *>(json), len);
  f.close();
  if (wr != len) {
    (void)LittleFS.remove(tmp);
    return;
  }
  (void)LittleFS.remove(path);
  if (!LittleFS.rename(tmp, path)) {
    (void)LittleFS.remove(tmp);
    return;
  }
  ESP_LOGI(TAG, "cached hd %s (%u bytes)", day_key, static_cast<unsigned>(len));
}

static bool ensure_month_loaded(const char *month_key) {
  s_month_load_from_network = false;
  if (s_month_json && strcmp(s_month_key, month_key) == 0) {
    return true;
  }
  if (load_month_from_fs(month_key)) {
    return true;
  }
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }
  if (!pm_heap_tls_ready(MYNAH_EPHEMERIS_MIN_FETCH_HEAP, "ephemeris")) {
    return false;
  }

  char url[192];
  snprintf(url, sizeof(url), "%s/%s.json", MYNAH_EPHEMERIS_DATA_BASE, month_key);

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(static_cast<uint16_t>(MYNAH_EPHEMERIS_HTTP_MS));
  if (!http.begin(client, url)) {
    return false;
  }
  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    ESP_LOGW(TAG, "month GET %d %s", code, month_key);
    http.end();
    return false;
  }
  const int len = http.getSize();
  if (len <= 0 || len > static_cast<int>(MYNAH_EPHEMERIS_MONTH_MAX_BYTES)) {
    ESP_LOGW(TAG, "month size %d", len);
    http.end();
    return false;
  }
  if (!s_month_json || s_month_json_cap < static_cast<size_t>(len) + 1u) {
    if (s_month_json) {
      free(s_month_json);
      s_month_json = nullptr;
    }
    s_month_json_cap = static_cast<size_t>(len) + 1u;
    s_month_json = static_cast<char *>(pm_heap_alloc_response(s_month_json_cap));
  }
  if (!s_month_json) {
    http.end();
    return false;
  }
  WiFiClient *stream = http.getStreamPtr();
  int rd = 0;
  while (rd < len) {
    const int n = stream->readBytes(s_month_json + rd, static_cast<size_t>(len - rd));
    if (n <= 0) {
      break;
    }
    rd += n;
  }
  http.end();
  if (rd != len) {
    ESP_LOGW(TAG, "month read short %d/%d", rd, len);
    return false;
  }
  s_month_json[rd] = '\0';
  strncpy(s_month_key, month_key, sizeof(s_month_key) - 1);
  s_month_key[sizeof(s_month_key) - 1] = '\0';
  save_month_to_fs(month_key, s_month_json, static_cast<size_t>(rd));
  s_month_load_from_network = true;
  ESP_LOGI(TAG, "loaded %s (%d bytes)", month_key, rd);
  return true;
}

static bool ensure_hd_day_loaded(const char *day_key) {
  if (s_hd_day_json && strcmp(s_hd_day_key, day_key) == 0) {
    return true;
  }
  if (load_hd_day_from_fs(day_key)) {
    return true;
  }
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }
  if (!pm_heap_tls_ready(MYNAH_EPHEMERIS_MIN_FETCH_HEAP, "human-design ephemeris")) {
    return false;
  }

  char url[208];
  snprintf(url, sizeof(url), "%s/%s.json", MYNAH_HUMAN_DESIGN_DATA_BASE, day_key);

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(static_cast<uint16_t>(MYNAH_EPHEMERIS_HTTP_MS));
  if (!http.begin(client, url)) {
    return false;
  }
  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    ESP_LOGW(TAG, "hd day GET %d %s", code, day_key);
    http.end();
    return false;
  }
  const int len = http.getSize();
  if (len <= 0 || len > static_cast<int>(MYNAH_EPHEMERIS_MONTH_MAX_BYTES)) {
    ESP_LOGW(TAG, "hd day size %d", len);
    http.end();
    return false;
  }
  if (!s_hd_day_json || s_hd_day_json_cap < static_cast<size_t>(len) + 1u) {
    if (s_hd_day_json) {
      free(s_hd_day_json);
      s_hd_day_json = nullptr;
    }
    s_hd_day_json_cap = static_cast<size_t>(len) + 1u;
    s_hd_day_json = static_cast<char *>(pm_heap_alloc_response(s_hd_day_json_cap));
  }
  if (!s_hd_day_json) {
    http.end();
    return false;
  }
  WiFiClient *stream = http.getStreamPtr();
  int rd = 0;
  while (rd < len) {
    const int n = stream->readBytes(s_hd_day_json + rd, static_cast<size_t>(len - rd));
    if (n <= 0) {
      break;
    }
    rd += n;
  }
  http.end();
  if (rd != len) {
    ESP_LOGW(TAG, "hd day read short %d/%d", rd, len);
    return false;
  }
  s_hd_day_json[rd] = '\0';
  strncpy(s_hd_day_key, day_key, sizeof(s_hd_day_key) - 1);
  s_hd_day_key[sizeof(s_hd_day_key) - 1] = '\0';
  save_hd_day_to_fs(day_key, s_hd_day_json, static_cast<size_t>(rd));
  ESP_LOGI(TAG, "loaded hd %s (%d bytes)", day_key, rd);
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
  const uint32_t now_ms = millis();
  if (s_fail_backoff_until_ms != 0 && now_ms < s_fail_backoff_until_ms) {
    if (!(s_month_json && strcmp(s_month_key, month) == 0) && !load_month_from_fs(month)) {
      return false;
    }
  }
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
  s_last_from_network = s_month_load_from_network;
  return true;
}

bool pm_ephemeris_fetch_human_design_epoch(time_t utc_epoch, PmHumanDesignPositions *out) {
  if (!out) {
    return false;
  }
  out->ok = false;
#if !MYNAH_EPHEMERIS_ENABLE
  (void)utc_epoch;
  return false;
#else
  if (utc_epoch < 0) {
    return false;
  }
  char day[11];
  day_key_for_epoch(utc_epoch, day, sizeof(day));
  const uint32_t now_ms = millis();
  if (s_fail_backoff_until_ms != 0 && now_ms < s_fail_backoff_until_ms) {
    if (!(s_hd_day_json && strcmp(s_hd_day_key, day) == 0) && !load_hd_day_from_fs(day)) {
      return false;
    }
  }
  if (!ensure_hd_day_loaded(day)) {
    s_fail_backoff_until_ms = now_ms + 30000u;
    return false;
  }
  s_fail_backoff_until_ms = 0;

  PmHumanDesignPositions parsed = {};
  if (!lookup_human_design_json(s_hd_day_json, utc_epoch, &parsed)) {
    ESP_LOGW(TAG, "hd lookup failed %s", day);
    return false;
  }
  *out = parsed;
  return true;
#endif
}

bool pm_ephemeris_prefetch_epoch(time_t utc_epoch) {
#if !MYNAH_EPHEMERIS_ENABLE
  (void)utc_epoch;
  return false;
#else
  if (utc_epoch < 0) {
    return false;
  }
  char month[8];
  month_key_for_epoch(utc_epoch, month, sizeof(month));
  const uint32_t now_ms = millis();
  if (s_fail_backoff_until_ms != 0 && now_ms < s_fail_backoff_until_ms) {
    return (s_month_json && strcmp(s_month_key, month) == 0) || load_month_from_fs(month);
  }
  if (!ensure_month_loaded(month)) {
    s_fail_backoff_until_ms = now_ms + 30000u;
    return false;
  }
  s_fail_backoff_until_ms = 0;
  return true;
#endif
}

bool pm_ephemeris_prefetch_range(time_t utc_start, time_t utc_end) {
#if !MYNAH_EPHEMERIS_ENABLE
  (void)utc_start;
  (void)utc_end;
  return false;
#else
  if (utc_start < 0 || utc_end < 0) {
    return false;
  }
  if (utc_end < utc_start) {
    const time_t tmp = utc_start;
    utc_start = utc_end;
    utc_end = tmp;
  }

  bool ok = true;
  time_t cursor = month_start_epoch(utc_start);
  while (cursor <= utc_end) {
    ok = pm_ephemeris_prefetch_epoch(cursor) && ok;
    const time_t next = next_month_start_epoch(cursor);
    if (next <= cursor) {
      break;
    }
    cursor = next;
  }
  return ok;
#endif
}
