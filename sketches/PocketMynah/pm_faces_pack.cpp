#include "pm_faces_pack.h"

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <FS.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>

#include <ArduinoJson.h>

#include "pin_config.h"
#include "pm_partitions.h"
#include "pm_runtime_version.h"
#include "pm_security.h"
#include "pm_wifi_ntp.h"

static char s_last_err[96] = "";
static bool s_pack_ok = false;
static char s_layout_path[64] = "";

static void set_err(const char *msg) {
  if (!msg) {
    s_last_err[0] = '\0';
    return;
  }
  strncpy(s_last_err, msg, sizeof(s_last_err) - 1);
  s_last_err[sizeof(s_last_err) - 1] = '\0';
}

static uint16_t color_from_hsv(Arduino_GFX *gfx, float h_deg, float s, float v) {
  return gfx->color565(
      static_cast<uint8_t>(fmodf(h_deg / 60.f + 6.f, 6.f) < 1.f ? 255 : 0),
      static_cast<uint8_t>(128), static_cast<uint8_t>(v * 255));
  /** Use GFX color565FromHsv if linked from sketch — duplicate minimal path */
  return gfx->color565(static_cast<uint8_t>(v * 80), static_cast<uint8_t>(v * 60),
                       static_cast<uint8_t>(v * 200));
}

static void draw_hand(Arduino_GFX *gfx, int cx, int cy, float ang, int len, uint16_t col, int half_w) {
  if (len < 1) {
    return;
  }
  const float ux = cosf(ang);
  const float uy = sinf(ang);
  const float px = -uy;
  const float py = ux;
  const int x1 = cx + static_cast<int>(lrintf(ux * static_cast<float>(len)));
  const int y1 = cy + static_cast<int>(lrintf(uy * static_cast<float>(len)));
  for (int w = -half_w; w <= half_w; ++w) {
    const int ox = static_cast<int>(lrintf(px * static_cast<float>(w)));
    const int oy = static_cast<int>(lrintf(py * static_cast<float>(w)));
    gfx->drawLine(cx + ox, cy + oy, x1 + ox, y1 + oy, col);
  }
}

static void draw_hue_ring(Arduino_GFX *gfx, int cx, int cy, int r_outer, int r_inner, float hue_base) {
  constexpr int k_steps = 72;
  const int half_w = (r_outer - r_inner) / 2;
  for (int i = 0; i < k_steps; ++i) {
    const float a0 = (static_cast<float>(i) / k_steps) * 6.2831853f - 1.5707963f;
    const float a1 = (static_cast<float>(i + 1) / k_steps) * 6.2831853f - 1.5707963f;
    const float hue = fmodf(hue_base + static_cast<float>(i) * (360.f / k_steps), 360.f);
    const uint16_t col = color_from_hsv(gfx, hue, 0.75f, 0.14f);
    const int rm = (r_outer + r_inner) / 2;
    const int x0 = cx + static_cast<int>(lrintf(cosf(a0) * static_cast<float>(rm)));
    const int y0 = cy + static_cast<int>(lrintf(sinf(a0) * static_cast<float>(rm)));
    const int x1 = cx + static_cast<int>(lrintf(cosf(a1) * static_cast<float>(rm)));
    const int y1 = cy + static_cast<int>(lrintf(sinf(a1) * static_cast<float>(rm)));
    const float tx = -sinf(a0);
    const float ty = cosf(a0);
    for (int w = -half_w; w <= half_w; ++w) {
      const int ox = static_cast<int>(lrintf(tx * static_cast<float>(w)));
      const int oy = static_cast<int>(lrintf(ty * static_cast<float>(w)));
      gfx->drawLine(x0 + ox, y0 + oy, x1 + ox, y1 + oy, col);
    }
  }
}

bool pm_faces_pack_init(void) {
  s_pack_ok = false;
  s_layout_path[0] = '\0';
  if (!pm_partitions_faces_mounted()) {
    set_err("faces fs not mounted");
    return false;
  }
  fs::FS &fs = pm_partitions_faces_fs();
  if (!fs.exists("/registry.json")) {
    set_err("no registry.json");
    return false;
  }
  if (fs.exists("/manifest.json")) {
    if (pm_security_verify_manifest_signature("/manifest.json") != ESP_OK) {
      set_err("manifest invalid");
      return false;
    }
  }
  File reg = fs.open("/registry.json", "r");
  if (!reg) {
    set_err("registry open fail");
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, reg)) {
    reg.close();
    set_err("registry parse fail");
    return false;
  }
  reg.close();
  const char *def = doc["default"] | "";
  const JsonArray faces = doc["faces"].as<JsonArray>();
  for (JsonObject face : faces) {
    const char *id = face["id"] | "";
    if (strcmp(id, def) == 0 || (def[0] == '\0' && id[0])) {
      const char *layout = face["layout"] | "";
      if (layout[0]) {
        snprintf(s_layout_path, sizeof(s_layout_path), "/%s", layout);
        if (s_layout_path[0] == '/' && s_layout_path[1] == '/') {
          memmove(s_layout_path, s_layout_path + 1, sizeof(s_layout_path) - 1);
        }
        s_pack_ok = true;
        return true;
      }
    }
  }
  set_err("no default face in registry");
  return false;
}

