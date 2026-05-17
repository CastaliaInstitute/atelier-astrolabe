#include "faces/castalia/pm_face_castalia.h"
#include "faces/shared/pm_face_draw.h"
#include "pm_castalia_auth.h"
#include "pm_wifi_ntp.h"
#include "pin_config.h"
#include "pm_display.h"

void pm_face_castalia_draw() {
  const uint16_t c_hi = pm_gfx->color565(210, 215, 235);
  const uint16_t c_dim = pm_gfx->color565(120, 128, 145);
  pm_face_draw_centered_line("CASTALIA", 40, c_hi, 2, 2);
  if (!pm_wifi_connected()) {
    pm_face_draw_centered_line("WiFi needed", 130, c_dim, 2, 2);
    return;
  }
  pm_face_draw_centered_line(pm_castalia_status_line(), 78, c_dim, 1, 1);
  if (pm_castalia_has_session()) {
    pm_face_draw_centered_line("Signed in", 200, c_hi, 1, 2);
    pm_face_draw_centered_line("voice / Spotify use JWT", 232, c_dim, 1, 1);
    return;
  }
  if (pm_castalia_signin_url_for_qr()[0] != '\0') {
    if (!pm_castalia_draw_qr(pm_gfx, LCD_WIDTH / 2, 238, 240)) {
      pm_face_draw_centered_line("QR encode fail", 220, c_dim, 1, 1);
    } else {
      pm_face_draw_centered_line("scan phone", 392, c_dim, 1, 1);
    }
  } else {
    pm_face_draw_centered_line("pairing...", 220, c_dim, 1, 1);
  }
}


