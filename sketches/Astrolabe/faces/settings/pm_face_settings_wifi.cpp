#include "faces/settings/pm_face_settings_wifi.h"

#include <WiFi.h>

#include "faces/shared/pm_face_draw.h"
#include "pm_display.h"
#include "pm_wifi_creds.h"
#include "pm_wifi_ntp.h"

static bool s_attempt_in_progress = false;
static bool s_last_attempt_ok = false;
static uint32_t s_last_attempt_ms = 0;

void pm_face_settings_wifi_mark_reconnecting(void) {
  s_attempt_in_progress = true;
  s_last_attempt_ok = false;
  s_last_attempt_ms = millis();
}

bool pm_face_settings_wifi_tap_reconnect(void) {
  s_last_attempt_ok = pm_wifi_reconnect();
  s_attempt_in_progress = false;
  s_last_attempt_ms = millis();
  return s_last_attempt_ok;
}

void pm_face_settings_wifi_draw(void) {
  const uint16_t c_hi = pm_gfx->color565(210, 215, 235);
  const uint16_t c_dim = pm_gfx->color565(120, 128, 145);
  const uint16_t c_ok = pm_gfx->color565(120, 230, 170);
  const uint16_t c_bad = pm_gfx->color565(235, 130, 120);

  char ssid[64];
  char pass[80];
  const bool have_ssid = pm_wifi_credentials_load(ssid, sizeof(ssid), pass, sizeof(pass));

  pm_face_draw_centered_line("WiFi", 56, c_hi, 2, 2);
  if (have_ssid && ssid[0] != '\0') {
    pm_face_draw_centered_line(ssid, 118, c_hi, 1, 2);
  } else {
    pm_face_draw_centered_line("(no SSID saved)", 118, c_dim, 1, 1);
  }

  if (pm_wifi_connected()) {
    pm_face_draw_centered_line("connected", 168, c_hi, 1, 2);
    const String ip = WiFi.localIP().toString();
    pm_face_draw_centered_line(ip.c_str(), 200, c_dim, 1, 1);
    char rssi_line[32];
    snprintf(rssi_line, sizeof(rssi_line), "%d dBm", WiFi.RSSI());
    pm_face_draw_centered_line(rssi_line, 228, c_dim, 1, 1);
  } else {
    pm_face_draw_centered_line("offline", 168, c_dim, 2, 2);
    pm_face_draw_centered_line("checking network…", 210, c_dim, 1, 1);
  }

  if (s_last_attempt_ms != 0) {
    pm_face_draw_centered_line(s_attempt_in_progress ? "tap: reconnecting..." :
                               (s_last_attempt_ok ? "last tap: connected" : "last tap: failed"),
                               320, s_attempt_in_progress ? c_hi : (s_last_attempt_ok ? c_ok : c_bad), 1, 1);
  }

  pm_face_draw_centered_line("serial: wifi SSID pass", 360, c_dim, 1, 1);
}
