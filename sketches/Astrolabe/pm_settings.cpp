#include "pm_settings.h"

#include "faces/castalia/pm_face_castalia.h"
#include "faces/settings/pm_face_settings_battery.h"
#include "faces/settings/pm_face_settings_ota.h"
#include "faces/settings/pm_face_settings_sleep.h"
#include "faces/settings/pm_face_settings_variant.h"
#include "faces/settings/pm_face_settings_wifi.h"
#include "faces/shared/pm_face_draw.h"
#include "pin_config.h"
#include "pm_display.h"

static SettingsPage s_page = SettingsPage::WiFi;

SettingsPage pm_settings_page(void) { return s_page; }

void pm_settings_set_page(SettingsPage page) {
  if (static_cast<unsigned>(page) >= static_cast<unsigned>(SettingsPage::kCount)) {
    return;
  }
  s_page = page;
}

void pm_settings_cycle(int delta) {
  int v = static_cast<int>(s_page) + delta;
  const int n = static_cast<int>(SettingsPage::kCount);
  v = (v % n + n) % n;
  s_page = static_cast<SettingsPage>(v);
}

const char *pm_settings_page_label(SettingsPage page) {
  switch (page) {
    case SettingsPage::WiFi:
      return "wifi";
    case SettingsPage::Battery:
      return "battery";
    case SettingsPage::Sleep:
      return "sleep";
    case SettingsPage::Variant:
      return "variant";
    case SettingsPage::Ota:
      return "ota";
    case SettingsPage::Castalia:
      return "castalia";
    default:
      return "settings";
  }
}

static void pm_settings_draw_chrome(void) {
  const uint16_t c_hi = pm_gfx->color565(200, 205, 225);
  const uint16_t c_dim = pm_gfx->color565(100, 108, 125);
  pm_face_draw_centered_line("SETTINGS", 18, c_hi, 1, 1);

  const int idx = static_cast<int>(s_page);
  const int n = static_cast<int>(SettingsPage::kCount);
  const int dot_y = 408;
  const int gap = 22;
  const int x0 = (LCD_WIDTH - (n - 1) * gap) / 2;
  for (int i = 0; i < n; ++i) {
    const int cx = x0 + i * gap;
    const uint16_t col = (i == idx) ? c_hi : c_dim;
    pm_gfx->fillCircle(cx, dot_y, i == idx ? 5 : 3, col);
  }
  pm_face_draw_centered_line("swipe ← →", 430, c_dim, 1, 1);
}

void pm_settings_draw(void) {
  switch (s_page) {
    case SettingsPage::WiFi:
      pm_face_settings_wifi_draw();
      break;
    case SettingsPage::Battery:
      pm_face_settings_battery_draw();
      break;
    case SettingsPage::Sleep:
      pm_face_settings_sleep_draw();
      break;
    case SettingsPage::Variant:
      pm_face_settings_variant_draw();
      break;
    case SettingsPage::Ota:
      pm_face_settings_ota_draw();
      break;
    case SettingsPage::Castalia:
      pm_face_castalia_draw();
      break;
    default:
      break;
  }
  pm_settings_draw_chrome();
}
