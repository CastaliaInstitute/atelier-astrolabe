#include "faces/weather/pm_face_weather.h"

#include "faces/shared/pm_face_draw.h"
#include <cmath>
#include <cstdio>
#include "pin_config.h"
#include "pm_display.h"

PmWeatherStatus g_weather_ui = {};

namespace {

constexpr int k_hours = 24;
constexpr float k_deg_per_hour = 360.f / static_cast<float>(k_hours);

/** Civil hour 0..23 → degrees clockwise from top (midnight at top, matches 24h rainbow). */
float hour_start_deg(int hour) {
  return static_cast<float>(hour % k_hours) * k_deg_per_hour;
}

uint16_t temp_color(int8_t temp_c) {
  const float t = static_cast<float>(temp_c);
  const float u = (t + 10.f) / 45.f;
  const float x = u < 0.f ? 0.f : (u > 1.f ? 1.f : u);
  const uint8_t r = static_cast<uint8_t>(lrintf(40.f + 200.f * x));
  const uint8_t g = static_cast<uint8_t>(lrintf(70.f + 90.f * (1.f - fabsf(x - 0.55f) * 1.8f)));
  const uint8_t b = static_cast<uint8_t>(lrintf(140.f - 110.f * x));
  return pm_gfx->color565(r, g, b);
}

uint16_t humidity_color(const PmWeatherHour &h) {
  if (h.precip == PmWeatherPrecip::Snow) {
    return pm_gfx->color565(235, 242, 255);
  }
  if (h.precip == PmWeatherPrecip::Rain) {
    const uint8_t v = static_cast<uint8_t>(80 + h.humidity_pct / 2);
    return pm_gfx->color565(30, 70, v);
  }
  const uint8_t rh = h.humidity_pct;
  return pm_gfx->color565(static_cast<uint8_t>(40 + rh / 4), static_cast<uint8_t>(55 + rh / 3),
                          static_cast<uint8_t>(70 + rh / 2));
}

void draw_hour_ring(int r_inner, int r_outer, bool (*color_fn)(int hour, uint16_t *col)) {
  const int cx = pm_face_lcd_cx;
  const int cy = pm_face_lcd_cy;
  for (int h = 0; h < k_hours; ++h) {
    uint16_t col = 0;
    if (!color_fn(h, &col)) {
      continue;
    }
    const float d0 = hour_start_deg(h);
    const float d1 = d0 + k_deg_per_hour;
    pm_face_draw_annular_wedge(cx, cy, r_inner, r_outer, d0, d1, col);
  }
}

bool temp_slice_color(int hour, uint16_t *col) {
  *col = temp_color(g_weather_ui.hourly[hour].temp_c);
  return true;
}

bool humidity_slice_color(int hour, uint16_t *col) {
  *col = humidity_color(g_weather_ui.hourly[hour]);
  return true;
}

const char *condition_glyph(const char *cond) {
  if (!cond || !cond[0]) {
    return "~";
  }
  if (strstr(cond, "snow") || strstr(cond, "Snow")) {
    return "*";
  }
  if (strstr(cond, "rain") || strstr(cond, "Rain") || strstr(cond, "shower")) {
    return "/";
  }
  if (strstr(cond, "cloud") || strstr(cond, "Cloud") || strstr(cond, "overcast")) {
    return "=";
  }
  if (strstr(cond, "clear") || strstr(cond, "Clear") || strstr(cond, "sun")) {
    return "+";
  }
  return "~";
}

void draw_now_tick(int r_outer, int local_hour, int local_min) {
  const float deg = hour_start_deg(local_hour) + k_deg_per_hour * (static_cast<float>(local_min) / 60.f);
  const float ang = pm_face_deg_to_rad(deg);
  const int cx = pm_face_lcd_cx;
  const int cy = pm_face_lcd_cy;
  const int x0 = cx + static_cast<int>(lrintf(cosf(ang) * static_cast<float>(r_outer - 6)));
  const int y0 = cy + static_cast<int>(lrintf(sinf(ang) * static_cast<float>(r_outer - 6)));
  const int x1 = cx + static_cast<int>(lrintf(cosf(ang) * static_cast<float>(r_outer + 5)));
  const int y1 = cy + static_cast<int>(lrintf(sinf(ang) * static_cast<float>(r_outer + 5)));
  pm_gfx->drawLine(x0, y0, x1, y1, pm_gfx->color565(255, 250, 220));
  pm_gfx->fillCircle(x1, y1, 3, pm_gfx->color565(255, 250, 220));
}

}  // namespace

