#include "faces/spotify/pm_face_spotify.h"
#include "faces/shared/pm_face_draw.h"
#include "pm_spotify.h"
#include "pm_wifi_ntp.h"
#include <cstdio>
#include <cstring>
#include "pin_config.h"
#include "pm_display.h"

PmSpotifyStatus g_spotify_ui = {};

void pm_face_spotify_copy_short_line(char *dst, size_t cap, const char *src) {
  if (!dst || cap < 4 || !src) {
    if (dst && cap) {
      dst[0] = '\0';
    }
    return;
  }
  strncpy(dst, src, cap - 1);
  dst[cap - 1] = '\0';
  const size_t n = strlen(dst);
  if (n >= cap - 1) {
    dst[cap - 4] = '.';
    dst[cap - 3] = '.';
    dst[cap - 2] = '.';
    dst[cap - 1] = '\0';
  }
}



bool pm_face_spotify_hit_transport_bar(int16_t x, int16_t y, int *zone_out) {
  if (!zone_out) {
    return false;
  }
  if (y < pm_face_spotify_bar_y || y > pm_face_spotify_bar_y + pm_face_spotify_bar_h) {
    return false;
  }
  const int bw = (LCD_WIDTH - 2 * pm_face_spotify_bar_pad - 16) / 3;
  const int x0 = pm_face_spotify_bar_pad;
  const int x1 = x0 + bw;
  const int gap = 8;
  const int x2 = x1 + gap;
  const int x3 = x2 + bw;
  const int x4 = x3 + gap;
  const int x5 = x4 + bw;
  if (x >= x0 && x < x1) {
    *zone_out = 0;
    return true;
  }
  if (x >= x2 && x < x3) {
    *zone_out = 1;
    return true;
  }
  if (x >= x4 && x < x5) {
    *zone_out = 2;
    return true;
  }
  return false;
}



void pm_face_spotify_draw() {
  const uint16_t c_spotify = pm_gfx->color565(29, 185, 84);
  const uint16_t c_txt = pm_gfx->color565(228, 228, 230);
  const uint16_t c_dim = pm_gfx->color565(130, 140, 148);
  const uint16_t c_btn_bg = pm_gfx->color565(36, 42, 48);
  const uint16_t c_btn_hi = pm_gfx->color565(52, 62, 72);

  pm_face_draw_centered_line("SPOTIFY", 76, c_spotify, 2, 2);
  pm_face_draw_centered_line("Connect", 104, c_dim, 1, 1);

  if (!pm_wifi_connected()) {
    pm_face_draw_centered_line("WiFi needed", 200, c_dim, 2, 2);
    pm_face_draw_centered_line("for transport", 232, c_dim, 1, 1);
    return;
  }

  if (g_spotify_ui.error[0] != '\0' && !g_spotify_ui.ok) {
    char line[48];
    pm_face_spotify_copy_short_line(line, sizeof(line), g_spotify_ui.error);
    pm_face_draw_centered_line(line, 136, pm_gfx->color565(255, 140, 120), 1, 1);
    pm_face_draw_centered_line("mynah-spotify fn", 160, c_dim, 1, 1);
    pm_face_draw_centered_line("+ Spotify secrets", 180, c_dim, 1, 1);
  } else {
    char ondev[80];
    if (g_spotify_ui.device[0] != '\0') {
      snprintf(ondev, sizeof(ondev), "On: %s", g_spotify_ui.device);
    } else {
      snprintf(ondev, sizeof(ondev), "%s", "On: (pick device in app)");
    }
    char dev_one[48];
    pm_face_spotify_copy_short_line(dev_one, sizeof(dev_one), ondev);
    pm_face_draw_centered_line(dev_one, 124, c_dim, 1, 1);

    char t1[44];
    char t2[44];
    pm_face_spotify_copy_short_line(t1, sizeof(t1), g_spotify_ui.track);
    pm_face_spotify_copy_short_line(t2, sizeof(t2), g_spotify_ui.artist);
    if (t1[0] == '\0') {
      strncpy(t1, "(no track)", sizeof(t1) - 1);
      t1[sizeof(t1) - 1] = '\0';
    }
    pm_face_draw_centered_line(t1, 148, c_txt, 1, 1);
    pm_face_draw_centered_line(t2, 170, c_dim, 1, 1);
  }

  const int bw = (LCD_WIDTH - 2 * pm_face_spotify_bar_pad - 16) / 3;
  const int yb = pm_face_spotify_bar_y;
  const int h = pm_face_spotify_bar_h;
  for (int z = 0; z < 3; ++z) {
    const int x = pm_face_spotify_bar_pad + z * (bw + 8);
    pm_gfx->fillRoundRect(x, yb, bw, h, 10, z == 1 ? c_btn_hi : c_btn_bg);
    pm_gfx->drawRoundRect(x, yb, bw, h, 10, c_spotify);
  }
  pm_gfx->setTextSize(2, 2);
  pm_gfx->setTextColor(c_spotify);
  pm_gfx->setCursor(pm_face_spotify_bar_pad + (bw - 12) / 2, yb + h / 2 - 8);
  pm_gfx->print("<");
  pm_gfx->setCursor(pm_face_spotify_bar_pad + (bw + 8) + (bw - 28) / 2, yb + h / 2 - 8);
  pm_gfx->print(g_spotify_ui.is_playing ? "||" : ">");
  pm_gfx->setCursor(pm_face_spotify_bar_pad + 2 * (bw + 8) + (bw - 28) / 2, yb + h / 2 - 8);
  pm_gfx->print(">>");

  pm_gfx->setTextSize(1, 1);
  pm_gfx->setTextColor(c_dim);
  pm_gfx->setCursor(pm_face_spotify_bar_pad + (bw - 30) / 2, yb + h - 2);
  pm_gfx->print("PREV");
  pm_gfx->setCursor(pm_face_spotify_bar_pad + (bw + 8) + (bw - 24) / 2, yb + h - 2);
  pm_gfx->print(g_spotify_ui.is_playing ? "STOP" : "PLAY");
  pm_gfx->setCursor(pm_face_spotify_bar_pad + 2 * (bw + 8) + (bw - 26) / 2, yb + h - 2);
  pm_gfx->print("NEXT");

  pm_face_draw_centered_line("controls active Connect device", 322, c_dim, 1, 1);
  pm_face_draw_centered_line("long press = refresh", 340, c_dim, 1, 1);
}


