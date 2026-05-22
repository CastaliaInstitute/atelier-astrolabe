#include "pm_wifi_ntp.h"

#if __has_include(<esp_bt.h>)
#include <esp_bt.h>
#define PM_WIFI_HAS_ESP_BT 1
#else
#define PM_WIFI_HAS_ESP_BT 0
#endif
#include <esp_event.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_netif.h>
#include <esp_sntp.h>
#include <esp_wifi.h>
#include <mdns.h>
#include <stdlib.h>
#include <string.h>
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
static bool s_wifi_driver_ready = false;
static bool s_wifi_connected = false;
static bool s_wifi_paused_for_ble = false;
static esp_netif_t *s_sta_netif = nullptr;
static char s_hostname[32] = "";
static char s_mdns_name[40] = "";
static char s_mac_suffix[7] = "";
static char s_mac_string[18] = "";
static char s_local_ip[16] = "0.0.0.0";

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
  (void)esp_read_mac(mac, ESP_MAC_WIFI_STA);
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

const char *pm_wifi_local_ip() {
  if (!pm_wifi_connected()) {
    return "0.0.0.0";
  }
  esp_netif_ip_info_t ip = {};
  if (s_sta_netif && esp_netif_get_ip_info(s_sta_netif, &ip) == ESP_OK && ip.ip.addr != 0) {
    snprintf(s_local_ip, sizeof(s_local_ip), IPSTR, IP2STR(&ip.ip));
  }
  return s_local_ip;
}

int pm_wifi_rssi() {
  wifi_ap_record_t ap = {};
  return esp_wifi_sta_get_ap_info(&ap) == ESP_OK ? static_cast<int>(ap.rssi) : 0;
}

int pm_wifi_status_code() { return pm_wifi_connected() ? 3 : 0; }

static void pm_wifi_event_handler(void *, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
    s_wifi_connected = false;
    s_local_ip[0] = '\0';
    strncpy(s_local_ip, "0.0.0.0", sizeof(s_local_ip));
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    s_wifi_connected = true;
    const ip_event_got_ip_t *event = static_cast<const ip_event_got_ip_t *>(event_data);
    if (event) {
      snprintf(s_local_ip, sizeof(s_local_ip), IPSTR, IP2STR(&event->ip_info.ip));
    }
  }
}

static bool pm_wifi_driver_init(void) {
  if (s_wifi_driver_ready) {
    return true;
  }
  esp_err_t err = esp_netif_init();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ESP_LOGW(TAG, "esp_netif_init failed %d", static_cast<int>(err));
    return false;
  }
  err = esp_event_loop_create_default();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ESP_LOGW(TAG, "event loop create failed %d", static_cast<int>(err));
    return false;
  }
  s_sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  if (!s_sta_netif) {
    s_sta_netif = esp_netif_create_default_wifi_sta();
  }
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  err = esp_wifi_init(&cfg);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ESP_LOGW(TAG, "esp_wifi_init failed %d", static_cast<int>(err));
    return false;
  }
  (void)esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, pm_wifi_event_handler, nullptr, nullptr);
  (void)esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, pm_wifi_event_handler, nullptr, nullptr);
  s_wifi_driver_ready = true;
  return true;
}

void pm_wifi_print_scan() {
  if (!pm_wifi_driver_init()) {
    Serial.println("wifi: scan driver failed");
    return;
  }
  wifi_mode_t mode = WIFI_MODE_NULL;
  if (esp_wifi_get_mode(&mode) != ESP_OK || mode == WIFI_MODE_NULL) {
    (void)esp_wifi_set_mode(WIFI_MODE_STA);
    (void)esp_wifi_start();
  }
  const esp_err_t scan_err = esp_wifi_scan_start(nullptr, true);
  if (scan_err != ESP_OK) {
    Serial.printf("wifi: scan failed err=%d\n", static_cast<int>(scan_err));
    return;
  }
  uint16_t n = 0;
  (void)esp_wifi_scan_get_ap_num(&n);
  wifi_ap_record_t aps[12] = {};
  uint16_t cap = 12;
  (void)esp_wifi_scan_get_ap_records(&cap, aps);
  Serial.printf("wifi: scan count=%u\n", static_cast<unsigned>(n));
  for (uint16_t i = 0; i < cap; ++i) {
    Serial.printf("wifi: ap %u ssid=%s rssi=%d channel=%u enc=%d\n", static_cast<unsigned>(i),
                  reinterpret_cast<const char *>(aps[i].ssid), static_cast<int>(aps[i].rssi),
                  static_cast<unsigned>(aps[i].primary), static_cast<int>(aps[i].authmode));
  }
}

