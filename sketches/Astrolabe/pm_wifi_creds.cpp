#include "pm_wifi_creds.h"

#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

#include "pm_config.h"

static constexpr const char *kNvsNs = "mynah";

bool pm_wifi_credentials_load(char *ssid, size_t ssid_sz, char *pass, size_t pass_sz) {
  if (!ssid || !pass || ssid_sz < 2 || pass_sz < 2) {
    return false;
  }
  ssid[0] = '\0';
  pass[0] = '\0';

  Preferences pref;
  if (!pref.begin(kNvsNs, false)) {
    strncpy(ssid, MYNAH_WIFI_SSID, ssid_sz - 1);
    ssid[ssid_sz - 1] = '\0';
    strncpy(pass, MYNAH_WIFI_PASSWORD, pass_sz - 1);
    pass[pass_sz - 1] = '\0';
    return strlen(ssid) > 0;
  }

  if (!pref.isKey("ssid")) {
    if (strlen(MYNAH_WIFI_SSID) > 0) {
      pref.putString("ssid", MYNAH_WIFI_SSID);
      pref.putString("pass", MYNAH_WIFI_PASSWORD);
    } else {
      pref.putString("ssid", MYNAH_WIFI_NVS_DEFAULT_SSID);
      pref.putString("pass", MYNAH_WIFI_NVS_DEFAULT_PASS);
    }
  }

  const String s = pref.getString("ssid", "");
  const String p = pref.getString("pass", "");
  pref.end();

  strncpy(ssid, s.c_str(), ssid_sz - 1);
  ssid[ssid_sz - 1] = '\0';
  strncpy(pass, p.c_str(), pass_sz - 1);
  pass[pass_sz - 1] = '\0';
  return strlen(ssid) > 0;
}

bool pm_wifi_credentials_save(const char *ssid, const char *pass) {
  if (!ssid || ssid[0] == '\0' || strlen(ssid) > 63 || (pass && strlen(pass) > 63)) {
    return false;
  }
  Preferences pref;
  if (!pref.begin(kNvsNs, false)) {
    return false;
  }
  const size_t ssid_written = pref.putString("ssid", ssid);
  pref.putString("pass", pass ? pass : "");
  pref.end();
  return ssid_written > 0;
}

bool pm_wifi_credentials_clear(void) {
  Preferences pref;
  if (!pref.begin(kNvsNs, false)) {
    return false;
  }
  pref.remove("ssid");
  pref.remove("pass");
  pref.end();
  return true;
}
