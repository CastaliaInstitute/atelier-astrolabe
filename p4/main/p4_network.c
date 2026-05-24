#include "p4_network.h"

#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs.h"

#if __has_include("secrets.local.h")
#include "secrets.local.h"
#else
#include "secrets.example.h"
#endif

static const char *TAG = "astrolabe_net";
static const char *NVS_NS = "mynah";

static bool s_initialized;
static bool s_init_failed;
static bool s_has_credentials;
static bool s_connected;
static bool s_wifi_enabled;
static esp_netif_t *s_netif;
static char s_ssid[33];
static char s_pass[65];
static char s_ip[16] = "0.0.0.0";
static char s_hostname[32] = "astrolabe-p4";
static int s_rssi;

static bool load_credentials(char *ssid, size_t ssid_size, char *pass, size_t pass_size) {
  nvs_handle_t nvs;
  size_t ssid_len = ssid_size;
  size_t pass_len = pass_size;
  ssid[0] = '\0';
  pass[0] = '\0';

  if (nvs_open(NVS_NS, NVS_READONLY, &nvs) == ESP_OK) {
    esp_err_t ssid_err = nvs_get_str(nvs, "ssid", ssid, &ssid_len);
    esp_err_t pass_err = nvs_get_str(nvs, "pass", pass, &pass_len);
    nvs_close(nvs);
    if (ssid_err == ESP_OK && pass_err == ESP_OK && ssid[0] != '\0') {
      return true;
    }
  }

  if (MYNAH_WIFI_SSID[0] == '\0') {
    return false;
  }
  strlcpy(ssid, MYNAH_WIFI_SSID, ssid_size);
  strlcpy(pass, MYNAH_WIFI_PASSWORD, pass_size);
  return true;
}

static void make_hostname(void) {
  uint8_t mac[6] = {};
  if (esp_read_mac(mac, ESP_MAC_BASE) == ESP_OK) {
    snprintf(s_hostname, sizeof(s_hostname), "astrolabe-p4-%02x%02x%02x", mac[3], mac[4], mac[5]);
  }
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data) {
  (void)arg;
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
    ESP_LOGI(TAG, "wifi sta start");
    if (s_has_credentials) {
      (void)esp_wifi_connect();
    }
  } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *)data;
    s_connected = false;
    strlcpy(s_ip, "0.0.0.0", sizeof(s_ip));
    ESP_LOGW(TAG, "wifi disconnected reason=%d%s", event ? event->reason : -1,
             s_has_credentials && s_wifi_enabled ? "; reconnecting" : "");
    if (s_has_credentials && s_wifi_enabled) {
      (void)esp_wifi_connect();
    }
  } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
    snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&event->ip_info.ip));
    wifi_ap_record_t ap = {};
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
      s_rssi = ap.rssi;
    }
    s_connected = true;
    ESP_LOGI(TAG, "wifi connected ssid=%s ip=%s rssi=%d host=%s", s_ssid, s_ip, s_rssi, s_hostname);
  }
}

esp_err_t astrolabe_p4_network_init(void) {
  if (s_initialized) {
    return ESP_OK;
  }
  if (s_init_failed) {
    return ESP_FAIL;
  }

  make_hostname();
  ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "esp_netif_init");
  esp_err_t loop = esp_event_loop_create_default();
  if (loop != ESP_OK && loop != ESP_ERR_INVALID_STATE) {
    ESP_RETURN_ON_ERROR(loop, TAG, "esp_event_loop_create_default");
  }

  s_netif = esp_netif_create_default_wifi_sta();
  ESP_RETURN_ON_FALSE(s_netif != NULL, ESP_FAIL, TAG, "create sta netif");
  (void)esp_netif_set_hostname(s_netif, s_hostname);

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  esp_err_t ret = esp_wifi_init(&cfg);
  if (ret != ESP_OK) {
    s_init_failed = true;
    ESP_LOGE(TAG, "wifi remote unavailable: esp_wifi_init failed: %s", esp_err_to_name(ret));
    return ret;
  }
  ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, NULL),
                      TAG, "wifi handler");
  ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL, NULL),
                      TAG, "ip handler");
  ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "wifi storage");
  ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "wifi mode");

  s_has_credentials = load_credentials(s_ssid, sizeof(s_ssid), s_pass, sizeof(s_pass));
  s_initialized = true;
  ESP_LOGI(TAG, "wifi remote initialized host=%s credentials=%s", s_hostname, s_has_credentials ? "yes" : "no");
  return ESP_OK;
}

bool astrolabe_p4_network_start(void) {
  if (astrolabe_p4_network_init() != ESP_OK) {
    return false;
  }
  if (!s_has_credentials) {
    ESP_LOGW(TAG, "wifi not started: no credentials. Use: wifi set <ssid> <password>");
    return false;
  }
  s_wifi_enabled = true;

  wifi_config_t wifi_config = {};
  strlcpy((char *)wifi_config.sta.ssid, s_ssid, sizeof(wifi_config.sta.ssid));
  strlcpy((char *)wifi_config.sta.password, s_pass, sizeof(wifi_config.sta.password));
  wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
  if (s_pass[0] == '\0') {
    wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
  }
  wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
  esp_err_t ret = esp_wifi_start();
  if (ret == ESP_ERR_WIFI_CONN) {
    ret = esp_wifi_connect();
  }
  if (ret != ESP_OK && ret != ESP_ERR_WIFI_CONN) {
    ESP_LOGE(TAG, "wifi start failed: %s", esp_err_to_name(ret));
    return false;
  }
  ESP_LOGI(TAG, "wifi connecting ssid=%s host=%s", s_ssid, s_hostname);
  return true;
}

