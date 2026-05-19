#include "pm_wifi_ntp.h"

#include <ESPmDNS.h>
#include <WiFi.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <stdlib.h>
#include <time.h>

#include "pm_config.h"
#include "pm_geo_tz.h"
#include "pm_log.h"
#include "pm_speaker.h"
#include "pm_wifi_creds.h"

static const char *TAG = "pm_wifi";
static constexpr uint32_t kWifiTimeoutMs = 20000;
static bool s_wifi_link_chimed = false;
static bool s_mdns_started = false;

#ifndef ASTROLABE_MDNS_HOSTNAME
#define ASTROLABE_MDNS_HOSTNAME "astrolabe"
#endif

const char *pm_wifi_hostname() { return ASTROLABE_MDNS_HOSTNAME; }

const char *pm_wifi_mdns_name() { return ASTROLABE_MDNS_HOSTNAME ".local"; }

static void pm_wifi_mdns_begin(void) {
  if (s_mdns_started || !pm_wifi_connected()) {
    return;
  }
  if (!MDNS.begin(ASTROLABE_MDNS_HOSTNAME)) {
    ESP_LOGW(TAG, "mDNS start failed");
    pm_log_printf(false, "wifi: mdns failed host=%s", pm_wifi_mdns_name());
    return;
  }
  MDNS.addService("http", "tcp", 80);
  s_mdns_started = true;
  ESP_LOGI(TAG, "mDNS http://%s/", pm_wifi_mdns_name());
  pm_log_printf(false, "wifi: mdns http://%s/ ip=%s", pm_wifi_mdns_name(),
                WiFi.localIP().toString().c_str());
}

static void pm_wifi_mdns_end(void) {
  if (!s_mdns_started) {
    return;
  }
  MDNS.end();
  s_mdns_started = false;
}

#ifndef ASTROLABE_QEMU
static void speaker_tone_blocking(float hz, uint32_t duration_ms) {
  if (!pm_speaker_play_tone_begin(hz, duration_ms)) {
    return;
  }
  const uint32_t t0 = millis();
  while (pm_speaker_poll() == PmSpeakerStatus::Playing) {
    if (millis() - t0 > duration_ms + 2500u) {
      pm_speaker_abort();
      break;
    }
    delay(5);
  }
}

static void pm_wifi_play_connect_chime(void) {
  /** Brief two-note major third (E5 → G5). */
  speaker_tone_blocking(659.25f, 95u);
  speaker_tone_blocking(783.99f, 130u);
  ESP_LOGI(TAG, "connect chime");
}
#endif

static void pm_wifi_on_link_up(void) {
  if (s_wifi_link_chimed) {
    return;
  }
  s_wifi_link_chimed = true;
#ifndef ASTROLABE_QEMU
  pm_wifi_play_connect_chime();
#endif
}

void pm_wifi_enable_bt_coexistence(void) {
#ifndef ASTROLABE_QEMU
  const wifi_mode_t mode = WiFi.getMode();
  if (mode == WIFI_OFF) {
    return;
  }
  WiFi.setSleep(true);
  const esp_err_t ps = esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
  if (ps != ESP_OK) {
    ESP_LOGW(TAG, "esp_wifi_set_ps(MIN_MODEM) %d", static_cast<int>(ps));
  } else {
    ESP_LOGI(TAG, "modem sleep on (BT coexistence)");
  }
#endif
}

bool pm_wifi_begin() {
  char ssid[64];
  char pass[64];
  if (!pm_wifi_credentials_load(ssid, sizeof(ssid), pass, sizeof(pass))) {
    return false;
  }
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(ASTROLABE_MDNS_HOSTNAME);
  WiFi.setSleep(false);
  WiFi.begin(ssid, pass);
  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < kWifiTimeoutMs) {
    delay(200);
  }
  const bool connected = WiFi.status() == WL_CONNECTED;
  if (connected) {
    pm_wifi_on_link_up();
    pm_wifi_mdns_begin();
  } else {
    s_wifi_link_chimed = false;
    pm_wifi_mdns_end();
  }
  return connected;
}

void pm_wifi_poll(void) {
  const bool connected = pm_wifi_connected();
  if (connected) {
    if (!s_wifi_link_chimed) {
      pm_wifi_on_link_up();
    }
    pm_wifi_mdns_begin();
  } else {
    s_wifi_link_chimed = false;
    pm_wifi_mdns_end();
  }
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
