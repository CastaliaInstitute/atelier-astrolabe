#include "pm_wifi_creds.h"

#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

#include "pm_config.h"

static constexpr const char *kNvsNs = "mynah";

static void seed_defaults_if_empty(Preferences &pref) {
  if (pref.getString("ssid", "").length() > 0) {
    return;
  }
  if (strlen(MYNAH_WIFI_SSID) > 0) {
    pref.putString("ssid", MYNAH_WIFI_SSID);
    pref.putString("pass", MYNAH_WIFI_PASSWORD);
  } else {
    pref.putString("ssid", MYNAH_WIFI_NVS_DEFAULT_SSID);
    pref.putString("pass", MYNAH_WIFI_NVS_DEFAULT_PASS);
  }
}

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
    if (ssid[0] == '\0') {
      strncpy(ssid, MYNAH_WIFI_NVS_DEFAULT_SSID, ssid_sz - 1);
      ssid[ssid_sz - 1] = '\0';
      strncpy(pass, MYNAH_WIFI_NVS_DEFAULT_PASS, pass_sz - 1);
      pass[pass_sz - 1] = '\0';
    }
    return strlen(ssid) > 0;
  }

  seed_defaults_if_empty(pref);

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
  if (!ssid || !pass || ssid[0] == '\0') {
    return false;
  }
  Preferences pref;
  if (!pref.begin(kNvsNs, false)) {
    return false;
  }
  pref.putString("ssid", ssid);
  pref.putString("pass", pass);
  pref.end();
  return true;
}