static void pm_wifi_mdns_begin(void) {
  if (s_mdns_started || !pm_wifi_connected()) {
    return;
  }
  esp_err_t err = mdns_init();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ESP_LOGW(TAG, "mDNS start failed");
    pm_log_printf(false, "wifi: mdns failed host=%s", pm_wifi_mdns_name());
    return;
  }
  (void)mdns_hostname_set(pm_wifi_hostname());
  (void)mdns_instance_name_set("Mynah Astrolabe");
  mdns_txt_item_t txt[] = {
      {"host", pm_wifi_hostname()},
      {"mac", pm_wifi_mac_string()},
      {"mac6", pm_wifi_mac_suffix()},
      {"product", "Mynah Astrolabe"},
  };
  err = mdns_service_add(pm_wifi_hostname(), "_http", "_tcp", 80, txt, sizeof(txt) / sizeof(txt[0]));
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "mDNS service add failed %d", static_cast<int>(err));
  }
  s_mdns_started = true;
  ESP_LOGI(TAG, "mDNS http://%s/", pm_wifi_mdns_name());
  pm_log_printf(false, "wifi: mdns http://%s/ ip=%s mac=%s", pm_wifi_mdns_name(),
                pm_wifi_local_ip(), pm_wifi_mac_string());
}

static void pm_wifi_mdns_end(void) {
  if (!s_mdns_started) {
    return;
  }
  mdns_free();
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

static bool pm_wifi_bt_controller_enabled(void) {
#if PM_WIFI_HAS_ESP_BT
  return esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED;
#else
  return false;
#endif
}

void pm_wifi_enable_bt_coexistence(void) {
#ifndef ASTROLABE_QEMU
  wifi_mode_t mode = WIFI_MODE_NULL;
  if (esp_wifi_get_mode(&mode) != ESP_OK || mode == WIFI_MODE_NULL) {
    return;
  }
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
  (void)esp_wifi_disconnect();
  (void)esp_wifi_stop();
  s_wifi_connected = false;
  strncpy(s_local_ip, "0.0.0.0", sizeof(s_local_ip));
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
  if (!pm_wifi_driver_init()) {
    return false;
  }
  pm_wifi_identity_init();
  if (s_sta_netif) {
    (void)esp_netif_set_hostname(s_sta_netif, pm_wifi_hostname());
  }
  (void)esp_wifi_set_mode(WIFI_MODE_STA);
  const bool bt_enabled = pm_wifi_bt_controller_enabled();
  (void)esp_wifi_set_ps(bt_enabled ? WIFI_PS_MIN_MODEM : WIFI_PS_NONE);
  wifi_config_t cfg = {};
  strncpy(reinterpret_cast<char *>(cfg.sta.ssid), ssid, sizeof(cfg.sta.ssid) - 1);
  strncpy(reinterpret_cast<char *>(cfg.sta.password), pass, sizeof(cfg.sta.password) - 1);
  cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
  cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
  (void)esp_wifi_set_config(WIFI_IF_STA, &cfg);
  (void)esp_wifi_start();
  (void)esp_wifi_connect();
  const uint32_t start = millis();
  while (!pm_wifi_connected() && (millis() - start) < kWifiTimeoutMs) {
    delay(200);
  }
  const bool connected = pm_wifi_connected();
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
  (void)esp_wifi_disconnect();
  s_wifi_connected = false;
  strncpy(s_local_ip, "0.0.0.0", sizeof(s_local_ip));
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

bool pm_wifi_connected() {
  wifi_ap_record_t ap = {};
  if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
    s_wifi_connected = true;
    return true;
  }
  return s_wifi_connected;
}

static void ntp_start() {
  setenv("TZ", "UTC0", 1);
  tzset();
  if (esp_sntp_enabled()) {
    esp_sntp_stop();
  }
  esp_sntp_setoperatingmode(static_cast<esp_sntp_operatingmode_t>(SNTP_OPMODE_POLL));
  esp_sntp_setservername(0, "time.google.com");
  esp_sntp_setservername(1, "time.cloudflare.com");
  esp_sntp_setservername(2, "pool.ntp.org");
  esp_sntp_init();
}

void pm_ntp_sync_blocking() {
  if (!pm_wifi_connected()) {
    return;
  }
  const bool tz_ok = pm_geo_tz_refresh_from_ip();
  ntp_start();
  for (int i = 0; i < 120 && !pm_time_valid(); ++i) {
    delay(500);
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
  delay(800);
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
