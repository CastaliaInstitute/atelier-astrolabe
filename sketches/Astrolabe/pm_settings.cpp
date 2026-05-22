#include "pm_settings.h"

#include "faces/castalia/pm_face_castalia.h"
#include "faces/settings/pm_face_settings_aec.h"
#include "faces/settings/pm_face_settings_wifi.h"
#include "faces/shared/pm_face_draw.h"
#include "pin_config.h"
#include "pm_display.h"
#include "pm_faculty.h"

static SettingsPage s_page = SettingsPage::WiFi;

SettingsPage pm_settings_page(void) { return s_page; }

const char *pm_settings_page_name(SettingsPage page) {
  switch (page) {
    case SettingsPage::WiFi:
      return "wifi";
    case SettingsPage::Castalia:
      return "castalia";
    case SettingsPage::Tour:
      return "tour";
    case SettingsPage::Aec:
      return "aec";
    default:
      return "settings";
  }
}

void pm_settings_on_leave(void) {
  if (s_page == SettingsPage::Aec) {
    pm_face_settings_aec_leave();
  }
}

void pm_settings_set_page(SettingsPage page) {
  if (static_cast<unsigned>(page) >= static_cast<unsigned>(SettingsPage::kCount)) {
    return;
  }
  if (s_page == SettingsPage::Aec && page != SettingsPage::Aec) {
    pm_face_settings_aec_leave();
  }
  s_page = page;
}

void pm_settings_cycle(int delta) {
  const SettingsPage prev = s_page;
  int v = static_cast<int>(s_page) + delta;
  const int n = static_cast<int>(SettingsPage::kCount);
  v = (v % n + n) % n;
  s_page = static_cast<SettingsPage>(v);
  if (prev == SettingsPage::Aec && s_page != SettingsPage::Aec) {
    pm_face_settings_aec_leave();
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
  pm_face_draw_centered_line("swipe L/R", 430, c_dim, 1, 1);
}

static void pm_settings_draw_tour(void) {
  const uint16_t c_hi = pm_gfx->color565(220, 225, 245);
  const uint16_t c_dim = pm_gfx->color565(120, 128, 150);
  const uint16_t c_accent = pm_gfx->color565(170, 210, 255);

  pm_face_draw_centered_line("Tour", 56, c_hi, 2, 2);
  pm_face_draw_centered_line("tap plays intro", 112, c_accent, 1, 2);
  pm_face_draw_centered_line("long press resets played", 146, c_dim, 1, 1);
  pm_face_draw_centered_line("up/down selects guide", 174, c_dim, 1, 1);

  PmFacultyProfile faculty = {};
  if (pm_faculty_active(&faculty)) {
    pm_face_draw_centered_line("guide", 238, c_dim, 1, 1);
    pm_face_draw_centered_line(faculty.name, 268, c_hi, 1, 2);
  } else {
    pm_face_draw_centered_line("no guide selected", 252, c_dim, 1, 1);
  }

  pm_face_draw_centered_line("first boot plays once", 336, c_dim, 1, 1);
}

void pm_settings_draw(void) {
  switch (s_page) {
    case SettingsPage::WiFi:
      pm_face_settings_wifi_draw();
      break;
    case SettingsPage::Castalia:
      pm_face_castalia_draw();
      break;
    case SettingsPage::Tour:
      pm_settings_draw_tour();
      break;
    case SettingsPage::Aec:
      pm_face_settings_aec_draw();
      break;
    default:
      break;
  }
  pm_settings_draw_chrome();
}
