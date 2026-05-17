#include "pm_geo_tz.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <stdlib.h>
#include <string.h>

static int32_t s_tz_off_sec = 0;

int32_t pm_geo_tz_offset_sec() { return s_tz_off_sec; }

static bool parse_worldtimeapi_utc_offset(const char *json, int32_t *out_sec) {
  const char *k = strstr(json, "\"utc_offset\"");
  if (!k) {
    return false;
  }
  const char *colon = strchr(k, ':');
  if (!colon) {
    return false;
  }
  const char *q = colon + 1;
  while (*q == ' ' || *q == '\t') {
    ++q;
  }
  if (*q != '"') {
    return false;
  }
  ++q;
  int sign = 1;
  if (*q == '+') {
    ++q;
  } else if (*q == '-') {
    sign = -1;
    ++q;
  }
  int h = 0;
  int m = 0;
  int s = 0;
  const int n = sscanf(q, "%d:%d:%d", &h, &m, &s);
  if (n < 1) {
    return false;
  }
  if (n == 1) {
    m = 0;
    s = 0;
  } else if (n == 2) {
    s = 0;
  }
  *out_sec = sign * (h * 3600 + m * 60 + s);
  return true;
}

static bool parse_ip_api_offset(const char *json, int32_t *out_sec) {
  if (strstr(json, "\"status\":\"fail\"") != nullptr) {
    return false;
  }
  const char *k = strstr(json, "\"offset\":");
  if (!k) {
    return false;
  }
  const char *p = k + strlen("\"offset\":");
  while (*p == ' ' || *p == '\t') {
    ++p;
  }
  char *end = nullptr;
  const long v = strtol(p, &end, 10);
  if (end == p) {
    return false;
  }
  *out_sec = static_cast<int32_t>(v);
  return true;
}

static bool fetch_url_body(const char *url, char *buf, size_t buf_cap) {
  if (!buf || buf_cap < 32) {
    return false;
  }
  HTTPClient http;
  http.setTimeout(12000);
  if (!http.begin(url)) {
    return false;
  }
  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    return false;
  }
  const String body = http.getString();
  http.end();
  if (body.length() == 0 || static_cast<size_t>(body.length()) >= buf_cap) {
    return false;
  }
  memcpy(buf, body.c_str(), static_cast<size_t>(body.length()) + 1);
  return true;
}

bool pm_geo_tz_refresh_from_ip() {
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }
  char buf[1536];
  int32_t off = 0;
  if (fetch_url_body("http://worldtimeapi.org/api/ip", buf, sizeof(buf)) &&
      parse_worldtimeapi_utc_offset(buf, &off)) {
    s_tz_off_sec = off;
    return true;
  }
  if (fetch_url_body("http://ip-api.com/json/?fields=status,offset", buf, sizeof(buf)) &&
      parse_ip_api_offset(buf, &off)) {
    s_tz_off_sec = off;
    return true;
  }
  return false;
}
