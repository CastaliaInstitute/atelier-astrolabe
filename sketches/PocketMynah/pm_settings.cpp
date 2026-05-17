#include "pm_settings.h"

#include <ESPmDNS.h>
#include <WiFi.h>
#include <cstdio>
#include <cstring>

#include "Arduino_GFX_Library.h"
#include "pm_qr.h"
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
    pm_qr_invalidate_cache();
    return;
  }
  ensure_mdns();
  if (s_host_label[0] != '\0') {
    snprintf(s_settings_url, sizeof(s_settings_url), "http://%s/settings", s_host_label);
  } else {
    snprintf(s_settings_url, sizeof(s_settings_url), "http://%s/settings", WiFi.localIP().toString().c_str());
    snprintf(s_host_label, sizeof(s_host_label), "%s", WiFi.localIP().toString().c_str());
  }
  pm_qr_invalidate_cache();
}

const char *pm_settings_url_for_qr() {
  return s_settings_url;
}

const char *pm_settings_host_label() {
  return s_host_label;
}

bool pm_settings_draw_qr(Arduino_Canvas *gfx, int cx, int cy, int max_px) {
  pm_settings_refresh_url();
  return pm_qr_draw_url(gfx, s_settings_url, cx, cy, max_px);
}