const char *pm_faces_pack_last_error(void) { return s_last_err; }
bool pm_faces_pack_available(void) { return s_pack_ok; }
bool pm_faces_pack_use_pack_face(void) { return s_pack_ok; }

esp_err_t pm_faces_pack_validate_registry(void) {
  return s_pack_ok ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t pm_faces_pack_dry_run(void) {
  if (!s_pack_ok) {
    return ESP_ERR_INVALID_STATE;
  }
  return ESP_OK;
}

void pm_faces_pack_render(Arduino_GFX *gfx, float /*thinking_progress*/) {
  if (!gfx || !s_pack_ok) {
    return;
  }
  struct tm tm = {};
  int sec_of_day = 0;
  if (pm_time_valid()) {
    pm_time_local(&tm);
    sec_of_day = tm.tm_hour * 3600 + tm.tm_min * 60 + tm.tm_sec;
  }
  const float hue =
      pm_time_valid() ? static_cast<float>(sec_of_day) * (360.f / 86400.f)
                      : fmodf(static_cast<float>(millis()) * 0.0015f, 360.f);
  const uint16_t bg = color_from_hsv(gfx, hue, 0.75f, 0.14f);
  gfx->fillScreen(bg);

  const int cx = LCD_WIDTH / 2;
  const int cy = LCD_HEIGHT / 2;

  File layout = pm_partitions_faces_fs().open(s_layout_path, "r");
  if (!layout) {
    gfx->setCursor(40, cy);
    gfx->setTextColor(gfx->color565(255, 255, 255));
    gfx->print("Hue pack");
    draw_hue_ring(gfx, cx, cy, 118, 105, hue);
    if (pm_time_valid()) {
      const float h_ang = static_cast<float>(tm.tm_hour % 12) * 0.5235988f +
                          static_cast<float>(tm.tm_min) * 0.0087266f - 1.5707963f;
      const float m_ang = static_cast<float>(tm.tm_min) * 0.1047198f - 1.5707963f;
      draw_hand(gfx, cx, cy, h_ang, 70, gfx->color565(220, 220, 240), 3);
      draw_hand(gfx, cx, cy, m_ang, 95, gfx->color565(200, 200, 220), 2);
    }
    return;
  }
  JsonDocument doc;
  deserializeJson(doc, layout);
  layout.close();

  const JsonArray nodes = doc["nodes"].as<JsonArray>();
  for (JsonObject node : nodes) {
    const char *type = node["type"] | "";
    const int ncx = node["cx"] | cx;
    const int ncy = node["cy"] | cy;
    if (strcmp(type, "radial_hue_ring") == 0) {
      draw_hue_ring(gfx, ncx, ncy, node["r_outer"] | 118, node["r_inner"] | 105, hue);
    } else if (strcmp(type, "analog_hand") == 0) {
      const char *bind = node["bind"] | "";
      float ang = -1.5707963f;
      int len = 80;
      int hw = 2;
      if (strstr(bind, "hour")) {
        ang = static_cast<float>(tm.tm_hour % 12) * 0.5235988f + static_cast<float>(tm.tm_min) * 0.0087266f -
              1.5707963f;
        len = 70;
        hw = 3;
      } else if (strstr(bind, "minute")) {
        ang = static_cast<float>(tm.tm_min) * 0.1047198f - 1.5707963f;
        len = 95;
      }
      if (pm_time_valid()) {
        draw_hand(gfx, ncx, ncy, ang, len, gfx->color565(230, 230, 240), hw);
      }
    } else if (strcmp(type, "text") == 0) {
      gfx->setTextSize(1, 2);
      gfx->setTextColor(gfx->color565(220, 220, 230));
      gfx->setCursor(ncx - 40, ncy);
      gfx->print("Hue pack");
    }
  }
}

esp_err_t pm_faces_pack_mount_inactive_and_verify(const char *label) {
  if (!pm_partitions_mount_faces_label(label)) {
    return ESP_FAIL;
  }
  if (pm_security_verify_manifest_signature("/manifest.json") != ESP_OK) {
    return ESP_ERR_INVALID_STATE;
  }
  return pm_faces_pack_validate_registry();
}