void pm_face_weather_draw(bool time_valid, int local_hour, int local_min) {
  const uint16_t c_bg = pm_gfx->color565(6, 10, 22);
  pm_gfx->fillScreen(c_bg);

  if (!g_weather_ui.ok) {
    pm_face_draw_centered_line("weather", 210, pm_gfx->color565(150, 160, 178), 2, 2);
    pm_face_draw_centered_line(g_weather_ui.error[0] ? g_weather_ui.error : "no data", 250,
                               pm_gfx->color565(120, 130, 150), 1, 1);
    pm_face_draw_circumference_rainbow_24h(time_valid);
    return;
  }

  const int R = min(LCD_WIDTH, LCD_HEIGHT) / 2;
  const int r_rainbow_inner = R - 9;
  const int r_hum_outer = r_rainbow_inner - 6;
  const int r_hum_inner = r_hum_outer - 14;
  const int r_temp_outer = r_hum_inner - 4;
  const int r_temp_inner = r_temp_outer - 14;
  const int r_center = r_temp_inner - 10;

  draw_hour_ring(r_temp_inner, r_temp_outer, temp_slice_color);
  draw_hour_ring(r_hum_inner, r_hum_outer, humidity_slice_color);

  if (time_valid) {
    draw_now_tick(r_hum_outer, local_hour, local_min);
  }

  const uint16_t c_disk = pm_gfx->color565(14, 20, 36);
  pm_gfx->fillCircle(pm_face_lcd_cx, pm_face_lcd_cy, r_center, c_disk);
  pm_gfx->drawCircle(pm_face_lcd_cx, pm_face_lcd_cy, r_center, pm_gfx->color565(50, 62, 88));

  char temp_line[16];
  snprintf(temp_line, sizeof(temp_line), "%d°", static_cast<int>(g_weather_ui.current_temp_c));
  pm_face_draw_centered_line(temp_line, pm_face_lcd_cy - 36, pm_gfx->color565(240, 245, 255), 3, 3);

  pm_gfx->setTextSize(4, 4);
  pm_gfx->setTextColor(pm_gfx->color565(180, 200, 230));
  const char *glyph = condition_glyph(g_weather_ui.condition);
  int16_t x1, y1;
  uint16_t w, h;
  pm_gfx->getTextBounds(glyph, 0, 0, &x1, &y1, &w, &h);
  pm_gfx->setCursor(pm_face_lcd_cx - static_cast<int>(w) / 2, pm_face_lcd_cy - static_cast<int>(h) / 2 - 4);
  pm_gfx->print(glyph);

  char sub[64];
  snprintf(sub, sizeof(sub), "%s", g_weather_ui.condition);
  pm_face_draw_centered_line(sub, pm_face_lcd_cy + 28, pm_gfx->color565(150, 165, 190), 1, 1);

  char band[32];
  snprintf(band, sizeof(band), "H%d  L%d", static_cast<int>(g_weather_ui.hi_c), static_cast<int>(g_weather_ui.lo_c));
  pm_face_draw_centered_line(band, pm_face_lcd_cy + 48, pm_gfx->color565(110, 125, 150), 1, 1);

  if (g_weather_ui.location[0]) {
    pm_face_draw_centered_line(g_weather_ui.location, pm_face_lcd_cy + 66, pm_gfx->color565(90, 100, 120), 1, 1);
  }
  if (g_weather_ui.demo) {
    pm_face_draw_centered_line("demo forecast", pm_face_lcd_cy + 82, pm_gfx->color565(80, 90, 110), 1, 1);
  }

  pm_face_draw_circumference_rainbow_24h(time_valid);
}
