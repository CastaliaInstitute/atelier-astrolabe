#include "faces/settings/pm_face_settings_wifi.h"

#include <WiFi.h>
#include <cstdio>

#include "faces/shared/pm_face_draw.h"
#include "pm_display.h"
#include "pm_wifi_creds.h"
#include "pm_wifi_ntp.h"

static bool s_wifi_reconnecting = false;

void pm_face_settings_wifi_mark_reconnecting(void) { s_wifi_reconnecting = true; }

bool pm_face_settings_wifi_tap_reconnect(void) {
  s_wifi_reconnecting = false;
  return pm_wifi_reconnect();
}

void pm_face_settings_wifi_draw(void) {
  const uint16_t c_hi = pm_gfx->color565(210, 215, 235);
  const uint16_t c_dim = pm_gfx->color565(120, 128, 145);

  char ssid[64];
  char pass[80];
  const bool have_ssid = pm_wifi_credentials_load(ssid, sizeof(ssid), pass, sizeof(pass));

  pm_face_draw_centered_line("WiFi", 56, c_hi, 2, 2);
  if (have_ssid && ssid[0] != '\0') {
    pm_face_draw_centered_line(ssid, 118, c_hi, 1, 2);
  } else {
    pm_face_draw_centered_line("(no SSID saved)", 118, c_dim, 1, 1);
  }

  if (s_wifi_reconnecting) {
    pm_face_draw_centered_line("reconnecting…", 168, c_hi, 1, 2);
  } else if (pm_wifi_connected()) {
    pm_face_draw_centered_line("connected", 168, c_hi, 1, 2);
    const String ip = WiFi.localIP().toString();
    pm_face_draw_centered_line(ip.c_str(), 200, c_dim, 1, 1);
    char rssi_line[32];
    snprintf(rssi_line, sizeof(rssi_line), "%d dBm", WiFi.RSSI());
    pm_face_draw_centered_line(rssi_line, 228, c_dim, 1, 1);
  } else {
    pm_face_draw_centered_line("offline", 168, c_dim, 2, 2);
    pm_face_draw_centered_line("tap to reconnect", 210, c_dim, 1, 1);
  }

  pm_face_draw_centered_line("serial: wifi SSID pass", 360, c_dim, 1, 1);
}
