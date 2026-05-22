#include "pm_wifi_creds.h"

#include <Arduino.h>
#include <string.h>

#include "pm_config.h"
#include "pm_nvs.h"

static constexpr const char *kNvsNs = "mynah";

bool pm_wifi_credentials_load(char *ssid, size_t ssid_sz, char *pass, size_t pass_sz) {
  if (!ssid || !pass || ssid_sz < 2 || pass_sz < 2) {
    return false;
  }
  ssid[0] = '\0';
  pass[0] = '\0';

  if (!pm_nvs_has_key(kNvsNs, "ssid")) {
    const char *default_ssid = strlen(MYNAH_WIFI_SSID) > 0 ? MYNAH_WIFI_SSID : MYNAH_WIFI_NVS_DEFAULT_SSID;
    const char *default_pass = strlen(MYNAH_WIFI_SSID) > 0 ? MYNAH_WIFI_PASSWORD : MYNAH_WIFI_NVS_DEFAULT_PASS;
    (void)pm_nvs_set_str(kNvsNs, "ssid", default_ssid);
    (void)pm_nvs_set_str(kNvsNs, "pass", default_pass);
  }

  if (!pm_nvs_has_key(kNvsNs, "ssid")) {
    strncpy(ssid, MYNAH_WIFI_SSID, ssid_sz - 1);
    ssid[ssid_sz - 1] = '\0';
    strncpy(pass, MYNAH_WIFI_PASSWORD, pass_sz - 1);
    pass[pass_sz - 1] = '\0';
    return strlen(ssid) > 0;
  }

  (void)pm_nvs_get_str(kNvsNs, "ssid", ssid, ssid_sz, "");
  (void)pm_nvs_get_str(kNvsNs, "pass", pass, pass_sz, "");
  return strlen(ssid) > 0;
}

bool pm_wifi_credentials_save(const char *ssid, const char *pass) {
  if (!ssid || ssid[0] == '\0' || strlen(ssid) > 63 || (pass && strlen(pass) > 63)) {
    return false;
  }
  const bool ssid_written = pm_nvs_set_str(kNvsNs, "ssid", ssid);
  (void)pm_nvs_set_str(kNvsNs, "pass", pass ? pass : "");
  return ssid_written;
}

bool pm_wifi_credentials_clear(void) {
  const bool ssid_removed = pm_nvs_remove(kNvsNs, "ssid");
  const bool pass_removed = pm_nvs_remove(kNvsNs, "pass");
  return ssid_removed && pass_removed;
}
