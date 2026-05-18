#include "pm_wifi_ntp.h"

#include <WiFi.h>
#include <stdlib.h>
#include <time.h>

#include "pm_config.h"
#include "pm_geo_tz.h"
#include "pm_wifi_creds.h"

static constexpr uint32_t kWifiTimeoutMs = 20000;

bool pm_wifi_begin() {
  char ssid[64];
  char pass[64];
  if (!pm_wifi_credentials_load(ssid, sizeof(ssid), pass, sizeof(pass))) {
    return false;
  }
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(ssid, pass);
  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < kWifiTimeoutMs) {
    delay(200);
  }
  return WiFi.status() == WL_CONNECTED;
}

bool pm_wifi_connected() { return WiFi.status() == WL_CONNECTED; }

static void ntp_start() {
  setenv("TZ", "UTC0", 1);
  tzset();
  configTime(0, 0, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
}

void pm_ntp_sync_blocking() {
  if (!pm_wifi_connected()) {
    return;
  }
  (void)pm_geo_tz_refresh_from_ip();
  ntp_start();
  struct tm ti = {};
  for (int i = 0; i < 120 && time(nullptr) < 1000000000; ++i) {
    (void)getLocalTime(&ti, 500);
    delay(50);
  }
}

void pm_ntp_retry_if_stale() {
  if (!pm_wifi_connected() || pm_time_valid()) {
    return;
  }
  (void)pm_geo_tz_refresh_from_ip();
  ntp_start();
  struct tm ti = {};
  (void)getLocalTime(&ti, 800);
}

bool pm_time_valid() { return time(nullptr) > 1000000000; }

void pm_time_utc(struct tm *out_tm) {
  const time_t t = time(nullptr);
  gmtime_r(&t, out_tm);
}

void pm_time_local(struct tm *out_tm) {
  const time_t t = time(nullptr) + static_cast<time_t>(pm_geo_tz_offset_sec());
  gmtime_r(&t, out_tm);
}
