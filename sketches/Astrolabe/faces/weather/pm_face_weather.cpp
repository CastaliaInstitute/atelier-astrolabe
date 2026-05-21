#include "faces/weather/pm_face_weather.h"

#include "faces/shared/pm_circadian_hue.h"
#include "faces/shared/pm_face_draw.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include "pin_config.h"
#include "pm_display.h"

PmWeatherStatus g_weather_ui = {};

namespace {

constexpr int k_hours = 24;
constexpr int k_slices = 48;
constexpr float k_deg_per_slice = 360.f / static_cast<float>(k_slices);

enum class SkyIcon : uint8_t { Sun, Cloud, PartlyCloudy, Rain, Snow, Fog, Unknown };

float slice_start_deg(int slice) {
  return static_cast<float>(slice % k_slices) * k_deg_per_slice;
}

uint16_t blend565(uint16_t bg, uint16_t fg, float alpha) {
  if (alpha <= 0.f) {
    return bg;
  }
  if (alpha >= 1.f) {
    return fg;
  }
  const uint8_t br = static_cast<uint8_t>(((bg >> 11) & 0x1F) * 255 / 31);
  const uint8_t bg_g = static_cast<uint8_t>(((bg >> 5) & 0x3F) * 255 / 63);
  const uint8_t bb = static_cast<uint8_t>((bg & 0x1F) * 255 / 31);
  const uint8_t fr = static_cast<uint8_t>(((fg >> 11) & 0x1F) * 255 / 31);
  const uint8_t fg_g = static_cast<uint8_t>(((fg >> 5) & 0x3F) * 255 / 63);
  const uint8_t fb = static_cast<uint8_t>((fg & 0x1F) * 255 / 31);
  const float a = alpha;
  const float ia = 1.f - a;
  return pm_gfx->color565(static_cast<uint8_t>(br * ia + fr * a), static_cast<uint8_t>(bg_g * ia + fg_g * a),
                          static_cast<uint8_t>(bb * ia + fb * a));
}

uint16_t temp_color(int8_t temp_c) {
  const float t = static_cast<float>(temp_c);
  const float u = (t + 8.f) / 42.f;
  const float x = u < 0.f ? 0.f : (u > 1.f ? 1.f : u);
  const float hue = 220.f - 200.f * x;
  return pm_face_color565_from_hsv(pm_gfx, hue, 0.72f, 0.22f + 0.38f * x);
}

uint16_t humidity_color(const PmWeatherHour &h) {
  if (h.precip == PmWeatherPrecip::Snow) {
    return pm_gfx->color565(228, 238, 255);
  }
  if (h.precip == PmWeatherPrecip::Rain) {
    const uint8_t v = static_cast<uint8_t>(lrintf(55.f + static_cast<float>(h.humidity_pct) * 0.55f));
    return pm_gfx->color565(18, 52, v);
  }
  const float rh = static_cast<float>(h.humidity_pct) / 100.f;
  return pm_face_color565_from_hsv(pm_gfx, 195.f, 0.35f + 0.4f * rh, 0.12f + 0.2f * rh);
}

int hour_for_slice(int slice) { return (slice * k_hours) / k_slices; }

bool slice_is_before_now(int slice, int local_hour, int local_min) {
  const int slice_mid_min = (slice * 30) + 15;
  const int now_min = local_hour * 60 + local_min;
  return slice_mid_min < now_min;
}

void draw_slice_ring(int r_inner, int r_outer, uint16_t (*color_fn)(int hour), int highlight_hour) {
  const int cx = pm_face_lcd_cx;
  const int cy = pm_face_lcd_cy;
  const uint16_t c_track = pm_gfx->color565(8, 12, 22);
  pm_face_draw_annular_wedge(cx, cy, r_inner - 1, r_outer + 1, 0.f, 360.f, c_track);

  for (int s = 0; s < k_slices; ++s) {
    const int h = hour_for_slice(s);
    uint16_t col = color_fn(h);
    if (h == highlight_hour) {
      col = blend565(col, pm_gfx->color565(255, 252, 240), 0.42f);
    }
    const float d0 = slice_start_deg(s);
    const float d1 = d0 + k_deg_per_slice;
    pm_face_draw_annular_wedge(cx, cy, r_inner, r_outer, d0, d1, col);
  }

  for (int tick = 0; tick < k_hours; ++tick) {
    const float deg = slice_start_deg(tick * 2);
    const float ang = pm_face_deg_to_rad(deg);
    const int x0 = cx + static_cast<int>(lrintf(cosf(ang) * static_cast<float>(r_inner - 1)));
    const int y0 = cy + static_cast<int>(lrintf(sinf(ang) * static_cast<float>(r_inner - 1)));
    const int x1 = cx + static_cast<int>(lrintf(cosf(ang) * static_cast<float>(r_outer + 1)));
    const int y1 = cy + static_cast<int>(lrintf(sinf(ang) * static_cast<float>(r_outer + 1)));
    const uint16_t c_tick =
        (tick % 6 == 0) ? pm_gfx->color565(90, 100, 125) : pm_gfx->color565(35, 42, 58);
    pm_gfx->drawLine(x0, y0, x1, y1, c_tick);
  }

  pm_gfx->drawCircle(cx, cy, r_inner, pm_gfx->color565(28, 36, 52));
  pm_gfx->drawCircle(cx, cy, r_outer, pm_gfx->color565(45, 55, 75));
}

uint16_t temp_slice_color(int hour) { return temp_color(g_weather_ui.hourly[hour].temp_c); }
uint16_t hum_slice_color(int hour) { return humidity_color(g_weather_ui.hourly[hour]); }

void draw_24h_time_labels(bool time_valid, int local_hour, int local_min) {
  const int cx = pm_face_lcd_cx;
  const int cy = pm_face_lcd_cy;
  const int R = min(LCD_WIDTH, LCD_HEIGHT) / 2;
  const int r_label = R - 25;
  const uint16_t c_future = pm_gfx->color565(185, 198, 220);
  const uint16_t c_now = pm_gfx->color565(255, 252, 235);

  pm_gfx->setTextSize(1, 1);
  for (int h = 0; h < k_hours; ++h) {
    const float deg = slice_start_deg(h * 2);
    const float ang = pm_face_deg_to_rad(deg);
    char label[3];
    snprintf(label, sizeof(label), "%02d", h);
    int16_t x1, y1;
    uint16_t w, th;
    pm_gfx->getTextBounds(label, 0, 0, &x1, &y1, &w, &th);
    const int x = cx + static_cast<int>(lrintf(cosf(ang) * static_cast<float>(r_label))) - static_cast<int>(w) / 2;
    const int y = cy + static_cast<int>(lrintf(sinf(ang) * static_cast<float>(r_label))) - static_cast<int>(th) / 2;

    uint16_t col = c_future;
    if (time_valid) {
      if (h == local_hour) {
        col = c_now;
      }
    }
    pm_gfx->setTextColor(col);
    pm_gfx->setCursor(x, y);
    pm_gfx->print(label);
  }
}

SkyIcon icon_from_condition(const char *cond) {
  if (!cond || !cond[0]) {
    return SkyIcon::Unknown;
  }
  if (strstr(cond, "snow") || strstr(cond, "Snow") || strstr(cond, "flurr")) {
    return SkyIcon::Snow;
  }
  if (strstr(cond, "rain") || strstr(cond, "Rain") || strstr(cond, "shower") || strstr(cond, "drizzle")) {
    return SkyIcon::Rain;
  }
  if (strstr(cond, "fog") || strstr(cond, "Fog") || strstr(cond, "mist") || strstr(cond, "haze")) {
    return SkyIcon::Fog;
  }
  if (strstr(cond, "partly") || strstr(cond, "Partly")) {
    return SkyIcon::PartlyCloudy;
  }
  if (strstr(cond, "cloud") || strstr(cond, "Cloud") || strstr(cond, "overcast")) {
    return SkyIcon::Cloud;
  }
  if (strstr(cond, "clear") || strstr(cond, "Clear") || strstr(cond, "sun")) {
    return SkyIcon::Sun;
  }
  return SkyIcon::Unknown;
}

void draw_cloud_blob(int cx, int cy, int r, uint16_t col) {
  pm_gfx->fillCircle(cx - r / 2, cy, r, col);
  pm_gfx->fillCircle(cx + r / 2, cy, r, col);
  pm_gfx->fillCircle(cx, cy - r / 3, static_cast<int>(r * 0.85f), col);
}

void draw_weather_icon(SkyIcon icon, int cx, int cy, uint16_t col, uint16_t accent) {
  switch (icon) {
    case SkyIcon::Sun: {
      pm_gfx->fillCircle(cx, cy, 18, accent);
      for (int i = 0; i < 8; ++i) {
        const float ang = static_cast<float>(i) * (pm_face_k_two_pi / 8.f);
        const int x0 = cx + static_cast<int>(lrintf(cosf(ang) * 24.f));
        const int y0 = cy + static_cast<int>(lrintf(sinf(ang) * 24.f));
        const int x1 = cx + static_cast<int>(lrintf(cosf(ang) * 34.f));
        const int y1 = cy + static_cast<int>(lrintf(sinf(ang) * 34.f));
        pm_gfx->drawLine(x0, y0, x1, y1, accent);
      }
      break;
    }
    case SkyIcon::Cloud:
      draw_cloud_blob(cx, cy + 4, 20, col);
      break;
    case SkyIcon::PartlyCloudy:
      pm_gfx->fillCircle(cx - 14, cy - 10, 12, accent);
      for (int i = 0; i < 6; ++i) {
        const float ang = static_cast<float>(i) * (pm_face_k_two_pi / 6.f) - pm_face_k_pi * 0.5f;
        pm_gfx->drawLine(cx - 14 + static_cast<int>(lrintf(cosf(ang) * 16.f)),
                         cy - 10 + static_cast<int>(lrintf(sinf(ang) * 16.f)),
                         cx - 14 + static_cast<int>(lrintf(cosf(ang) * 22.f)),
                         cy - 10 + static_cast<int>(lrintf(sinf(ang) * 22.f)), accent);
      }
      draw_cloud_blob(cx + 6, cy + 8, 18, col);
      break;
    case SkyIcon::Rain:
      draw_cloud_blob(cx, cy - 6, 18, col);
      for (int i = -2; i <= 2; ++i) {
        const int x = cx + i * 9;
        pm_gfx->drawLine(x, cy + 12, x - 2, cy + 26, pm_gfx->color565(100, 170, 255));
        pm_gfx->drawLine(x + 1, cy + 12, x - 1, cy + 26, pm_gfx->color565(60, 130, 220));
      }
      break;
    case SkyIcon::Snow:
      draw_cloud_blob(cx, cy - 6, 18, col);
      for (int i = -2; i <= 2; ++i) {
        const int x = cx + i * 10;
        const int y = cy + 20;
        pm_gfx->drawLine(x, y - 4, x, y + 4, pm_gfx->color565(230, 240, 255));
        pm_gfx->drawLine(x - 4, y, x + 4, y, pm_gfx->color565(230, 240, 255));
      }
      break;
    case SkyIcon::Fog:
      for (int i = -2; i <= 2; ++i) {
        pm_gfx->drawFastHLine(cx - 26, cy - 8 + i * 7, 52, blend565(col, pm_gfx->color565(200, 210, 225), 0.5f));
      }
      break;
    default:
      draw_cloud_blob(cx, cy, 16, col);
      break;
  }
}

void draw_sky_backdrop(bool time_valid, int local_hour, int local_min) {
  const int cx = pm_face_lcd_cx;
  const int cy = pm_face_lcd_cy;
  float hour_f = static_cast<float>(local_hour) + static_cast<float>(local_min) / 60.f;
  if (!time_valid) {
    hour_f = 14.f;
  }
  const uint16_t sky = pm_circadian_color565_at_hour(hour_f);
  const uint16_t deep = blend565(sky, pm_gfx->color565(4, 8, 18), 0.82f);
  pm_gfx->fillScreen(deep);

  const uint16_t glow = blend565(sky, pm_gfx->color565(255, 255, 255), 0.08f);
  for (int r = 200; r >= 120; r -= 20) {
    pm_gfx->drawCircle(cx, cy, r, blend565(deep, glow, 0.06f));
  }
}

void truncate_condition(char *out, size_t cap, const char *src);

void draw_center_glass(int r_disk, int8_t temp_c, SkyIcon icon, const char *condition) {
  const int cx = pm_face_lcd_cx;
  const int cy = pm_face_lcd_cy;
  const uint16_t warm = temp_color(temp_c);
  const uint16_t c_core = blend565(pm_gfx->color565(12, 18, 32), warm, 0.22f);
  const uint16_t c_edge = pm_gfx->color565(70, 85, 110);

  for (int r = r_disk; r > 0; r -= 6) {
    const float t = static_cast<float>(r) / static_cast<float>(r_disk);
    pm_gfx->drawCircle(cx, cy, r, blend565(c_core, c_edge, t * 0.45f));
  }
  pm_gfx->fillCircle(cx, cy, r_disk, c_core);
  pm_gfx->drawCircle(cx, cy, r_disk, c_edge);
  pm_gfx->drawCircle(cx, cy, r_disk - 2, blend565(c_edge, pm_gfx->color565(120, 140, 170), 0.35f));

  const uint16_t c_icon = pm_gfx->color565(210, 220, 235);
  const uint16_t c_sun = pm_gfx->color565(255, 220, 120);
  draw_weather_icon(icon, cx, cy - 48, c_icon, c_sun);

  char cond[28];
  truncate_condition(cond, sizeof(cond), condition);
  pm_face_draw_centered_line(cond, cy - 10, pm_gfx->color565(176, 192, 218), 1, 1);

  char temp_line[12];
  snprintf(temp_line, sizeof(temp_line), "%d", static_cast<int>(temp_c));
  pm_gfx->setTextSize(4, 4);
  pm_gfx->setTextColor(pm_gfx->color565(250, 252, 255));
  int16_t x1, y1;
  uint16_t w, h;
  pm_gfx->getTextBounds(temp_line, 0, 0, &x1, &y1, &w, &h);
  pm_gfx->setCursor(cx - static_cast<int>(w) / 2 - 8, cy + 30);
  pm_gfx->print(temp_line);
  pm_gfx->setTextSize(2, 2);
  pm_gfx->setTextColor(blend565(pm_gfx->color565(250, 252, 255), warm, 0.4f));
  pm_gfx->setCursor(cx - static_cast<int>(w) / 2 + static_cast<int>(w) - 4, cy + 40);
  pm_gfx->print("C");
}

void draw_now_beacon(int r_inner, int r_outer, int local_hour, int local_min) {
  const float deg = slice_start_deg(local_hour * 2) + k_deg_per_slice * (static_cast<float>(local_min) / 60.f);
  const float ang = pm_face_deg_to_rad(deg);
  const int cx = pm_face_lcd_cx;
  const int cy = pm_face_lcd_cy;
  const uint16_t c_now = pm_gfx->color565(255, 250, 230);
  const int x0 = cx + static_cast<int>(lrintf(cosf(ang) * static_cast<float>(r_inner)));
  const int y0 = cy + static_cast<int>(lrintf(sinf(ang) * static_cast<float>(r_inner)));
  const int x1 = cx + static_cast<int>(lrintf(cosf(ang) * static_cast<float>(r_outer)));
  const int y1 = cy + static_cast<int>(lrintf(sinf(ang) * static_cast<float>(r_outer)));
  pm_gfx->drawLine(x0, y0, x1, y1, c_now);
  pm_gfx->drawLine(x0 + 1, y0, x1 + 1, y1, pm_gfx->color565(255, 255, 255));
  const int bx = cx + static_cast<int>(lrintf(cosf(ang) * static_cast<float>(r_outer + 3)));
  const int by = cy + static_cast<int>(lrintf(sinf(ang) * static_cast<float>(r_outer + 3)));
  pm_gfx->fillCircle(bx, by, 7, c_now);
  pm_gfx->drawCircle(bx, by, 10, pm_gfx->color565(255, 255, 255));
}

void draw_ring_legends(int r_hum_inner, int r_hum_outer, int r_temp_inner, int r_temp_outer) {
  const uint16_t c_dim = pm_gfx->color565(100, 115, 140);
  const float ang = pm_face_deg_to_rad(92.f);
  const int cx = pm_face_lcd_cx;
  const int cy = pm_face_lcd_cy;
  const int rh = (r_hum_inner + r_hum_outer) / 2;
  const int rt = (r_temp_inner + r_temp_outer) / 2;
  pm_gfx->setTextSize(1, 1);
  pm_gfx->setTextColor(c_dim);
  pm_gfx->setCursor(cx + static_cast<int>(lrintf(cosf(ang) * static_cast<float>(rh))) - 8,
                    cy + static_cast<int>(lrintf(sinf(ang) * static_cast<float>(rh))) - 4);
  pm_gfx->print("wet");
  pm_gfx->setCursor(cx + static_cast<int>(lrintf(cosf(ang) * static_cast<float>(rt))) - 8,
                    cy + static_cast<int>(lrintf(sinf(ang) * static_cast<float>(rt))) - 4);
  pm_gfx->print("temp");
}

void truncate_condition(char *out, size_t cap, const char *src) {
  if (!out || cap == 0) {
    return;
  }
  if (!src) {
    out[0] = '\0';
    return;
  }
  strncpy(out, src, cap - 1);
  out[cap - 1] = '\0';
  if (strlen(out) > 22) {
    out[21] = '.';
    out[22] = '.';
    out[23] = '\0';
  }
}

}  // namespace