bool astrolabe_p4_network_stop(void) {
  if (!s_initialized) {
    return true;
  }
  s_wifi_enabled = false;
  s_connected = false;
  strlcpy(s_ip, "0.0.0.0", sizeof(s_ip));
  (void)esp_wifi_disconnect();
  esp_err_t ret = esp_wifi_stop();
  if (ret != ESP_OK && ret != ESP_ERR_WIFI_NOT_INIT) {
    ESP_LOGW(TAG, "wifi stop failed: %s", esp_err_to_name(ret));
    return false;
  }
  ESP_LOGI(TAG, "wifi stopped");
  return true;
}

bool astrolabe_p4_network_save_credentials(const char *ssid, const char *pass) {
  if (ssid == NULL || ssid[0] == '\0' || strlen(ssid) > 32 || (pass != NULL && strlen(pass) > 64)) {
    ESP_LOGW(TAG, "invalid wifi credentials");
    return false;
  }
  nvs_handle_t nvs;
  if (nvs_open(NVS_NS, NVS_READWRITE, &nvs) != ESP_OK) {
    return false;
  }
  esp_err_t ret = nvs_set_str(nvs, "ssid", ssid);
  if (ret == ESP_OK) {
    ret = nvs_set_str(nvs, "pass", pass ? pass : "");
  }
  if (ret == ESP_OK) {
    ret = nvs_commit(nvs);
  }
  nvs_close(nvs);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "save wifi credentials failed: %s", esp_err_to_name(ret));
    return false;
  }
  strlcpy(s_ssid, ssid, sizeof(s_ssid));
  strlcpy(s_pass, pass ? pass : "", sizeof(s_pass));
  s_has_credentials = true;
  ESP_LOGI(TAG, "saved wifi credentials ssid=%s", s_ssid);
  return astrolabe_p4_network_start();
}

bool astrolabe_p4_network_forget_credentials(void) {
  nvs_handle_t nvs;
  if (nvs_open(NVS_NS, NVS_READWRITE, &nvs) == ESP_OK) {
    (void)nvs_erase_key(nvs, "ssid");
    (void)nvs_erase_key(nvs, "pass");
    (void)nvs_commit(nvs);
    nvs_close(nvs);
  }
  s_has_credentials = false;
  s_ssid[0] = '\0';
  s_pass[0] = '\0';
  s_connected = false;
  strlcpy(s_ip, "0.0.0.0", sizeof(s_ip));
  (void)esp_wifi_disconnect();
  ESP_LOGI(TAG, "forgot wifi credentials");
  return true;
}

void astrolabe_p4_network_log_status(void) {
  astrolabe_p4_network_status_t st = astrolabe_p4_network_status();
  ESP_LOGI(TAG, "wifi status init=%d creds=%d connected=%d ssid=%s ip=%s rssi=%d host=%s", st.initialized,
           st.has_credentials, st.connected, st.ssid, st.ip, st.rssi, st.hostname);
}

void astrolabe_p4_network_scan(void) {
  if (astrolabe_p4_network_init() != ESP_OK) {
    return;
  }
  esp_err_t ret = esp_wifi_start();
  if (ret != ESP_OK && ret != ESP_ERR_WIFI_CONN) {
    ESP_LOGW(TAG, "wifi scan start failed: %s", esp_err_to_name(ret));
    return;
  }
  wifi_scan_config_t scan = {};
  ret = esp_wifi_scan_start(&scan, true);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "wifi scan failed: %s", esp_err_to_name(ret));
    return;
  }
  uint16_t count = 12;
  wifi_ap_record_t aps[12] = {};
  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_scan_get_ap_records(&count, aps));
  ESP_LOGI(TAG, "wifi scan found=%u showing=%u", (unsigned)count, (unsigned)count);
  for (uint16_t i = 0; i < count; ++i) {
    ESP_LOGI(TAG, "wifi ap %u ssid=%s rssi=%d auth=%d", (unsigned)i, (const char *)aps[i].ssid, aps[i].rssi,
             aps[i].authmode);
  }
}

astrolabe_p4_network_status_t astrolabe_p4_network_status(void) {
  astrolabe_p4_network_status_t status = {
      .initialized = s_initialized,
      .has_credentials = s_has_credentials,
      .connected = s_connected,
      .rssi = s_rssi,
  };
  strlcpy(status.ssid, s_ssid, sizeof(status.ssid));
  strlcpy(status.ip, s_ip, sizeof(status.ip));
  strlcpy(status.hostname, s_hostname, sizeof(status.hostname));
  return status;
}
