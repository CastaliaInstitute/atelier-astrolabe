#include "pm_settings.h"

#include <Arduino.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <cstdio>
#include <cstring>

#include "pm_wifi_ntp.h"

static char s_settings_url[96] = "";
static char s_host_label[40] = "";
static bool s_mdns_started = false;

static void ensure_mdns() {
  if (s_mdns_started || !pm_wifi_connected()) {
    return;
  }

  uint8_t mac[6] = {};
  WiFi.macAddress(mac);
  char host[24];
  snprintf(host, sizeof(host), "astrolabe-%02x%02x", mac[4], mac[5]);

  if (!MDNS.begin(host)) {
    return;
  }
  MDNS.addService("http", "tcp", 80);
  s_mdns_started = true;
  snprintf(s_host_label, sizeof(s_host_label), "%s.local", host);
}

void pm_settings_refresh_url(void) {
  s_settings_url[0] = '\0';
  if (!pm_wifi_connected()) {
    return;
  }

  ensure_mdns();
  if (s_host_label[0] != '\0' && strchr(s_host_label, '.') != nullptr) {
    snprintf(s_settings_url, sizeof(s_settings_url), "http://%s/settings", s_host_label);
    return;
  }

  const String ip = WiFi.localIP().toString();
  snprintf(s_host_label, sizeof(s_host_label), "%s", ip.c_str());
  snprintf(s_settings_url, sizeof(s_settings_url), "http://%s/settings", ip.c_str());
}

void pm_settings_note_wifi_changed(void) {
  if (s_mdns_started) {
    MDNS.end();
  }
  s_mdns_started = false;
  s_settings_url[0] = '\0';
  s_host_label[0] = '\0';
}

const char *pm_settings_url(void) { return s_settings_url; }

const char *pm_settings_host_label(void) { return s_host_label; }
