#include "faces/settings/pm_face_settings_ota.h"

#include <WiFi.h>

#include "faces/shared/pm_face_draw.h"
#include "pm_display.h"
#include "pm_screen_http.h"
#include "pm_variant.h"
#include "pm_wifi_ntp.h"

void pm_face_settings_ota_draw(void) {
  const uint16_t c_hi = pm_gfx->color565(210, 215, 235);
  const uint16_t c_dim = pm_gfx->color565(120, 128, 145);
  const uint16_t c_ok = pm_gfx->color565(120, 230, 170);
  const uint16_t c_warn = pm_gfx->color565(245, 190, 115);

  pm_face_draw_centered_line("OTA", 56, c_hi, 2, 2);
  pm_face_draw_centered_line(pm_variant_label(pm_variant_get()), 116, c_hi, 1, 2);

  char release_line[64];
  snprintf(release_line, sizeof(release_line), "%s / %s", pm_variant_device_platform(),
           pm_variant_ota_channel());
  pm_face_draw_centered_line(release_line, 150, c_dim, 1, 1);

  if (pm_wifi_connected()) {
    const String ip = WiFi.localIP().toString();
    char url[96];
    snprintf(url, sizeof(url), "http://%s/ota", ip.c_str());
    pm_face_draw_centered_line(url, 206, c_hi, 1, 1);

    char mdns[80];
    snprintf(mdns, sizeof(mdns), "%s.local/ota", pm_wifi_mdns_name());
    pm_face_draw_centered_line(mdns, 234, c_dim, 1, 1);
  } else {
    pm_face_draw_centered_line("WiFi offline", 214, c_warn, 1, 2);
  }

  const bool armed = pm_screen_http_ota_armed();
  pm_face_draw_centered_line(armed ? "upload armed" : "tap to arm upload", 296,
                             armed ? c_ok : c_hi, 1, 2);
  pm_face_draw_centered_line(pm_screen_http_ota_status(), 328, armed ? c_ok : c_dim, 1, 1);

  const size_t bytes = pm_screen_http_ota_bytes();
  if (bytes > 0) {
    char line[48];
    snprintf(line, sizeof(line), "%u KB received", static_cast<unsigned>(bytes / 1024u));
    pm_face_draw_centered_line(line, 358, c_dim, 1, 1);
  } else {
    pm_face_draw_centered_line("browser upload or integration", 358, c_dim, 1, 1);
  }
}
