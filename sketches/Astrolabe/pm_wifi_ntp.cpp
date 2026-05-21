#include "pm_wifi_ntp.h"

#include <ESPmDNS.h>
#include <WiFi.h>
#include <esp_bt.h>
#include <esp_log.h>
#include <esp_mac.h>
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
static bool s_identity_ready = false;
static bool s_wifi_paused_for_ble = false;
static char s_hostname[32] = "";
static char s_mdns_name[40] = "";
static char s_mac_suffix[7] = "";
static char s_mac_string[18] = "";

#ifndef ASTROLABE_MDNS_HOSTNAME
#define ASTROLABE_MDNS_HOSTNAME "astrolabe"
#endif

static bool has_mac_suffix(const char *host) {
  const size_t len = host ? strlen(host) : 0;
  if (len < 8 || host[len - 7] != '-') {
    return false;
  }
  for (size_t i = len - 6; i < len; ++i) {
    const char c = host[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
      return false;
    }
  }
  return true;
}

static void pm_wifi_identity_init(void) {
  if (s_identity_ready) {
    return;
  }
  uint8_t mac[6] = {};
  if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) {
    WiFi.macAddress(mac);
  }
  snprintf(s_mac_suffix, sizeof(s_mac_suffix), "%02x%02x%02x", mac[3], mac[4], mac[5]);
  snprintf(s_mac_string, sizeof(s_mac_string), "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3],
           mac[4], mac[5]);
  if (has_mac_suffix(ASTROLABE_MDNS_HOSTNAME)) {
    snprintf(s_hostname, sizeof(s_hostname), "%s", ASTROLABE_MDNS_HOSTNAME);
  } else {
    snprintf(s_hostname, sizeof(s_hostname), "%s-%s", ASTROLABE_MDNS_HOSTNAME, s_mac_suffix);
  }
  snprintf(s_mdns_name, sizeof(s_mdns_name), "%s.local", s_hostname);
  s_identity_ready = true;
}

const char *pm_wifi_hostname() {
  pm_wifi_identity_init();
  return s_hostname;
}

const char *pm_wifi_mdns_name() {
  pm_wifi_identity_init();
  return s_mdns_name;
}

const char *pm_wifi_mac_suffix() {
  pm_wifi_identity_init();
  return s_mac_suffix;
}

const char *pm_wifi_mac_string() {
  pm_wifi_identity_init();
  return s_mac_string;
}

static void pm_wifi_mdns_begin(void) {
  if (s_mdns_started || !pm_wifi_connected()) {
    return;
  }
  if (!MDNS.begin(pm_wifi_hostname())) {
    ESP_LOGW(TAG, "mDNS start failed");
    pm_log_printf(false, "wifi: mdns failed host=%s", pm_wifi_mdns_name());
    return;
  }
  MDNS.addService("http", "tcp", 80);
  MDNS.addServiceTxt("http", "tcp", "host", pm_wifi_hostname());
  MDNS.addServiceTxt("http", "tcp", "mac", pm_wifi_mac_string());
  MDNS.addServiceTxt("http", "tcp", "mac6", pm_wifi_mac_suffix());
  MDNS.addServiceTxt("http", "tcp", "product", "Mynah Astrolabe");
  s_mdns_started = true;
  ESP_LOGI(TAG, "mDNS http://%s/", pm_wifi_mdns_name());
  pm_log_printf(false, "wifi: mdns http://%s/ ip=%s mac=%s", pm_wifi_mdns_name(),
                WiFi.localIP().toString().c_str(), pm_wifi_mac_string());
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

void pm_wifi_pause_for_ble(void) {
#ifndef ASTROLABE_QEMU
  wifi_mode_t mode = WIFI_MODE_NULL;
  const esp_err_t mode_err = esp_wifi_get_mode(&mode);
  if (mode_err != ESP_OK || mode == WIFI_MODE_NULL) {
    s_wifi_paused_for_ble = true;
    return;
  }
  pm_wifi_mdns_end();
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);
  s_wifi_link_chimed = false;
  s_wifi_paused_for_ble = true;
  delay(150);
  ESP_LOGI(TAG, "wifi paused for BLE heap");
#endif
}

void pm_wifi_resume_after_ble(void) {
#ifndef ASTROLABE_QEMU
  if (!s_wifi_paused_for_ble) {
    return;
  }
  s_wifi_paused_for_ble = false;
  (void)pm_wifi_begin();
#endif
}

bool pm_wifi_begin() {
  char ssid[64];
  char pass[64];
  if (!pm_wifi_credentials_load(ssid, sizeof(ssid), pass, sizeof(pass))) {
    return false;
  }
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(pm_wifi_hostname());
  const bool bt_enabled = esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED;
  WiFi.setSleep(bt_enabled);
  if (bt_enabled) {
    (void)esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
  }
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

bool pm_wifi_reconnect() {
  s_wifi_link_chimed = false;
  pm_wifi_mdns_end();
  WiFi.disconnect(false, true);
  delay(250);
  return pm_wifi_begin();
}

void pm_wifi_poll(void) {
  if (s_wifi_paused_for_ble) {
    return;
  }
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
  configTime(0, 0, "time.google.com", "time.cloudflare.com", "pool.ntp.org");
}

void pm_ntp_sync_blocking() {
  if (!pm_wifi_connected()) {
    return;
  }
  const bool tz_ok = pm_geo_tz_refresh_from_ip();
  ntp_start();
  struct tm ti = {};
  for (int i = 0; i < 120 && !pm_time_valid(); ++i) {
    (void)getLocalTime(&ti, 500);
    delay(50);
  }
  if (pm_time_valid()) {
    struct tm utc = {};
    struct tm local = {};
    pm_time_utc(&utc);
    pm_time_local(&local);
    char utc_s[28];
    char local_s[28];
    strftime(utc_s, sizeof(utc_s), "%Y-%m-%dT%H:%M:%SZ", &utc);
    strftime(local_s, sizeof(local_s), "%Y-%m-%d %H:%M:%S", &local);
    pm_log_printf(false, "ntp: synced utc=%s local=%s offset_sec=%ld tz=%s", utc_s, local_s,
                  static_cast<long>(pm_geo_tz_offset_sec()), tz_ok ? "ok" : "fallback");
  } else {
    pm_log_printf(false, "ntp: sync failed");
  }
}

void pm_ntp_retry_if_stale() {
  if (!pm_wifi_connected() || pm_time_valid()) {
    return;
  }
  ntp_start();
  struct tm ti = {};
  (void)getLocalTime(&ti, 800);
}

bool pm_time_valid() { return time(nullptr) >= static_cast<time_t>(MYNAH_TIME_VALID_MIN_EPOCH); }

void pm_time_utc(struct tm *out_tm) {
  const time_t t = time(nullptr);
  gmtime_r(&t, out_tm);
}

void pm_time_local(struct tm *out_tm) {
  const time_t t = time(nullptr) + static_cast<time_t>(pm_geo_tz_offset_sec());
  gmtime_r(&t, out_tm);
}