void pm_face_weather_draw(bool time_valid, int local_hour, int local_min) {
  draw_sky_backdrop(time_valid, local_hour, local_min);

  if (!g_weather_ui.ok) {
    pm_face_draw_centered_line("weather", 210, pm_gfx->color565(180, 190, 210), 2, 2);
    pm_face_draw_centered_line(g_weather_ui.error[0] ? g_weather_ui.error : "no data", 250,
                               pm_gfx->color565(120, 135, 160), 1, 1);
    pm_face_draw_circumference_rainbow_24h(time_valid);
    return;
  }

  const int highlight = time_valid ? local_hour : -1;
  const int R = min(LCD_WIDTH, LCD_HEIGHT) / 2;
  const int r_rainbow_inner = R - 9;
  const int r_hum_outer = r_rainbow_inner - 5;
  const int r_hum_inner = r_hum_outer - 16;
  const int r_temp_outer = r_hum_inner - 5;
  const int r_temp_inner = r_temp_outer - 16;
  const int r_center = r_temp_inner - 12;

  draw_slice_ring(r_hum_inner, r_hum_outer, hum_slice_color, highlight);
  draw_slice_ring(r_temp_inner, r_temp_outer, temp_slice_color, highlight);
  draw_24h_time_labels(time_valid, local_hour, local_min);
  draw_ring_legends(r_hum_inner, r_hum_outer, r_temp_inner, r_temp_outer);

  if (time_valid) {
    draw_now_beacon(r_temp_inner - 3, r_hum_outer + 4, local_hour, local_min);
  }

  const SkyIcon icon = icon_from_condition(g_weather_ui.condition);
  draw_center_glass(r_center, g_weather_ui.current_temp_c, icon, g_weather_ui.condition);

  char band[28];
  snprintf(band, sizeof(band), "next 24h  H %d  L %d", static_cast<int>(g_weather_ui.hi_c),
           static_cast<int>(g_weather_ui.lo_c));
  pm_face_draw_centered_line(band, pm_face_lcd_cy + r_center + 32, pm_gfx->color565(110, 125, 150), 1, 1);

  if (g_weather_ui.location[0]) {
    pm_face_draw_centered_line(g_weather_ui.location, pm_face_lcd_cy + r_center + 48, pm_gfx->color565(85, 98, 120),
                               1, 1);
  }
  if (g_weather_ui.demo) {
    pm_face_draw_centered_line("demo", pm_face_lcd_cy + r_center + 62, pm_gfx->color565(70, 80, 95), 1, 1);
  }

  pm_face_draw_circumference_rainbow_24h(time_valid);
}
