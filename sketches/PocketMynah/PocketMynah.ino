// PocketMynah MVP: WiFi + NTP hue clock + hold-to-talk (Supabase voice-pipeline: STT / LLM / TTS).

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include <cstdio>
#include <cmath>
#include <ctime>
#include <cstring>

#include "esp_heap_caps.h"

#include "pin_config.h"
#include "pm_config.h"
#include "pm_gesture.h"
#include "pm_mic.h"
#include "pm_side_buttons.h"
#include "pm_speaker.h"
#include "pm_touch.h"
#include "pm_spotify.h"
#include "pm_voice.h"
#include "pm_wifi_ntp.h"
#include "pm_screen_http.h"
#include "pm_settings.h"
#include "pm_birth_nvs.h"
#include "pm_transit.h"
#include "pm_ephemeris.h"
#include "pm_castalia_auth.h"
#include "pm_calcifer.h"
#include "pm_astro_highlight.h"
#include "pm_diag.h"
#include "pm_face_safe.h"
#include "pm_faces_pack.h"
#include "pm_ota.h"
#include "pm_circadian_hue.h"
#include "pm_cycle_nvs.h"
#include "pm_moon.h"

Arduino_DataBus *bus = new Arduino_ESP32QSPI(
    LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);

Arduino_CO5300 *tft = new Arduino_CO5300(
    bus, LCD_RESET, 0, false, LCD_WIDTH, LCD_HEIGHT, 6, 0, 0, 0);
/** Full-framebuffer canvas; flush() pushes pixels to the CO5300 (enables WiFi BMP grab). */
Arduino_Canvas *gfx = new Arduino_Canvas(LCD_WIDTH, LCD_HEIGHT, tft);

enum class AppState { kClock, kRecording, kThinking, kPlaying };

static AppState g_state = AppState::kClock;
/** When true, `kThinking` calls `pm_voice_post_message` instead of PCM STT. */
static bool g_voice_use_message = false;
static constexpr uint8_t k_tv_none = 0;
static constexpr uint8_t k_tv_astro = 1;
static constexpr uint8_t k_tv_moon = 2;
static uint8_t g_text_voice_route = k_tv_none;
static bool g_calcifer_briefing = false;
static char g_moon_voice_msg[2200] = "";
static char g_moon_sys_prompt[640] = "";
static bool g_moon_voice_pcm = false;
/** Cached last TTS MP3 for BOOT replay (PSRAM). */
static uint8_t *g_last_play_mp3 = nullptr;
static size_t g_last_play_mp3_len = 0;
/** Reset [kPlaying] static arm state on next entry. */
static bool g_voice_play_reset = false;
static char g_astrology_voice_msg[2200] = "";
static char g_astrology_sys_prompt[2800] = "";
static bool s_rec_mic_on = false;
static uint32_t g_cycle_confirm_until_ms = 0;
/** Astrology voice: stay on chart during record/think/speak + highlight mentions. */
static bool g_astro_voice_active = false;
/** True when astro turn uses recorded PCM (PWR hold); false for BOOT tap text reading. */
static bool g_astro_voice_pcm = false;
static bool s_astro_voice_armed = false;
static bool s_astro_play_armed = false;
static PmAstroHighlightPlan g_astro_highlight_plan = {};
/** Set when entering clock UI so the face repaints after voice/recording states. */
static bool g_clock_repaint_pending = true;
static uint8_t *g_pcm = nullptr;
static size_t g_pcm_len = 0;
static PmVoiceResult g_voice_result = {};
static char g_gesture_banner[44] = "";
/** Last full clock paint background (for second-hand erasure). */
static uint16_t g_clock_bg565 = 0;
static int g_analog_saved_local_h = -1;
static int g_analog_saved_local_m = -1;

enum class ClockFace : uint8_t {
  ClassicAnalog = 0,
  Apocalypso,
  DigitalLocal,
  Spotify,
  Astrology,
  /** Lunar phase disk; PWR hold = ask, BOOT = spoken phase brief. */
  Moon,
  /** CalDAV block countdown via `calcifer-status`. */
  CalciferCountdown,
  /** On-device menstrual cycle ring; NVS only, low-text wellness glance. */
  Cycle,
  /** QR → castalia.institute Google sign-in; tokens stored on watch for Edge Functions. */
  Castalia,
  /** QR → on-device LAN settings page (`/settings`). */
  Settings,
  /** Declarative Hue clock from LittleFS face pack (when mounted). */
  HuePack,
  kNumFaces,
};

static ClockFace g_clock_face = ClockFace::ClassicAnalog;

static void cycle_clock_face(int delta) {
  int v = static_cast<int>(g_clock_face) + delta;
  const int n = static_cast<int>(ClockFace::kNumFaces);
  v = (v % n + n) % n;
  g_clock_face = static_cast<ClockFace>(v);
}

static PmSpotifyStatus g_spotify_ui = {};
static bool s_spotify_have_data = false;
static uint32_t s_last_spotify_poll_ms = 0;

static PmCalciferStatus g_calcifer_ui = {};
static bool s_calcifer_have_data = false;
static uint32_t s_last_calcifer_poll_ms = 0;

static PmTransitPositions g_astro_remote_tp = {};
static bool s_astro_remote_have = false;
static time_t s_astro_remote_epoch_min = -1;
static uint32_t s_astro_remote_retry_after_ms = 0;

#ifndef MYNAH_SPOTIFY_POLL_MS
#define MYNAH_SPOTIFY_POLL_MS 25000u
#endif

/** Spotify transport row (must match draw_spotify_face hit zones). */
static constexpr int kSpotifyBarY = 238;
static constexpr int kSpotifyBarH = 62;
static constexpr int kSpotifyBarPad = 20;

static void spotify_copy_short_line(char *dst, size_t cap, const char *src) {
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

static bool spotify_hit_transport_bar(int16_t x, int16_t y, int *zone_out) {
  if (!zone_out) {
    return false;
  }
  if (y < kSpotifyBarY || y > kSpotifyBarY + kSpotifyBarH) {
    return false;
  }
  const int bw = (LCD_WIDTH - 2 * kSpotifyBarPad - 16) / 3;
  const int x0 = kSpotifyBarPad;
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

static void draw_spotify_face() {
  const uint16_t c_spotify = gfx->color565(29, 185, 84);
  const uint16_t c_txt = gfx->color565(228, 228, 230);
  const uint16_t c_dim = gfx->color565(130, 140, 148);
  const uint16_t c_btn_bg = gfx->color565(36, 42, 48);
  const uint16_t c_btn_hi = gfx->color565(52, 62, 72);

  drawCenteredLine("SPOTIFY", 76, c_spotify, 2, 2);
  drawCenteredLine("Connect", 104, c_dim, 1, 1);

  if (!pm_wifi_connected()) {
    drawCenteredLine("WiFi needed", 200, c_dim, 2, 2);
    drawCenteredLine("for transport", 232, c_dim, 1, 1);
    return;
  }

  if (g_spotify_ui.error[0] != '\0' && !g_spotify_ui.ok) {
    char line[48];
    spotify_copy_short_line(line, sizeof(line), g_spotify_ui.error);
    drawCenteredLine(line, 136, gfx->color565(255, 140, 120), 1, 1);
    drawCenteredLine("mynah-spotify fn", 160, c_dim, 1, 1);
    drawCenteredLine("+ Spotify secrets", 180, c_dim, 1, 1);
  } else {
    char ondev[80];
    if (g_spotify_ui.device[0] != '\0') {
      snprintf(ondev, sizeof(ondev), "On: %s", g_spotify_ui.device);
    } else {
      snprintf(ondev, sizeof(ondev), "%s", "On: (pick device in app)");
    }
    char dev_one[48];
    spotify_copy_short_line(dev_one, sizeof(dev_one), ondev);
    drawCenteredLine(dev_one, 124, c_dim, 1, 1);

    char t1[44];
    char t2[44];
    spotify_copy_short_line(t1, sizeof(t1), g_spotify_ui.track);
    spotify_copy_short_line(t2, sizeof(t2), g_spotify_ui.artist);
    if (t1[0] == '\0') {
      strncpy(t1, "(no track)", sizeof(t1) - 1);
      t1[sizeof(t1) - 1] = '\0';
    }
    drawCenteredLine(t1, 148, c_txt, 1, 1);
    drawCenteredLine(t2, 170, c_dim, 1, 1);
  }

  const int bw = (LCD_WIDTH - 2 * kSpotifyBarPad - 16) / 3;
  const int yb = kSpotifyBarY;
  const int h = kSpotifyBarH;
  for (int z = 0; z < 3; ++z) {
    const int x = kSpotifyBarPad + z * (bw + 8);
    gfx->fillRoundRect(x, yb, bw, h, 10, z == 1 ? c_btn_hi : c_btn_bg);
    gfx->drawRoundRect(x, yb, bw, h, 10, c_spotify);
  }
  gfx->setTextSize(2, 2);
  gfx->setTextColor(c_spotify);
  gfx->setCursor(kSpotifyBarPad + (bw - 12) / 2, yb + h / 2 - 8);
  gfx->print("<");
  gfx->setCursor(kSpotifyBarPad + (bw + 8) + (bw - 28) / 2, yb + h / 2 - 8);
  gfx->print(g_spotify_ui.is_playing ? "||" : ">");
  gfx->setCursor(kSpotifyBarPad + 2 * (bw + 8) + (bw - 28) / 2, yb + h / 2 - 8);
  gfx->print(">>");

  gfx->setTextSize(1, 1);
  gfx->setTextColor(c_dim);
  gfx->setCursor(kSpotifyBarPad + (bw - 30) / 2, yb + h - 2);
  gfx->print("PREV");
  gfx->setCursor(kSpotifyBarPad + (bw + 8) + (bw - 24) / 2, yb + h - 2);
  gfx->print(g_spotify_ui.is_playing ? "STOP" : "PLAY");
  gfx->setCursor(kSpotifyBarPad + 2 * (bw + 8) + (bw - 26) / 2, yb + h - 2);
  gfx->print("NEXT");

  drawCenteredLine("controls active Connect device", 322, c_dim, 1, 1);
  drawCenteredLine("long press = refresh", 340, c_dim, 1, 1);
}

static const char *gesture_label(PmGestureKind k) {
  switch (k) {
    case PmGestureKind::Tap:
      return "tap";
    case PmGestureKind::DoubleTap:
      return "double tap";
    case PmGestureKind::TripleTap:
      return "triple tap";
    case PmGestureKind::SwipeUp:
      return "swipe up";
    case PmGestureKind::SwipeDown:
      return "swipe down";
    case PmGestureKind::SwipeLeft:
      return "swipe left";
    case PmGestureKind::SwipeRight:
      return "swipe right";
    case PmGestureKind::LongPress:
      return "long press";
    case PmGestureKind::MultiFingerTap2:
      return "2-finger tap";
    case PmGestureKind::MultiFingerTap3:
      return "3-finger tap";
    case PmGestureKind::MultiFingerTap4:
      return "4-finger tap";
    case PmGestureKind::MultiFingerTap5:
      return "5-finger tap";
    default:
      return "?";
  }
}

static uint16_t color565FromHsv(Arduino_GFX *out, float h_deg, float s, float v) {
  h_deg = fmodf(h_deg, 360.0f);
  if (h_deg < 0) {
    h_deg += 360.0f;
  }
  const float c = v * s;
  const float x = c * (1.0f - fabsf(fmodf(h_deg / 60.0f, 2.0f) - 1.0f));
  const float m = v - c;
  float rp = 0, gp = 0, bp = 0;
  if (h_deg < 60.0f) {
    rp = c;
    gp = x;
  } else if (h_deg < 120.0f) {
    rp = x;
    gp = c;
  } else if (h_deg < 180.0f) {
    gp = c;
    bp = x;
  } else if (h_deg < 240.0f) {
    gp = x;
    bp = c;
  } else if (h_deg < 300.0f) {
    rp = x;
    bp = c;
  } else {
    rp = c;
    bp = x;
  }
  const uint8_t r = static_cast<uint8_t>((rp + m) * 255.0f);
  const uint8_t gv = static_cast<uint8_t>((gp + m) * 255.0f);
  const uint8_t b = static_cast<uint8_t>((bp + m) * 255.0f);
  return out->color565(r, gv, b);
}

static void drawCenteredLine(const char *text, int y, uint16_t fg, uint8_t textSizeX, uint8_t textSizeY) {
  gfx->setTextSize(textSizeX, textSizeY);
  int16_t x1, y1;
  uint16_t w, h;
  gfx->getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  const int x = (LCD_WIDTH - static_cast<int>(w)) / 2;
  gfx->setCursor(x, y);
  gfx->setTextColor(fg);
  gfx->print(text);
}

static constexpr float kPi = 3.14159265f;
static constexpr float kTwoPi = kPi * 2.f;
/** Face background `color565FromHsv`; rainbow rim uses same S/V so brightness matches. */
static constexpr float k_clock_face_hsv_s = 0.75f;
static constexpr float k_clock_face_hsv_v = 0.14f;

static void draw_hand_radial(int cx, int cy, float ang, int len, uint16_t col, int half_w) {
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

static constexpr int kAnalogCx = LCD_WIDTH / 2;
static constexpr int kAnalogCy = LCD_HEIGHT / 2;
static constexpr int kAnalogR = 138;
static constexpr int kAnalogSecLen = kAnalogR - 10;

static void draw_analog_clock(uint16_t bg565, const struct tm *tm, bool valid) {
  const int cx = kAnalogCx;
  const int cy = kAnalogCy;
  const int r = kAnalogR;

  const uint16_t tick_major = gfx->color565(230, 232, 250);
  const uint16_t tick_minor = gfx->color565(120, 125, 150);
  for (int h = 0; h < 12; ++h) {
    const float ang = h * (kTwoPi / 12.f) - kPi * 0.5f;
    const bool major = (h % 3) == 0;
    const int r0 = r - 2;
    const int r1 = r - (major ? 14 : 8);
    const int x0 = cx + static_cast<int>(lrintf(cosf(ang) * static_cast<float>(r0)));
    const int y0 = cy + static_cast<int>(lrintf(sinf(ang) * static_cast<float>(r0)));
    const int x1 = cx + static_cast<int>(lrintf(cosf(ang) * static_cast<float>(r1)));
    const int y1 = cy + static_cast<int>(lrintf(sinf(ang) * static_cast<float>(r1)));
    gfx->drawLine(x0, y0, x1, y1, major ? tick_major : tick_minor);
  }

  float h_ang;
  float m_ang;
  float s_ang;
  if (valid) {
    const float hf =
        static_cast<float>(tm->tm_hour % 12) + static_cast<float>(tm->tm_min) / 60.f +
        static_cast<float>(tm->tm_sec) / 3600.f;
    h_ang = hf * (kTwoPi / 12.f) - kPi * 0.5f;
    m_ang =
        (static_cast<float>(tm->tm_min) + static_cast<float>(tm->tm_sec) / 60.f) * (kTwoPi / 60.f) -
        kPi * 0.5f;
    s_ang = static_cast<float>(tm->tm_sec) * (kTwoPi / 60.f) - kPi * 0.5f;
  } else {
    h_ang = m_ang = s_ang = -kPi * 0.5f;
  }

  const uint16_t c_hour = gfx->color565(210, 218, 255);
  const uint16_t c_min = RGB565_WHITE;
  const uint16_t c_sec = gfx->color565(255, 95, 95);

  draw_hand_radial(cx, cy, h_ang, r - 52, c_hour, 3);
  draw_hand_radial(cx, cy, m_ang, r - 22, c_min, 2);
  draw_hand_radial(cx, cy, s_ang, kAnalogSecLen, c_sec, 1);

  gfx->fillCircle(cx, cy, 7, c_hour);
  gfx->fillCircle(cx, cy, 3, bg565);
}

/** Apocalypso risk radar (12 axes, 5 rings) — matches apocalypso.castalia.institute RISK PROFILE widget. */
static void draw_label_at_polar(int rcx, int rcy, int r, float ang, const char *text, uint16_t col) {
  gfx->setTextSize(1, 1);
  gfx->setTextColor(col);
  int16_t x1, y1;
  uint16_t w, h;
  gfx->getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  const int tx = rcx + static_cast<int>(lrintf(cosf(ang) * static_cast<float>(r))) - static_cast<int>(w) / 2;
  const int ty = rcy + static_cast<int>(lrintf(sinf(ang) * static_cast<float>(r))) - static_cast<int>(h) / 2;
  gfx->setCursor(tx, ty);
  gfx->print(text);
}

static void draw_glyph_line(int cx, int cy, int x0, int y0, int x1, int y1, uint16_t col) {
  gfx->drawLine(cx + x0, cy + y0, cx + x1, cy + y1, col);
}

static void draw_glyph_cross(int cx, int cy, int y0, int y1, uint16_t col) {
  draw_glyph_line(cx, cy, 0, y0, 0, y1, col);
  draw_glyph_line(cx, cy, -4, (y0 + y1) / 2, 4, (y0 + y1) / 2, col);
}

static void draw_zodiac_glyph(int cx, int cy, int sign, uint16_t col, uint16_t bg) {
  switch (sign) {
    case 0:  // Aries
      draw_glyph_line(cx, cy, 0, 7, 0, -6, col);
      draw_glyph_line(cx, cy, 0, -6, -8, 4, col);
      draw_glyph_line(cx, cy, 0, -6, 8, 4, col);
      gfx->drawCircle(cx - 6, cy + 2, 4, col);
      gfx->drawCircle(cx + 6, cy + 2, 4, col);
      break;
    case 1:  // Taurus
      gfx->drawCircle(cx, cy + 3, 6, col);
      draw_glyph_line(cx, cy, -8, -6, -3, -1, col);
      draw_glyph_line(cx, cy, 8, -6, 3, -1, col);
      break;
    case 2:  // Gemini
      draw_glyph_line(cx, cy, -6, -8, -6, 8, col);
      draw_glyph_line(cx, cy, 6, -8, 6, 8, col);
      draw_glyph_line(cx, cy, -9, -7, 9, -7, col);
      draw_glyph_line(cx, cy, -9, 7, 9, 7, col);
      break;
    case 3:  // Cancer
      gfx->drawCircle(cx - 5, cy - 3, 4, col);
      gfx->drawCircle(cx + 5, cy + 3, 4, col);
      draw_glyph_line(cx, cy, -1, -6, 9, -6, col);
      draw_glyph_line(cx, cy, -9, 6, 1, 6, col);
      break;
    case 4:  // Leo
      gfx->drawCircle(cx - 4, cy + 3, 4, col);
      draw_glyph_line(cx, cy, 0, 1, 4, -7, col);
      draw_glyph_line(cx, cy, 4, -7, 9, -2, col);
      draw_glyph_line(cx, cy, 8, -1, 5, 8, col);
      break;
    case 5:  // Virgo
      draw_glyph_line(cx, cy, -8, -7, -8, 7, col);
      draw_glyph_line(cx, cy, -8, -3, -3, -7, col);
      draw_glyph_line(cx, cy, -3, -7, -3, 7, col);
      draw_glyph_line(cx, cy, -3, -3, 2, -7, col);
      draw_glyph_line(cx, cy, 2, -7, 2, 7, col);
      gfx->drawCircle(cx + 7, cy + 4, 4, col);
      break;
    case 6:  // Libra
      draw_glyph_line(cx, cy, -9, 7, 9, 7, col);
      draw_glyph_line(cx, cy, -9, 3, -3, 3, col);
      draw_glyph_line(cx, cy, 3, 3, 9, 3, col);
      gfx->drawCircle(cx, cy + 1, 4, col);
      break;
    case 7:  // Scorpio
      draw_glyph_line(cx, cy, -8, -7, -8, 7, col);
      draw_glyph_line(cx, cy, -8, -3, -3, -7, col);
      draw_glyph_line(cx, cy, -3, -7, -3, 7, col);
      draw_glyph_line(cx, cy, -3, -3, 2, -7, col);
      draw_glyph_line(cx, cy, 2, -7, 2, 6, col);
      draw_glyph_line(cx, cy, 2, 6, 9, 2, col);
      draw_glyph_line(cx, cy, 9, 2, 6, 1, col);
      draw_glyph_line(cx, cy, 9, 2, 8, 5, col);
      break;
    case 8:  // Sagittarius
      draw_glyph_line(cx, cy, -7, 7, 8, -8, col);
      draw_glyph_line(cx, cy, 8, -8, 7, 1, col);
      draw_glyph_line(cx, cy, 8, -8, -1, -7, col);
      draw_glyph_line(cx, cy, -5, -1, 2, 6, col);
      break;
    case 9:  // Capricorn
      draw_glyph_line(cx, cy, -8, -7, -4, 5, col);
      draw_glyph_line(cx, cy, -4, 5, 0, -7, col);
      draw_glyph_line(cx, cy, 0, -7, 0, 7, col);
      gfx->drawCircle(cx + 6, cy + 4, 4, col);
      break;
    case 10:  // Aquarius
      draw_glyph_line(cx, cy, -9, -3, -5, -6, col);
      draw_glyph_line(cx, cy, -5, -6, -1, -3, col);
      draw_glyph_line(cx, cy, -1, -3, 3, -6, col);
      draw_glyph_line(cx, cy, 3, -6, 9, -3, col);
      draw_glyph_line(cx, cy, -9, 5, -5, 2, col);
      draw_glyph_line(cx, cy, -5, 2, -1, 5, col);
      draw_glyph_line(cx, cy, -1, 5, 3, 2, col);
      draw_glyph_line(cx, cy, 3, 2, 9, 5, col);
      break;
    case 11:  // Pisces
      (void)bg;
      draw_glyph_line(cx, cy, -8, -8, -4, 0, col);
      draw_glyph_line(cx, cy, -4, 0, -8, 8, col);
      draw_glyph_line(cx, cy, 8, -8, 4, 0, col);
      draw_glyph_line(cx, cy, 4, 0, 8, 8, col);
      draw_glyph_line(cx, cy, -9, 0, 9, 0, col);
      break;
    default:
      break;
  }
}

static void draw_planet_glyph(PmEphemBody body, int cx, int cy, uint16_t col, uint16_t bg) {
  switch (body) {
    case kPmBodySun:
      gfx->drawCircle(cx, cy, 5, col);
      gfx->fillCircle(cx, cy, 1, col);
      break;
    case kPmBodyMoon:
      gfx->fillCircle(cx - 1, cy, 5, col);
      gfx->fillCircle(cx + 2, cy, 5, bg);
      gfx->drawCircle(cx - 1, cy, 5, col);
      break;
    case kPmBodyMercury:
      gfx->drawCircle(cx, cy, 4, col);
      gfx->drawCircle(cx, cy - 5, 3, col);
      draw_glyph_cross(cx, cy, 4, 9, col);
      break;
    case kPmBodyVenus:
      gfx->drawCircle(cx, cy - 2, 4, col);
      draw_glyph_cross(cx, cy, 2, 9, col);
      break;
    case kPmBodyMars:
      gfx->drawCircle(cx - 2, cy + 2, 4, col);
      draw_glyph_line(cx, cy, 2, -2, 8, -8, col);
      draw_glyph_line(cx, cy, 8, -8, 7, -2, col);
      draw_glyph_line(cx, cy, 8, -8, 2, -7, col);
      break;
    case kPmBodyJupiter:
      draw_glyph_line(cx, cy, -5, -4, 2, -4, col);
      draw_glyph_line(cx, cy, -1, -8, -1, 8, col);
      draw_glyph_line(cx, cy, -6, 2, 6, 2, col);
      draw_glyph_line(cx, cy, -5, -4, -7, 1, col);
      break;
    case kPmBodySaturn:
      draw_glyph_line(cx, cy, -3, -8, -3, 8, col);
      draw_glyph_line(cx, cy, -7, -4, 4, -4, col);
      gfx->drawCircle(cx + 4, cy + 4, 4, col);
      break;
    default:
      break;
  }
}

static void draw_apocalypso_face(const struct tm *tm, bool valid) {
  (void)tm;
  (void)valid;

  const uint16_t c_ring = gfx->color565(55, 65, 82);
  const uint16_t c_spoke = gfx->color565(72, 84, 102);
  const uint16_t c_fill = gfx->color565(55, 140, 215);
  const uint16_t c_outline = RGB565_WHITE;
  const uint16_t c_pct = gfx->color565(140, 148, 158);
  const uint16_t c_white = RGB565_WHITE;

  static const char *const k_lab[12] = {
      "Biblical", "Nuclear", "Bio",       "AI",      "Cyber",    "Infra",
      "Market",   "State",   "Epistemic", "Climate", "Biosphere", "Solar",
  };
  static const uint16_t k_col[12] = {
      gfx->color565(227, 179, 65),  gfx->color565(255, 123, 114), gfx->color565(86, 211, 100),
      gfx->color565(121, 192, 255), gfx->color565(188, 160, 220), gfx->color565(240, 136, 62),
      gfx->color565(227, 200, 80),  gfx->color565(255, 171, 145), gfx->color565(210, 168, 255),
      gfx->color565(86, 212, 220),  gfx->color565(63, 185, 80),   gfx->color565(242, 204, 96),
  };

  /** Demo profile (0..1 of outer ring); Climate peak, AI & Biblical elevated. */
  static const float k_risk[12] = {
      0.28f, 0.10f, 0.15f, 0.28f, 0.12f, 0.14f, 0.08f, 0.10f, 0.12f, 0.45f, 0.18f, 0.12f,
  };

  const int rcx = LCD_WIDTH / 2;
  const int rcy = LCD_HEIGHT / 2;
  const int rmax = 120;
  constexpr int k_axes = 12;

  char ttop[8];
  if (valid) {
    snprintf(ttop, sizeof(ttop), "%02d:%02d", tm->tm_hour, tm->tm_min);
  } else {
    snprintf(ttop, sizeof(ttop), "%s", "--:--");
  }
  const uint16_t c_green = gfx->color565(0x56, 0xd3, 0x64);
  drawCenteredLine(ttop, 66, c_green, 1, 1);
  drawCenteredLine("RISK PROFILE", 86, c_pct, 1, 1);

  for (int ring = 1; ring <= 5; ++ring) {
    const int rr = (rmax * ring) / 5;
    gfx->drawCircle(rcx, rcy, rr, c_ring);
  }

  for (int i = 0; i < k_axes; ++i) {
    const float ang = i * (kTwoPi / static_cast<float>(k_axes)) - kPi * 0.5f;
    const int xe = rcx + static_cast<int>(lrintf(cosf(ang) * static_cast<float>(rmax)));
    const int ye = rcy + static_cast<int>(lrintf(sinf(ang) * static_cast<float>(rmax)));
    gfx->drawLine(rcx, rcy, xe, ye, c_spoke);
  }

  int vx[12];
  int vy[12];
  for (int i = 0; i < k_axes; ++i) {
    const float ang = i * (kTwoPi / static_cast<float>(k_axes)) - kPi * 0.5f;
    const int ri = static_cast<int>(lrintf(static_cast<float>(rmax) * k_risk[i]));
    vx[i] = rcx + static_cast<int>(lrintf(cosf(ang) * static_cast<float>(ri)));
    vy[i] = rcy + static_cast<int>(lrintf(sinf(ang) * static_cast<float>(ri)));
  }

  for (int i = 0; i < k_axes; ++i) {
    const int j = (i + 1) % k_axes;
    gfx->fillTriangle(rcx, rcy, vx[i], vy[i], vx[j], vy[j], c_fill);
  }
  for (int i = 0; i < k_axes; ++i) {
    const int j = (i + 1) % k_axes;
    gfx->drawLine(vx[i], vy[i], vx[j], vy[j], c_outline);
  }

  gfx->drawLine(rcx, rcy, rcx, rcy - rmax, c_white);

  gfx->setTextSize(1, 1);
  gfx->setTextColor(c_pct);
  const char *pct[] = {"100%", "75%", "50%", "25%", "0%"};
  for (int p = 0; p < 5; ++p) {
    const int step = (rmax * (5 - p)) / 5;
    int16_t x1, y1;
    uint16_t w, h;
    gfx->getTextBounds(pct[p], 0, 0, &x1, &y1, &w, &h);
    gfx->setCursor(rcx - 36 - static_cast<int>(w), rcy - step - static_cast<int>(h) / 2);
    gfx->print(pct[p]);
  }

  const int r_lab = rmax + 14;
  for (int i = 0; i < k_axes; ++i) {
    const float ang = i * (kTwoPi / static_cast<float>(k_axes)) - kPi * 0.5f;
    draw_label_at_polar(rcx, rcy, r_lab, ang, k_lab[i], k_col[i]);
  }

  const float ang_imp = -kPi * 0.5f - (kTwoPi / static_cast<float>(k_axes)) * 0.5f;
  draw_label_at_polar(rcx, rcy, r_lab + 22, ang_imp, "Impact", c_pct);
}

static void draw_digital_local_face(const struct tm *tm, bool valid) {
  char line1[16];
  if (valid) {
    snprintf(line1, sizeof(line1), "%02d:%02d", tm->tm_hour, tm->tm_min);
  } else {
    snprintf(line1, sizeof(line1), "--:--");
  }
  drawCenteredLine(line1, 210, RGB565_WHITE, 5, 5);
}

static void format_calcifer_countdown(int64_t end_unix, char *out, size_t cap) {
  const time_t now = time(nullptr);
  int64_t left = end_unix - static_cast<int64_t>(now);
  if (left < 0) {
    left = 0;
  }
  const int h = static_cast<int>(left / 3600);
  const int m = static_cast<int>((left % 3600) / 60);
  const int s = static_cast<int>(left % 60);
  if (h > 0) {
    snprintf(out, cap, "%d:%02d:%02d", h, m, s);
  } else {
    snprintf(out, cap, "%02d:%02d", m, s);
  }
}

static void draw_calcifer_face() {
  const uint16_t c_title = gfx->color565(255, 190, 110);
  const uint16_t c_big = RGB565_WHITE;
  const uint16_t c_dim = gfx->color565(130, 140, 155);
  drawCenteredLine("schedule", 52, c_title, 1, 1);

  if (!pm_wifi_connected()) {
    drawCenteredLine("need WiFi", 220, c_dim, 2, 2);
    return;
  }
  if (!pm_time_valid()) {
    drawCenteredLine("need time", 220, c_dim, 2, 2);
    return;
  }
  if (!s_calcifer_have_data) {
    drawCenteredLine("loading…", 220, c_dim, 2, 2);
    return;
  }
  if (!g_calcifer_ui.ok) {
    drawCenteredLine(g_calcifer_ui.error[0] ? g_calcifer_ui.error : "unavailable", 220,
                     gfx->color565(255, 110, 110), 1, 1);
    return;
  }
  if (!g_calcifer_ui.configured) {
    drawCenteredLine("CalDAV not set", 210, c_dim, 1, 1);
    drawCenteredLine("on server", 240, c_dim, 1, 1);
    return;
  }

  char line[96];
  if (g_calcifer_ui.current.valid) {
    const time_t now_sec = time(nullptr);
    const int64_t left = g_calcifer_ui.current.end_unix - static_cast<int64_t>(now_sec);
    const bool urgent = left > 0 && left < 300;
    drawCenteredLine(g_calcifer_ui.current.summary, 150, c_dim, 1, 1);
    format_calcifer_countdown(g_calcifer_ui.current.end_unix, line, sizeof(line));
    drawCenteredLine(line, 220, urgent ? gfx->color565(255, 95, 85) : c_big, 3, 3);
    drawCenteredLine(urgent ? "ending soon" : "left in block", 286, urgent ? gfx->color565(255, 150, 100) : c_dim, 1, 1);
  } else {
    drawCenteredLine("free", 200, c_big, 2, 2);
    if (g_calcifer_ui.next.valid) {
      snprintf(line, sizeof(line), "next: %s", g_calcifer_ui.next.summary);
      drawCenteredLine(line, 260, c_dim, 1, 1);
    }
  }
}

static const char *moon_phase_name_from_elong_deg(double el_deg) {
  int oct = static_cast<int>(el_deg / 45.0) % 8;
  if (oct < 0) {
    oct += 8;
  }
  static const char *const k[] = {"New moon",      "Waxing crescent", "First quarter", "Waxing gibbous",
                                  "Full moon",     "Waning gibbous",  "Last quarter",  "Waning crescent"};
  return k[oct];
}

static bool moon_illum_waxing_from_tp(const PmTransitPositions *tp, float *illum, bool *waxing) {
  if (!tp || !tp->ok || !illum || !waxing) {
    return false;
  }
  double el = tp->lon[kPmBodyMoon] - tp->lon[kPmBodySun];
  while (el < 0) {
    el += 360.0;
  }
  while (el >= 360.0) {
    el -= 360.0;
  }
  *waxing = el < 180.0;
  const float rad = static_cast<float>(el * (static_cast<double>(kPi) / 180.0));
  *illum = (1.f - cosf(rad)) * 0.5f;
  return true;
}

static void draw_moon_face(const struct tm *tm_local, bool valid_local) {
  const uint16_t c_dim = gfx->color565(150, 160, 178);
  if (!valid_local) {
    drawCenteredLine("need NTP time", 220, c_dim, 2, 2);
    return;
  }
  struct tm utc = {};
  pm_time_utc(&utc);
  PmTransitPositions tp = {};
  pm_transit_compute_utc(&utc, &tp);
  float illum = 0.5f;
  bool waxing = true;
  if (!moon_illum_waxing_from_tp(&tp, &illum, &waxing)) {
    drawCenteredLine("ephemeris", 220, c_dim, 2, 2);
    return;
  }
  const int cx = LCD_WIDTH / 2;
  const int cy = LCD_HEIGHT / 2;
  const int R = min(LCD_WIDTH, LCD_HEIGHT) / 2;
  const int r = R - 14;
  pm_moon_draw_disk(gfx, cx, cy, r, illum, waxing);
  (void)tm_local;
}

static bool build_moon_voice_message(char *buf, size_t cap) {
  if (!buf || cap < 200 || !pm_wifi_connected() || !pm_time_valid()) {
    return false;
  }
  struct tm loc = {};
  pm_time_local(&loc);
  struct tm utc = {};
  pm_time_utc(&utc);
  PmTransitPositions tp = {};
  pm_transit_compute_utc(&utc, &tp);
  float illum = 0.5f;
  bool wax = true;
  if (!moon_illum_waxing_from_tp(&tp, &illum, &wax)) {
    return false;
  }
  double el = tp.lon[kPmBodyMoon] - tp.lon[kPmBodySun];
  while (el < 0) {
    el += 360.0;
  }
  while (el >= 360.0) {
    el -= 360.0;
  }
  const char *nm = moon_phase_name_from_elong_deg(el);
  const int n = snprintf(
      buf, cap,
      "Pocket Mynah round watch. Local %04d-%02d-%02d %02d:%02d. Sun-Moon elongation ~%.0f deg "
      "(illum ~%d%%, %s). Phase: \"%s\". In 3-5 short spoken sentences: name the phase, brief geometry, "
      "a poetic note. No medical, legal, or fortune-telling advice.",
      loc.tm_year + 1900, loc.tm_mon + 1, loc.tm_mday, loc.tm_hour, loc.tm_min, el,
      static_cast<int>(lrintf(illum * 100.f)), wax ? "waxing" : "waning", nm);
  return n > 80 && static_cast<size_t>(n) < cap;
}

static bool build_moon_system_prompt() {
  if (!pm_time_valid()) {
    return false;
  }
  struct tm utc = {};
  pm_time_utc(&utc);
  PmTransitPositions tp = {};
  pm_transit_compute_utc(&utc, &tp);
  float illum = 0.5f;
  bool wax = true;
  if (!moon_illum_waxing_from_tp(&tp, &illum, &wax)) {
    return false;
  }
  double el = tp.lon[kPmBodyMoon] - tp.lon[kPmBodySun];
  while (el < 0) {
    el += 360.0;
  }
  while (el >= 360.0) {
    el -= 360.0;
  }
  const char *nm = moon_phase_name_from_elong_deg(el);
  snprintf(g_moon_sys_prompt, sizeof(g_moon_sys_prompt),
           "Moon context: %s, %d%% illuminated, %s. Answer the user's spoken question briefly for audio.",
           nm, static_cast<int>(lrintf(illum * 100.f)), wax ? "waxing" : "waning");
  return g_moon_sys_prompt[0] != '\0';
}

static const char *zodiac_abbr_from_lon(double lon_deg) {
  static const char *const kZ[12] = {"Ar", "Ta", "Ge", "Cn", "Le", "Vi",
                                     "Li", "Sc", "Sg", "Cp", "Aq", "Pi"};
  double x = fmod(lon_deg, 360.0);
  if (x < 0) {
    x += 360.0;
  }
  const int idx = static_cast<int>(x / 30.0) % 12;
  return kZ[idx];
}

static bool astro_remote_positions_for_epoch(time_t epoch, PmTransitPositions *out) {
  if (!out || !s_astro_remote_have || !g_astro_remote_tp.ok || epoch <= 0) {
    return false;
  }
  if ((epoch / 60) != s_astro_remote_epoch_min) {
    return false;
  }
  *out = g_astro_remote_tp;
  return true;
}

static void compute_astrology_positions_utc(const struct tm *utc, time_t epoch, PmTransitPositions *out,
                                            bool *remote_out) {
  if (remote_out) {
    *remote_out = false;
  }
  if (!out) {
    return;
  }
  if (astro_remote_positions_for_epoch(epoch, out)) {
    if (remote_out) {
      *remote_out = true;
    }
    return;
  }
  pm_transit_compute_utc(utc, out);
}

static float astro_angle_from_lon(double lon_deg) {
  return static_cast<float>(kPi + lon_deg * (kPi / 180.0f));
}

static double angle_delta_deg(double a, double b) {
  double d = fabs(a - b);
  while (d >= 360.0) {
    d -= 360.0;
  }
  if (d > 180.0) {
    d = 360.0 - d;
  }
  return d;
}

static bool match_aspect(double delta, int *aspect_out) {
  static const int k_aspects[] = {60, 90, 120, 180};
  for (unsigned i = 0; i < sizeof(k_aspects) / sizeof(k_aspects[0]); ++i) {
    if (fabs(delta - static_cast<double>(k_aspects[i])) <= 4.0) {
      if (aspect_out) {
        *aspect_out = k_aspects[i];
      }
      return true;
    }
  }
  return false;
}

static void draw_astrology_aspects(const PmTransitPositions *tp, int cx, int cy, int r) {
  if (!MYNAH_ASTROLOGY_ASPECT_LINES || !tp || !tp->ok) {
    return;
  }
  for (int a = 0; a < kPmBodyCount; ++a) {
    for (int b = a + 1; b < kPmBodyCount; ++b) {
      int aspect = 0;
      if (!match_aspect(angle_delta_deg(tp->lon[a], tp->lon[b]), &aspect)) {
        continue;
      }
      uint16_t col = gfx->color565(68, 92, 120);
      if (aspect == 90) {
        col = gfx->color565(110, 72, 92);
      } else if (aspect == 120) {
        col = gfx->color565(70, 108, 100);
      } else if (aspect == 180) {
        col = gfx->color565(105, 88, 130);
      }
      const float aa = astro_angle_from_lon(tp->lon[a]);
      const float ab = astro_angle_from_lon(tp->lon[b]);
      const int ax = cx + static_cast<int>(lrintf(cosf(aa) * static_cast<float>(r)));
      const int ay = cy + static_cast<int>(lrintf(sinf(aa) * static_cast<float>(r)));
      const int bx = cx + static_cast<int>(lrintf(cosf(ab) * static_cast<float>(r)));
      const int by = cy + static_cast<int>(lrintf(sinf(ab) * static_cast<float>(r)));
      gfx->drawLine(ax, ay, bx, by, col);
    }
  }
}

static void draw_astrology_face(const struct tm *tm_local, bool valid_local, int highlight_body,
                                int highlight_sign, bool pulse_chart) {
  const uint16_t c_dim = gfx->color565(130, 140, 158);
  const uint16_t c_ring = gfx->color565(55, 62, 78);
  const uint16_t c_spoke = gfx->color565(78, 88, 108);
  const uint16_t c_lbl = gfx->color565(170, 178, 195);

  struct tm utc = {};
  PmTransitPositions tp = {};
  bool remote_tp = false;
  const time_t epoch_now = valid_local ? time(nullptr) : 0;
  if (valid_local) {
    pm_time_utc(&utc);
    compute_astrology_positions_utc(&utc, epoch_now, &tp, &remote_tp);
  }

  PmBirthSpec birth = {};
  (void)pm_birth_load(&birth);

  const int cx = LCD_WIDTH / 2;
  const int cy = LCD_HEIGHT / 2;
  const int R = min(LCD_WIDTH, LCD_HEIGHT) / 2;
  /** Chart fills the dial inside the 24h rainbow rim (rainbow inner ≈ R−9). */
  const int r_outer = R - 12;
  const int r_in = r_outer * 42 / 118;
  const int r_lab = r_outer - 18;
  const int r_aspect = r_in + (r_outer - r_in) * 52 / 100;
  /** Bodies in the annulus between aspect chords and sign glyphs (~10px clearance each side). */
  const int r_body = r_aspect + (r_lab - r_aspect) * 2 / 5;

  if (!tp.ok) {
    drawCenteredLine("ephemeris needs", 200, c_dim, 1, 1);
    drawCenteredLine("valid UTC time", 222, c_dim, 1, 1);
  } else {
    for (int s = 0; s < 12; ++s) {
      const float a0 = static_cast<float>(s) * (kTwoPi / 12.f) - kPi * 0.5f;
      const float a1 = static_cast<float>(s + 1) * (kTwoPi / 12.f) - kPi * 0.5f;
      const int x0 = cx + static_cast<int>(lrintf(cosf(a0) * static_cast<float>(r_outer)));
      const int y0 = cy + static_cast<int>(lrintf(sinf(a0) * static_cast<float>(r_outer)));
      const int x1 = cx + static_cast<int>(lrintf(cosf(a1) * static_cast<float>(r_outer)));
      const int y1 = cy + static_cast<int>(lrintf(sinf(a1) * static_cast<float>(r_outer)));
      gfx->drawLine(x0, y0, x1, y1, c_spoke);
      gfx->drawLine(cx, cy, x0, y0, c_ring);
    }
    gfx->drawCircle(cx, cy, r_outer, c_ring);
    gfx->drawCircle(cx, cy, r_in, c_ring);
    draw_astrology_aspects(&tp, cx, cy, r_aspect);

    if (highlight_sign >= 0 && highlight_sign < 12) {
      const uint16_t c_hi = gfx->color565(72, 82, 118);
      const float a0 = static_cast<float>(highlight_sign) * (kTwoPi / 12.f) - kPi * 0.5f;
      const float a1 = static_cast<float>(highlight_sign + 1) * (kTwoPi / 12.f) - kPi * 0.5f;
      constexpr int k_fan = 10;
      for (int step = 0; step < k_fan; ++step) {
        const float t0 = a0 + (a1 - a0) * (static_cast<float>(step) / static_cast<float>(k_fan));
        const float t1 = a0 + (a1 - a0) * (static_cast<float>(step + 1) / static_cast<float>(k_fan));
        const int x0 = cx + static_cast<int>(lrintf(cosf(t0) * static_cast<float>(r_outer)));
        const int y0 = cy + static_cast<int>(lrintf(sinf(t0) * static_cast<float>(r_outer)));
        const int x1 = cx + static_cast<int>(lrintf(cosf(t1) * static_cast<float>(r_outer)));
        const int y1 = cy + static_cast<int>(lrintf(sinf(t1) * static_cast<float>(r_outer)));
        gfx->fillTriangle(cx, cy, x0, y0, x1, y1, c_hi);
      }
      gfx->drawLine(cx, cy, cx + static_cast<int>(lrintf(cosf(a0) * static_cast<float>(r_outer))),
                    cy + static_cast<int>(lrintf(sinf(a0) * static_cast<float>(r_outer))),
                    gfx->color565(200, 210, 240));
      gfx->drawLine(cx, cy, cx + static_cast<int>(lrintf(cosf(a1) * static_cast<float>(r_outer))),
                    cy + static_cast<int>(lrintf(sinf(a1) * static_cast<float>(r_outer))),
                    gfx->color565(200, 210, 240));
    }

    for (int s = 0; s < 12; ++s) {
      const float amid = (static_cast<float>(s) + 0.5f) * (kTwoPi / 12.f) - kPi * 0.5f;
      const uint16_t lbl_col =
          (highlight_sign == s) ? gfx->color565(255, 250, 200) : c_lbl;
      const int lx = cx + static_cast<int>(lrintf(cosf(amid) * static_cast<float>(r_lab)));
      const int ly = cy + static_cast<int>(lrintf(sinf(amid) * static_cast<float>(r_lab)));
      draw_zodiac_glyph(lx, ly, s, lbl_col, gfx->color565(12, 14, 22));
    }

    static const uint16_t k_body_col[kPmBodyCount] = {
        gfx->color565(255, 210, 90),  gfx->color565(200, 210, 230), gfx->color565(180, 180, 190),
        gfx->color565(255, 190, 140), gfx->color565(230, 90, 70),   gfx->color565(220, 180, 120),
        gfx->color565(190, 170, 140),
    };
    const bool pulse_on = pulse_chart && ((millis() / 500u) % 2u) == 0u;
    for (int bi = 0; bi < kPmBodyCount; ++bi) {
      const double lon = tp.lon[bi];
      const float ang = astro_angle_from_lon(lon);
      const int px = cx + static_cast<int>(lrintf(cosf(ang) * static_cast<float>(r_body)));
      const int py = cy + static_cast<int>(lrintf(sinf(ang) * static_cast<float>(r_body)));
      int rr = (bi == kPmBodySun) ? 8 : (bi == kPmBodyMoon ? 7 : 6);
      const bool hi = (highlight_body == bi);
      if (hi) {
        rr += 3;
      } else if (pulse_on) {
        rr += 1;
      }
      const uint16_t col = hi ? gfx->color565(255, 245, 170) : k_body_col[bi];
      gfx->fillCircle(px, py, rr, col);
      gfx->drawCircle(px, py, rr, hi ? gfx->color565(255, 255, 255) : RGB565_WHITE);
      draw_planet_glyph(static_cast<PmEphemBody>(bi), px, py, RGB565_BLACK, RGB565_BLACK);
      if (hi) {
        gfx->drawCircle(px, py, rr + 4, gfx->color565(255, 255, 255));
      }
    }
    double natal_sun = 0;
    if (birth.valid && pm_transit_natal_sun_lon(&birth, &natal_sun)) {
      const float angn = astro_angle_from_lon(natal_sun);
      const int qx = cx + static_cast<int>(lrintf(cosf(angn) * static_cast<float>(r_in - 6)));
      const int qy = cy + static_cast<int>(lrintf(sinf(angn) * static_cast<float>(r_in - 6)));
      const int q2x = cx + static_cast<int>(lrintf(cosf(angn + 0.35f) * static_cast<float>(r_in - 18)));
      const int q2y = cy + static_cast<int>(lrintf(sinf(angn + 0.35f) * static_cast<float>(r_in - 18)));
      const int q3x = cx + static_cast<int>(lrintf(cosf(angn - 0.35f) * static_cast<float>(r_in - 18)));
      const int q3y = cy + static_cast<int>(lrintf(sinf(angn - 0.35f) * static_cast<float>(r_in - 18)));
      gfx->fillTriangle(qx, qy, q2x, q2y, q3x, q3y, gfx->color565(120, 200, 255));
    }
    (void)remote_tp;
  }
}

static uint32_t s_thinking_t0_ms = 0;
static uint32_t s_thinking_est_ms = 1;

static void thinking_progress_begin(uint32_t est_ms) {
  s_thinking_t0_ms = millis();
  s_thinking_est_ms = est_ms < 4000u ? 4000u : est_ms;
}

static void thinking_progress_end() {
  s_thinking_t0_ms = 0;
}

static float thinking_progress_now() {
  if (s_thinking_t0_ms == 0) {
    return -1.f;
  }
  const uint32_t el = millis() - s_thinking_t0_ms;
  float p = static_cast<float>(el) / static_cast<float>(s_thinking_est_ms);
  if (p > 0.96f) {
    p = 0.96f;
  }
  return p;
}

static void recording_progress_begin() {
  thinking_progress_begin(5000u);
}

static void recording_progress_end() {
  thinking_progress_end();
}

static float recording_progress_now() {
  if (s_thinking_t0_ms == 0) {
    return -1.f;
  }
  float p = thinking_progress_now();
  const float by_fill =
      static_cast<float>(g_pcm_len) / static_cast<float>(MYNAH_VOICE_MAX_PCM_BYTES);
  if (by_fill > p) {
    p = by_fill;
  }
  if (p > 0.96f) {
    p = 0.96f;
  }
  return p;
}

/** 1 px ring just inside the 24h rainbow; `progress` 0..1 fills clockwise from top. */
static void draw_thinking_progress_ring(float progress) {
  if (progress < 0.f) {
    progress = 0.f;
  }
  if (progress > 1.f) {
    progress = 1.f;
  }
  const int cx = LCD_WIDTH / 2;
  const int cy = LCD_HEIGHT / 2;
  const int R = min(LCD_WIDTH, LCD_HEIGHT) / 2;
  const int r_ring = R - 10;
  const uint16_t c_track = gfx->color565(36, 40, 52);
  const uint16_t c_arc = gfx->color565(200, 215, 255);

  gfx->drawCircle(cx, cy, r_ring, c_track);

  if (progress <= 0.f) {
    return;
  }
  const float a0 = -kPi * 0.5f;
  const float span = kTwoPi * progress;
  const int steps = static_cast<int>(lrintf(span * static_cast<float>(r_ring) / 2.f));
  const int n = steps < 24 ? 24 : (steps > 360 ? 360 : steps);
  int px0 = 0;
  int py0 = 0;
  bool have0 = false;
  for (int i = 0; i <= n; ++i) {
    const float a = a0 + span * (static_cast<float>(i) / static_cast<float>(n));
    const int px = cx + static_cast<int>(lrintf(cosf(a) * static_cast<float>(r_ring)));
    const int py = cy + static_cast<int>(lrintf(sinf(a) * static_cast<float>(r_ring)));
    gfx->drawPixel(px, py, c_arc);
    if (have0) {
      gfx->drawLine(px0, py0, px, py, c_arc);
    }
    px0 = px;
    py0 = py;
    have0 = true;
  }
}

static void draw_astro_voice_screen(const char *status, int highlight_body, int highlight_sign,
                                    bool pulse_chart, float thinking_progress = -1.f) {
  struct tm tm = {};
  const bool valid = pm_time_valid();
  if (valid) {
    pm_time_local(&tm);
  }
  gfx->fillScreen(gfx->color565(12, 14, 22));
  draw_astrology_face(&tm, valid, highlight_body, highlight_sign, pulse_chart);
  if (thinking_progress >= 0.f) {
    draw_circumference_rainbow_24h(valid);
    draw_thinking_progress_ring(thinking_progress);
  } else if (status && status[0] != '\0') {
    gfx->setTextSize(1, 1);
    int16_t x1, y1;
    uint16_t w, h;
    gfx->getTextBounds(status, 0, 0, &x1, &y1, &w, &h);
    const int pad_x = 10;
    const int pill_w = static_cast<int>(w) + pad_x * 2;
    const int pill_x = (LCD_WIDTH - pill_w) / 2;
    gfx->fillRoundRect(pill_x, 10, pill_w, 22, 10, gfx->color565(18, 20, 34));
    gfx->drawRoundRect(pill_x, 10, pill_w, 22, 10, gfx->color565(78, 82, 118));
    gfx->setTextColor(gfx->color565(220, 200, 255));
    gfx->setCursor(pill_x + pad_x, 16);
    gfx->print(status);
  }
  gfx->flush();
}

static void draw_radial_annulus_slice(int cx, int cy, float ang, int r0, int r1, uint16_t col, int half_w) {
  if (r1 <= r0 || half_w < 0) {
    return;
  }
  const float ux = cosf(ang);
  const float uy = sinf(ang);
  const float px = -uy;
  const float py = ux;
  const int x0 = cx + static_cast<int>(lrintf(ux * static_cast<float>(r0)));
  const int y0 = cy + static_cast<int>(lrintf(uy * static_cast<float>(r0)));
  const int x1 = cx + static_cast<int>(lrintf(ux * static_cast<float>(r1)));
  const int y1 = cy + static_cast<int>(lrintf(uy * static_cast<float>(r1)));
  for (int w = -half_w; w <= half_w; ++w) {
    const int ox = static_cast<int>(lrintf(px * static_cast<float>(w)));
    const int oy = static_cast<int>(lrintf(py * static_cast<float>(w)));
    gfx->drawLine(x0 + ox, y0 + oy, x1 + ox, y1 + oy, col);
  }
}

static float cycle_angle_for_day(float day0, float cycle_len) {
  return (day0 / cycle_len) * kTwoPi - kPi * 0.5f;
}

static void draw_cycle_band(int cx, int cy, int r_inner, int r_outer, uint8_t cycle_len,
                            float start_day0, float day_count, uint16_t col, int half_w) {
  if (cycle_len == 0 || day_count <= 0.f) {
    return;
  }
  while (start_day0 < 0.f) {
    start_day0 += static_cast<float>(cycle_len);
  }
  while (start_day0 >= static_cast<float>(cycle_len)) {
    start_day0 -= static_cast<float>(cycle_len);
  }

  const float max_count = static_cast<float>(cycle_len);
  if (day_count > max_count) {
    day_count = max_count;
  }
  const int steps = static_cast<int>(ceilf(day_count * 12.f));
  const int n = steps < 8 ? 8 : (steps > 432 ? 432 : steps);
  for (int i = 0; i <= n; ++i) {
    float day = start_day0 + day_count * (static_cast<float>(i) / static_cast<float>(n));
    while (day >= static_cast<float>(cycle_len)) {
      day -= static_cast<float>(cycle_len);
    }
    draw_radial_annulus_slice(cx, cy, cycle_angle_for_day(day, static_cast<float>(cycle_len)),
                              r_inner, r_outer, col, half_w);
  }
}

static void draw_cycle_marker(int cx, int cy, int r_mid, float ang, bool pulse) {
  const float ux = cosf(ang);
  const float uy = sinf(ang);
  const int x = cx + static_cast<int>(lrintf(ux * static_cast<float>(r_mid)));
  const int y = cy + static_cast<int>(lrintf(uy * static_cast<float>(r_mid)));
  const int pulse_px = pulse ? (2 + static_cast<int>((millis() / 110u) % 3u)) : 0;
  const uint16_t c_marker = gfx->color565(252, 248, 230);
  const uint16_t c_halo = pulse ? gfx->color565(120, 210, 205) : gfx->color565(70, 76, 92);
  gfx->fillCircle(x, y, 11 + pulse_px, c_halo);
  gfx->fillCircle(x, y, 6 + pulse_px / 2, c_marker);
  gfx->drawLine(cx + static_cast<int>(lrintf(ux * static_cast<float>(r_mid + 12))),
                cy + static_cast<int>(lrintf(uy * static_cast<float>(r_mid + 12))),
                cx + static_cast<int>(lrintf(ux * static_cast<float>(r_mid + 25))),
                cy + static_cast<int>(lrintf(uy * static_cast<float>(r_mid + 25))), c_marker);
}

static void draw_cycle_face(const struct tm *tm_local, bool valid_local) {
  const uint16_t c_dim = gfx->color565(142, 150, 166);
  const uint16_t c_error = gfx->color565(255, 155, 145);
  if (!valid_local || !tm_local) {
    drawCenteredLine("need time", 220, c_dim, 2, 2);
    return;
  }

  PmCycleProfile cycle = {};
  (void)pm_cycle_load(&cycle);
  if (!cycle.has_last_period) {
    drawCenteredLine("set cycle", 220, c_dim, 2, 2);
    return;
  }

  const uint16_t year = static_cast<uint16_t>(tm_local->tm_year + 1900);
  const uint8_t month = static_cast<uint8_t>(tm_local->tm_mon + 1);
  const uint8_t day = static_cast<uint8_t>(tm_local->tm_mday);
  const int32_t day_idx = pm_cycle_day_index_for_date(&cycle, year, month, day);
  if (day_idx < 0) {
    drawCenteredLine("set cycle", 220, c_error, 2, 2);
    return;
  }

  const int cx = LCD_WIDTH / 2;
  const int cy = LCD_HEIGHT / 2;
  const int R = min(LCD_WIDTH, LCD_HEIGHT) / 2;
  const int r_outer = R - 20;
  const int r_inner = r_outer - 32;
  const int r_mid = (r_inner + r_outer) / 2;
  const uint8_t cycle_len = cycle.cycle_length_days ? cycle.cycle_length_days : PM_CYCLE_DEFAULT_LENGTH_DAYS;
  const uint8_t period_len = cycle.period_length_days < cycle_len ? cycle.period_length_days
                                                                  : PM_CYCLE_DEFAULT_PERIOD_DAYS;
  int ov_day = static_cast<int>(cycle_len) - 14;
  if (ov_day < 1) {
    ov_day = 1;
  } else if (ov_day > static_cast<int>(cycle_len)) {
    ov_day = cycle_len;
  }

  const uint16_t c_track = gfx->color565(33, 39, 55);
  const uint16_t c_luteal = gfx->color565(105, 82, 148);
  const uint16_t c_fertile = gfx->color565(44, 165, 140);
  const uint16_t c_period = gfx->color565(205, 76, 118);
  const uint16_t c_ov = gfx->color565(245, 195, 80);
  const uint16_t c_spoke = gfx->color565(60, 68, 84);

  draw_cycle_band(cx, cy, r_inner, r_outer, cycle_len, 0.f, static_cast<float>(cycle_len), c_track, 2);
  draw_cycle_band(cx, cy, r_inner, r_outer, cycle_len, static_cast<float>(ov_day),
                  static_cast<float>(cycle_len - ov_day), c_luteal, 2);
  draw_cycle_band(cx, cy, r_inner, r_outer, cycle_len, static_cast<float>(ov_day - 1 - 3), 7.f,
                  c_fertile, 2);
  draw_cycle_band(cx, cy, r_inner, r_outer, cycle_len, 0.f, static_cast<float>(period_len), c_period, 2);

  for (uint8_t d = 0; d < cycle_len; ++d) {
    if (d % 7 != 0 && d != 0) {
      continue;
    }
    const float a = cycle_angle_for_day(static_cast<float>(d), static_cast<float>(cycle_len));
    const int t0 = d == 0 ? r_inner - 10 : r_inner - 5;
    const int t1 = r_inner - 1;
    gfx->drawLine(cx + static_cast<int>(lrintf(cosf(a) * static_cast<float>(t0))),
                  cy + static_cast<int>(lrintf(sinf(a) * static_cast<float>(t0))),
                  cx + static_cast<int>(lrintf(cosf(a) * static_cast<float>(t1))),
                  cy + static_cast<int>(lrintf(sinf(a) * static_cast<float>(t1))), c_spoke);
  }

  const float ov_ang = cycle_angle_for_day(static_cast<float>(ov_day - 1), static_cast<float>(cycle_len));
  draw_radial_annulus_slice(cx, cy, ov_ang, r_inner - 2, r_outer + 4, c_ov, 3);

  const bool confirm = static_cast<int32_t>(millis() - g_cycle_confirm_until_ms) < 0;
  const float today_ang = cycle_angle_for_day(static_cast<float>(day_idx), static_cast<float>(cycle_len));
  draw_cycle_marker(cx, cy, r_mid, today_ang, confirm);

  gfx->drawCircle(cx, cy, r_outer + 5, gfx->color565(38, 45, 60));
  gfx->drawCircle(cx, cy, r_inner - 8, gfx->color565(30, 36, 50));
  if (confirm) {
    gfx->drawCircle(cx, cy, r_outer + 9, gfx->color565(90, 210, 190));
  }
}

static void draw_charging_ripples_on_rainbow_rim(int cx, int cy, int r_inner, int r_outer, bool valid) {
  if (!pm_pmu_charging()) {
    return;
  }

  auto wrap360 = [](float d) {
    d = fmodf(d, 360.0f);
    if (d < 0.f) {
      d += 360.0f;
    }
    return d;
  };

  constexpr int k_steps = 92;
  constexpr int k_half_w = 3;
  const float a0 = kPi * 0.5f - 0.82f;
  const float a1 = kPi * 0.5f + 0.82f;
  const float phase = fmodf(static_cast<float>(millis()) * 0.00042f, 1.f);

  for (int i = 0; i < k_steps; ++i) {
    const float u = static_cast<float>(i) / static_cast<float>(k_steps - 1);
    float envelope = 1.f - fabsf(u - 0.5f) * 1.65f;
    if (envelope <= 0.f) {
      continue;
    }
    if (envelope > 1.f) {
      envelope = 1.f;
    }

    float wave = 0.f;
    for (int j = 0; j < 3; ++j) {
      float center = phase + static_cast<float>(j) * 0.34f;
      center -= floorf(center);
      float d = fabsf(u - center);
      if (d > 0.5f) {
        d = 1.f - d;
      }
      float pulse = 1.f - d / 0.095f;
      if (pulse > wave) {
        wave = pulse;
      }
    }

    const float intensity = wave * envelope;
    if (intensity < 0.10f) {
      continue;
    }

    const float ang = a0 + (a1 - a0) * u;
    float af = ang + kPi * 0.5f;
    af = fmodf(af, kTwoPi);
    if (af < 0.f) {
      af += kTwoPi;
    }

    const float hue_deg = valid ? wrap360(af * (360.f / kTwoPi))
                                : wrap360(af * (360.f / kTwoPi) +
                                          fmodf(static_cast<float>(millis()) * 0.025f, 360.f));
    const float sat = 0.58f - intensity * 0.18f;
    const float val = 0.18f + intensity * 0.20f;
    const uint16_t col = color565FromHsv(gfx, hue_deg, sat, val);
    const int radial_wobble =
        static_cast<int>(lrintf(sinf(static_cast<float>(millis()) * 0.006f + u * kPi * 5.f)));
    draw_radial_annulus_slice(cx, cy, ang, r_inner - 1 + radial_wobble, r_outer + 1, col, k_half_w);
  }
}

/** 24h rim: outermost band; hue(sec of day) matches face fill. */
static void draw_circumference_rainbow_24h(bool valid) {
  const int cx = LCD_WIDTH / 2;
  const int cy = LCD_HEIGHT / 2;
  const int R = min(LCD_WIDTH, LCD_HEIGHT) / 2;
  /** Inset a few pixels from the physical edge (bezel / mask). */
  const int r_outer = R - 4;
  const int r_inner = r_outer - 5;
  constexpr int k_seg = 288;
  constexpr int k_half_w = 4;

  auto wrap360 = [](float d) {
    d = fmodf(d, 360.0f);
    if (d < 0.f) {
      d += 360.0f;
    }
    return d;
  };

  for (int s = 0; s < k_seg; ++s) {
    const float amid =
        (static_cast<float>(s) + 0.5f) * (kTwoPi / static_cast<float>(k_seg)) - kPi * 0.5f;
    float af = amid + kPi * 0.5f;
    af = fmodf(af, kTwoPi);
    if (af < 0.f) {
      af += kTwoPi;
    }
    float hue_deg;
    if (valid) {
      /** `af` = 0 at top → midnight; same circadian keyframes as the face fill. */
      const float sec_of_day = af * (86400.f / kTwoPi);
      hue_deg = pm_circadian_hue_from_seconds(sec_of_day);
    } else {
      hue_deg = wrap360(af * (360.f / kTwoPi) + fmodf(static_cast<float>(millis()) * 0.025f, 360.f));
    }
    const uint16_t col = color565FromHsv(gfx, hue_deg, k_clock_face_hsv_s, k_clock_face_hsv_v);
    draw_radial_annulus_slice(cx, cy, amid, r_inner, r_outer, col, k_half_w);
  }
  draw_charging_ripples_on_rainbow_rim(cx, cy, r_inner, r_outer, valid);
}

static void voice_last_play_clear() {
  if (g_last_play_mp3) {
    free(g_last_play_mp3);
    g_last_play_mp3 = nullptr;
  }
  g_last_play_mp3_len = 0;
}

static void voice_last_play_save(const uint8_t *mp3, size_t len) {
  voice_last_play_clear();
  if (!mp3 || len < 64) {
    return;
  }
  uint8_t *buf = static_cast<uint8_t *>(heap_caps_malloc(len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!buf) {
    buf = static_cast<uint8_t *>(malloc(len));
  }
  if (!buf) {
    return;
  }
  memcpy(buf, mp3, len);
  g_last_play_mp3 = buf;
  g_last_play_mp3_len = len;
}

/** Copies cached MP3 into [g_voice_result] and enters [kPlaying]. */
static bool voice_last_play_begin() {
  if (!g_last_play_mp3 || g_last_play_mp3_len < 64) {
    return false;
  }
  pm_voice_result_free(&g_voice_result);
  uint8_t *copy = static_cast<uint8_t *>(
      heap_caps_malloc(g_last_play_mp3_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!copy) {
    copy = static_cast<uint8_t *>(malloc(g_last_play_mp3_len));
  }
  if (!copy) {
    return false;
  }
  memcpy(copy, g_last_play_mp3, g_last_play_mp3_len);
  g_voice_result.mp3 = copy;
  g_voice_result.mp3_len = g_last_play_mp3_len;
  g_astro_voice_active = false;
  g_moon_voice_pcm = false;
  g_calcifer_briefing = false;
  g_voice_play_reset = true;
  g_state = AppState::kPlaying;
  return true;
}

/** `outward`: false = listening waves rim→center; true = speaking waves center→rim. */
static void draw_voice_waves_overlay(bool outward, uint32_t t_ms) {
  const int cx = LCD_WIDTH / 2;
  const int cy = LCD_HEIGHT / 2;
  const int R = min(LCD_WIDTH, LCD_HEIGHT) / 2 - 18;
  const float phase = fmodf(static_cast<float>(t_ms) * 0.0045f, 1.f);
  constexpr int k_n = 7;
  for (int i = 0; i < k_n; ++i) {
    float t = phase + static_cast<float>(i) / static_cast<float>(k_n);
    t -= floorf(t);
    const float u = outward ? t : (1.f - t);
    const int r = 22 + static_cast<int>(u * static_cast<float>(R - 22));
    const uint8_t b = static_cast<uint8_t>(70 + u * 150.f);
    const uint16_t col = gfx->color565(static_cast<uint8_t>(b * 0.55f), b, static_cast<uint8_t>(160 + u * 70.f));
    gfx->drawCircle(cx, cy, r, col);
    if (r > 3) {
      gfx->drawCircle(cx, cy, r - 2, col);
    }
  }
}

static void draw_voice_wave_screen(bool outward, uint32_t t_ms, const char *label) {
  gfx->fillScreen(gfx->color565(8, 10, 18));
  if (pm_time_valid()) {
    draw_circumference_rainbow_24h(true);
  }
  draw_voice_waves_overlay(outward, t_ms);
  if (label && label[0] != '\0') {
    gfx->fillRect(0, 0, LCD_WIDTH, 40, gfx->color565(10, 12, 22));
    drawCenteredLine(label, 12, gfx->color565(215, 205, 255), 1, 1);
  }
  gfx->flush();
}

static void draw_settings_face() {
  const uint16_t c_hi = gfx->color565(210, 215, 235);
  const uint16_t c_dim = gfx->color565(120, 128, 145);
  drawCenteredLine("SETTINGS", 40, c_hi, 2, 2);
  if (!pm_wifi_connected()) {
    drawCenteredLine("WiFi needed", 130, c_dim, 2, 2);
    return;
  }
  const char *host = pm_settings_host_label();
  if (host[0] != '\0') {
    drawCenteredLine(host, 78, c_dim, 1, 1);
  }
  if (pm_settings_url_for_qr()[0] != '\0') {
    if (!pm_settings_draw_qr(gfx, LCD_WIDTH / 2, 238, 240)) {
      drawCenteredLine("QR encode fail", 220, c_dim, 1, 1);
    } else {
      drawCenteredLine("scan for web settings", 392, c_dim, 1, 1);
    }
  }
}

static void draw_castalia_face() {
  const uint16_t c_hi = gfx->color565(210, 215, 235);
  const uint16_t c_dim = gfx->color565(120, 128, 145);
  drawCenteredLine("CASTALIA", 40, c_hi, 2, 2);
  if (!pm_wifi_connected()) {
    drawCenteredLine("WiFi needed", 130, c_dim, 2, 2);
    return;
  }
  if (pm_castalia_has_session()) {
    const char *display = pm_castalia_profile_display_name();
    if (display[0] != '\0') {
      pm_castalia_draw_profile_avatar(gfx, LCD_WIDTH / 2, 228, 72);
      drawCenteredLine(display, 328, c_hi, 1, 2);
      drawCenteredLine("Castalia account", 368, c_dim, 1, 1);
    } else {
      drawCenteredLine(pm_castalia_status_line(), 78, c_dim, 1, 1);
      drawCenteredLine("Signed in", 220, c_hi, 1, 2);
    }
    return;
  }
  drawCenteredLine(pm_castalia_status_line(), 78, c_dim, 1, 1);
  if (pm_castalia_signin_url_for_qr()[0] != '\0') {
    if (!pm_castalia_draw_qr(gfx, LCD_WIDTH / 2, 238, 240)) {
      drawCenteredLine("QR encode fail", 220, c_dim, 1, 1);
    } else {
      drawCenteredLine("scan phone", 392, c_dim, 1, 1);
    }
  } else {
    drawCenteredLine("pairing...", 220, c_dim, 1, 1);
  }
}

static void draw_clock_face(float thinking_progress = -1.f) {
  if (pm_diag_safe_mode()) {
    pm_face_safe_draw(gfx, nullptr);
    gfx->flush();
    return;
  }
  if (g_clock_face == ClockFace::HuePack && pm_faces_pack_available()) {
    pm_faces_pack_render(gfx, thinking_progress);
    gfx->flush();
    return;
  }
  struct tm tm = {};
  int sec_of_day_for_hue = 0;
  if (pm_time_valid()) {
    pm_time_local(&tm);
    sec_of_day_for_hue = tm.tm_hour * 3600 + tm.tm_min * 60 + tm.tm_sec;
  }
  const float hue = pm_time_valid() ? pm_circadian_hue_from_seconds(static_cast<float>(sec_of_day_for_hue))
                                    : fmodf(static_cast<float>(millis()) * 0.0015f, 360.0f);
  const uint16_t bg_hsv = color565FromHsv(gfx, hue, k_clock_face_hsv_s, k_clock_face_hsv_v);
  const uint16_t bg = bg_hsv;
  gfx->fillScreen(bg);

  switch (g_clock_face) {
    case ClockFace::ClassicAnalog:
      draw_analog_clock(bg, &tm, pm_time_valid());
      break;
    case ClockFace::Apocalypso:
      draw_apocalypso_face(&tm, pm_time_valid());
      break;
    case ClockFace::DigitalLocal:
      draw_digital_local_face(&tm, pm_time_valid());
      break;
    case ClockFace::Spotify:
      draw_spotify_face();
      break;
    case ClockFace::Astrology:
      draw_astrology_face(&tm, pm_time_valid(), -1, -1, false);
      break;
    case ClockFace::Moon:
      draw_moon_face(&tm, pm_time_valid());
      break;
    case ClockFace::CalciferCountdown:
      draw_calcifer_face();
      break;
    case ClockFace::Cycle:
      draw_cycle_face(&tm, pm_time_valid());
      break;
    case ClockFace::Castalia:
      draw_castalia_face();
      break;
    case ClockFace::Settings:
      draw_settings_face();
      break;
    case ClockFace::HuePack:
      pm_faces_pack_render(gfx, thinking_progress);
      break;
    default:
      break;
  }

  const int banner_y = (g_clock_face == ClockFace::Apocalypso || g_clock_face == ClockFace::Spotify ||
                        g_clock_face == ClockFace::Astrology || g_clock_face == ClockFace::Moon ||
                        g_clock_face == ClockFace::CalciferCountdown || g_clock_face == ClockFace::Cycle ||
                        g_clock_face == ClockFace::Castalia || g_clock_face == ClockFace::Settings)
                           ? 352
                           : 320;
  if (MYNAH_DEBUG_GESTURES && g_gesture_banner[0] != '\0') {
    drawCenteredLine(g_gesture_banner, banner_y, gfx->color565(255, 220, 160), 1, 1);
  }

  /** Rainbow annulus last (skip on Castalia/Settings — QR + rim was tripping WDT/stack). */
  if (g_clock_face != ClockFace::Castalia && g_clock_face != ClockFace::Settings &&
      g_clock_face != ClockFace::HuePack) {
    draw_circumference_rainbow_24h(pm_time_valid());
    if (thinking_progress >= 0.f) {
      draw_thinking_progress_ring(thinking_progress);
    }
  }
  g_clock_bg565 = bg;
  if (pm_time_valid()) {
    g_analog_saved_local_h = tm.tm_hour;
    g_analog_saved_local_m = tm.tm_min;
  }
  gfx->flush();
  static bool s_boot_marked = false;
  if (!s_boot_marked && !pm_diag_safe_mode()) {
    s_boot_marked = true;
    pm_diag_mark_runtime_valid();
    pm_ota_validate_pending_facepack();
  }
}

static void ensure_pcm_buffer() {
  if (g_pcm) {
    return;
  }
  g_pcm = static_cast<uint8_t *>(
      heap_caps_malloc(MYNAH_VOICE_MAX_PCM_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!g_pcm) {
    g_pcm = static_cast<uint8_t *>(malloc(MYNAH_VOICE_MAX_PCM_BYTES));
  }
}

static void reset_recording_buffer() {
  g_pcm_len = 0;
}

static const char kAstroVoiceSys[] =
    "You are a warm, articulate astrologer speaking aloud for a tiny round watch. Use tropical zodiac. "
    "Chart snapshot data is provided below. If the user asks a question, answer it using those positions; "
    "if they did not ask a question, give ONE flowing mini-reading (under 90 seconds spoken) about today's "
    "transits versus their natal Sun and anything else notable. "
    "No medical or legal advice; reflective insight only, not deterministic fate. "
    "Do not claim arc-minute precision from the numbers. "
    "Do not use asterisk stage directions or emotes (e.g. *smiles*); output only words to be spoken aloud.";

static bool build_astrology_voice_message(char *buf, size_t cap) {
  if (!buf || cap < 200) {
    return false;
  }
  if (!pm_wifi_connected() || !pm_time_valid()) {
    return false;
  }
  struct tm utc = {};
  struct tm loc = {};
  pm_time_local(&loc);
  pm_time_utc(&utc);
  PmTransitPositions tp = {};
  bool remote_tp = false;
  compute_astrology_positions_utc(&utc, time(nullptr), &tp, &remote_tp);
  if (!tp.ok) {
    return false;
  }
  PmBirthSpec b = {};
  (void)pm_birth_load(&b);
  double nslon = 0;
  const bool has_natal = b.valid && pm_transit_natal_sun_lon(&b, &nslon);

  int n = snprintf(
      buf, cap,
      "Pocket Mynah transit snapshot for %04d-%02d-%02d %02d:%02d local. Tropical longitudes (%s deg): ",
      loc.tm_year + 1900, loc.tm_mon + 1, loc.tm_mday, loc.tm_hour, loc.tm_min,
      remote_tp ? "Castalia ephemeris" : "approx");
  if (n < 0 || static_cast<size_t>(n) >= cap) {
    return false;
  }
  size_t off = static_cast<size_t>(n);
  for (int i = 0; i < kPmBodyCount && off + 40 < cap; ++i) {
    const int m = snprintf(buf + off, cap - off, "%s %.1f; ", pm_ephem_body_label(static_cast<PmEphemBody>(i)),
                           tp.lon[i]);
    if (m < 0) {
      return false;
    }
    off += static_cast<size_t>(m);
  }
  if (has_natal && off + 120 < cap) {
    snprintf(buf + off, cap - off,
             "Natal (local civil on this device TZ): %04u-%02u-%02u %02u:%02u — Sun ~%.1f deg (%s). ",
             b.year, b.month, b.day, b.hour, b.minute, nslon, zodiac_abbr_from_lon(nslon));
  } else if (off + 80 < cap) {
    snprintf(buf + off, cap - off, "Natal birth not stored; describe transits in general. ");
  }
  off = strlen(buf);
  if (off + 80 < cap) {
    snprintf(buf + off, cap - off, "Please deliver the spoken reading now.");
  }
  return strlen(buf) > 0;
}

static bool build_astrology_system_prompt() {
  if (!build_astrology_voice_message(g_astrology_voice_msg, sizeof(g_astrology_voice_msg))) {
    return false;
  }
  const int n = snprintf(g_astrology_sys_prompt, sizeof(g_astrology_sys_prompt),
                         "%s\n\nChart snapshot:\n%s", kAstroVoiceSys, g_astrology_voice_msg);
  return n > 0 && static_cast<size_t>(n) < sizeof(g_astrology_sys_prompt);
}

static bool face_index_from_name(const char *name, int *out) {
  if (!name || !out) {
    return false;
  }
  struct {
    const char *n;
    int idx;
  } k[] = {{"classic", 0}, {"hue", 0},     {"analog", 0},    {"apocalypso", 1},
           {"digital", 2}, {"spotify", 3}, {"astro", 4},       {"astrology", 4},
           {"moon", 5},    {"calcifer", 6}, {"schedule", 6},  {"cycle", 7},
           {"menstrual", 7}, {"castalia", 8}, {"settings", 9}, {"config", 9},
           {"huepack", 10}, {"pack", 10}};
  for (const auto &e : k) {
    if (strcasecmp(name, e.n) == 0) {
      *out = e.idx;
      return true;
    }
  }
  return false;
}

static void print_cycle_status() {
  PmCycleProfile p = {};
  (void)pm_cycle_load(&p);
  if (p.has_last_period) {
    Serial.printf("cycle: last_period_ymd=%04u-%02u-%02u cycle_length_days=%u period_length_days=%u\n",
                  p.last_period_year, p.last_period_month, p.last_period_day, p.cycle_length_days,
                  p.period_length_days);
  } else {
    Serial.printf("cycle: last_period_ymd=(unset) cycle_length_days=%u period_length_days=%u\n",
                  p.cycle_length_days, p.period_length_days);
  }
  if (pm_time_valid() && p.has_last_period) {
    struct tm loc = {};
    pm_time_local(&loc);
    const int32_t idx = pm_cycle_day_index_for_date(&p, static_cast<uint16_t>(loc.tm_year + 1900),
                                                    static_cast<uint8_t>(loc.tm_mon + 1),
                                                    static_cast<uint8_t>(loc.tm_mday));
    if (idx >= 0) {
      Serial.printf("cycle: today day %ld of %u\n", static_cast<long>(idx + 1), p.cycle_length_days);
    }
  }
  Serial.println("cycle: wellness estimate only; NVS-only, no cloud sync");
}

static void poll_serial_birth_commands() {
  static char line[100];
  static size_t li = 0;
  while (Serial.available() > 0) {
    const int c = Serial.read();
    if (c < 0) {
      break;
    }
    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      line[li] = '\0';
      li = 0;
      if (strncmp(line, "birth ", 6) == 0) {
        const char *p = line + 6;
        while (*p == ' ') {
          ++p;
        }
        if (strncmp(p, "clear", 5) == 0 && (p[5] == '\0' || p[5] == ' ')) {
          pm_birth_clear();
          Serial.println("birth: cleared (NVS)");
        } else {
          unsigned y = 0, mo = 0, d = 0, h = 0, mi = 0;
          if (sscanf(p, "%u %u %u %u %u", &y, &mo, &d, &h, &mi) == 5 && y >= 1900 && y <= 2100 && mo >= 1 &&
              mo <= 12 && d >= 1 && d <= 31 && h <= 23 && mi <= 59) {
            PmBirthSpec bb = {};
            bb.year = static_cast<uint16_t>(y);
            bb.month = static_cast<uint8_t>(mo);
            bb.day = static_cast<uint8_t>(d);
            bb.hour = static_cast<uint8_t>(h);
            bb.minute = static_cast<uint8_t>(mi);
            bb.valid = true;
            pm_birth_save(&bb);
            Serial.printf("birth: saved %u-%02u-%02u %02u:%02u local (NVS)\n", y, mo, d, h, mi);
          } else {
            Serial.println("birth: usage: birth YYYY MM DD HH MI   |   birth clear");
          }
        }
        g_clock_repaint_pending = true;
      } else if (strcmp(line, "cycle") == 0 || strncmp(line, "cycle ", 6) == 0) {
        const char *p = line + 5;
        while (*p == ' ') {
          ++p;
        }
        if (*p == '\0' || strncmp(p, "status", 6) == 0) {
          print_cycle_status();
        } else if (strncmp(p, "clear", 5) == 0 && (p[5] == '\0' || p[5] == ' ')) {
          pm_cycle_clear();
          Serial.println("cycle: cleared (NVS)");
        } else if ((strncmp(p, "today", 5) == 0 && (p[5] == '\0' || p[5] == ' ')) ||
                   (strncmp(p, "start", 5) == 0 && (p[5] == '\0' || p[5] == ' '))) {
          if (!pm_time_valid()) {
            Serial.println("cycle: need time");
          } else {
            struct tm loc = {};
            pm_time_local(&loc);
            if (pm_cycle_log_period_started_today(&loc)) {
              Serial.printf("cycle: saved %04d-%02d-%02d as period day 1 (NVS)\n",
                            loc.tm_year + 1900, loc.tm_mon + 1, loc.tm_mday);
              g_cycle_confirm_until_ms = millis() + 1200u;
            } else {
              Serial.println("cycle: save failed");
            }
          }
        } else if (strncmp(p, "length ", 7) == 0) {
          unsigned days = 0;
          if (sscanf(p + 7, "%u", &days) == 1 &&
              pm_cycle_set_cycle_length_days(static_cast<uint8_t>(days))) {
            Serial.printf("cycle: cycle_length_days=%u (NVS)\n", days);
            g_cycle_confirm_until_ms = millis() + 1200u;
          } else {
            Serial.printf("cycle: length must be %u-%u days\n", PM_CYCLE_MIN_LENGTH_DAYS,
                          PM_CYCLE_MAX_LENGTH_DAYS);
          }
        } else if (strncmp(p, "period ", 7) == 0) {
          unsigned days = 0;
          if (sscanf(p + 7, "%u", &days) == 1 &&
              pm_cycle_set_period_length_days(static_cast<uint8_t>(days))) {
            Serial.printf("cycle: period_length_days=%u (NVS)\n", days);
            g_cycle_confirm_until_ms = millis() + 1200u;
          } else {
            Serial.println("cycle: period must be 1-10 days and shorter than cycle length");
          }
        } else {
          unsigned y = 0, mo = 0, d = 0;
          if (sscanf(p, "%u %u %u", &y, &mo, &d) == 3 &&
              pm_cycle_set_last_period(static_cast<uint16_t>(y), static_cast<uint8_t>(mo),
                                       static_cast<uint8_t>(d))) {
            Serial.printf("cycle: saved %u-%02u-%02u as period day 1 (NVS)\n", y, mo, d);
            g_cycle_confirm_until_ms = millis() + 1200u;
          } else {
            Serial.println("cycle: usage: cycle | cycle YYYY MM DD | cycle today | cycle length N | cycle period N | cycle clear");
          }
        }
        g_clock_repaint_pending = true;
      } else if (strncmp(line, "face ", 5) == 0) {
        const char *p = line + 5;
        while (*p == ' ') {
          ++p;
        }
        int idx = -1;
        char *end = nullptr;
        const long n = strtol(p, &end, 10);
        if (end != p && end && (*end == '\0' || *end == ' ')) {
          idx = static_cast<int>(n);
        } else if (face_index_from_name(p, &idx)) {
          /* ok */
        }
        if (idx >= 0 && idx < static_cast<int>(ClockFace::kNumFaces)) {
          g_clock_face = static_cast<ClockFace>(idx);
          g_clock_repaint_pending = true;
          Serial.printf("face: %d\n", idx);
        } else {
          Serial.println("face: usage: face <0-9|name>");
        }
      } else if (strcmp(line, "ota status") == 0) {
        pm_ota_print_status();
      } else if (strcmp(line, "safe") == 0) {
        pm_diag_enter_safe_mode("serial");
        g_clock_repaint_pending = true;
      }
      continue;
    }
    if (li + 1 < sizeof(line)) {
      line[li++] = static_cast<char>(c);
    } else {
      li = 0;
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);

  Wire.begin(IIC_SDA, IIC_SCL);

  if (!gfx->begin()) {
    Serial.println("gfx->begin() failed");
    while (true) {
      delay(1000);
    }
  }

  pm_ota_init();
  pm_diag_init();
  pm_ota_validate_pending_runtime();
  pm_ota_validate_pending_facepack();
  if (pm_faces_pack_available()) {
    Serial.println("face pack: loaded (swipe to HuePack or serial: face pack)");
  }
  tft->setBrightness(200);
  gfx->fillScreen(RGB565_BLACK);
  gfx->flush();

  (void)pm_touch_begin();
  pm_gesture_reset();
  (void)pm_side_buttons_begin();

  if (pm_wifi_begin()) {
    pm_ntp_sync_blocking();
    pm_castalia_warmup_after_wifi();
  }
  pm_screen_http_begin(gfx);

  ensure_pcm_buffer();

  Serial.println("PocketMynah MVP ready");
}

void loop() {
  pm_screen_http_loop();
  const uint32_t now = millis();
  poll_serial_birth_commands();
  const uint8_t side_ev = pm_side_buttons_poll(now);

  pm_gesture_poll(now);

  PmGestureEvent ge;
  while (pm_gesture_consume(&ge)) {
    if (g_state == AppState::kClock &&
        (ge.kind == PmGestureKind::SwipeLeft || ge.kind == PmGestureKind::SwipeRight)) {
      cycle_clock_face(ge.kind == PmGestureKind::SwipeLeft ? 1 : -1);
      g_clock_repaint_pending = true;
      g_gesture_banner[0] = '\0';
      Serial.printf("[gesture] face @ %d,%d\n", static_cast<int>(ge.x), static_cast<int>(ge.y));
      continue;
    } else if (g_state == AppState::kClock && g_clock_face == ClockFace::Spotify &&
               (ge.kind == PmGestureKind::SwipeUp || ge.kind == PmGestureKind::SwipeDown ||
                ge.kind == PmGestureKind::Tap || ge.kind == PmGestureKind::LongPress)) {
      if (!pm_wifi_connected()) {
        snprintf(g_gesture_banner, sizeof(g_gesture_banner), "spotify: no wifi");
      } else if (ge.kind == PmGestureKind::SwipeUp) {
        pm_spotify_command("next", &g_spotify_ui);
      } else if (ge.kind == PmGestureKind::SwipeDown) {
        pm_spotify_command("previous", &g_spotify_ui);
      } else if (ge.kind == PmGestureKind::Tap) {
        int z = -1;
        if (spotify_hit_transport_bar(ge.x, ge.y, &z)) {
          if (z == 0) {
            pm_spotify_command("previous", &g_spotify_ui);
            snprintf(g_gesture_banner, sizeof(g_gesture_banner), "spotify: prev");
          } else if (z == 1) {
            if (g_spotify_ui.is_playing) {
              pm_spotify_command("stop", &g_spotify_ui);
              snprintf(g_gesture_banner, sizeof(g_gesture_banner), "spotify: stop");
            } else {
              pm_spotify_command("play", &g_spotify_ui);
              snprintf(g_gesture_banner, sizeof(g_gesture_banner), "spotify: play");
            }
          } else {
            pm_spotify_command("next", &g_spotify_ui);
            snprintf(g_gesture_banner, sizeof(g_gesture_banner), "spotify: next");
          }
        } else {
          if (g_spotify_ui.is_playing) {
            pm_spotify_command("stop", &g_spotify_ui);
          } else {
            pm_spotify_command("play", &g_spotify_ui);
          }
          snprintf(g_gesture_banner, sizeof(g_gesture_banner), "spotify: tap");
        }
      } else {
        pm_spotify_refresh(&g_spotify_ui);
        snprintf(g_gesture_banner, sizeof(g_gesture_banner), "spotify: refresh");
      }
      g_clock_repaint_pending = true;
    } else if (g_state == AppState::kClock && g_clock_face == ClockFace::Cycle &&
               (ge.kind == PmGestureKind::Tap || ge.kind == PmGestureKind::SwipeUp ||
                ge.kind == PmGestureKind::SwipeDown)) {
      if (ge.kind == PmGestureKind::Tap) {
        if (!pm_time_valid()) {
          Serial.println("cycle: tap needs time");
        } else {
          struct tm loc = {};
          pm_time_local(&loc);
          if (pm_cycle_log_period_started_today(&loc)) {
            Serial.printf("cycle: tap saved %04d-%02d-%02d as day 1\n",
                          loc.tm_year + 1900, loc.tm_mon + 1, loc.tm_mday);
            g_cycle_confirm_until_ms = now + 1200u;
          }
        }
      } else {
        const uint8_t len = pm_cycle_adjust_cycle_length_preset(ge.kind == PmGestureKind::SwipeUp ? 1 : -1);
        Serial.printf("cycle: length preset %u days\n", len);
        g_cycle_confirm_until_ms = now + 1200u;
      }
      g_gesture_banner[0] = '\0';
      g_clock_repaint_pending = true;
      continue;
    } else if (g_state == AppState::kClock && g_clock_face == ClockFace::Moon &&
               (ge.kind == PmGestureKind::SwipeUp || ge.kind == PmGestureKind::SwipeDown)) {
      cycle_clock_face(ge.kind == PmGestureKind::SwipeUp ? 1 : -1);
      g_clock_repaint_pending = true;
      g_gesture_banner[0] = '\0';
      continue;
    } else if (ge.kind != PmGestureKind::SwipeUp && ge.kind != PmGestureKind::SwipeDown) {
      snprintf(g_gesture_banner, sizeof(g_gesture_banner), "%s", gesture_label(ge.kind));
      Serial.printf("[gesture] %s @ %d,%d\n", g_gesture_banner, static_cast<int>(ge.x), static_cast<int>(ge.y));
    }
  }

  if (g_state == AppState::kClock && g_clock_face == ClockFace::Moon &&
      (side_ev & PM_SIDE_BTN_BOOT) != 0) {
    if (!pm_wifi_connected()) {
      g_clock_repaint_pending = true;
    } else if (!pm_time_valid()) {
      g_clock_repaint_pending = true;
    } else if (!build_moon_voice_message(g_moon_voice_msg, sizeof(g_moon_voice_msg))) {
      g_clock_repaint_pending = true;
    } else {
      pm_voice_result_free(&g_voice_result);
      g_voice_use_message = true;
      g_text_voice_route = k_tv_moon;
      g_astro_voice_active = false;
      g_moon_voice_pcm = false;
      g_calcifer_briefing = false;
      g_state = AppState::kThinking;
    }
  }

  if (g_state == AppState::kClock && g_clock_face == ClockFace::Astrology &&
      (side_ev & PM_SIDE_BTN_BOOT) != 0) {
    if (!pm_wifi_connected()) {
      snprintf(g_gesture_banner, sizeof(g_gesture_banner), "astro: need WiFi");
      g_clock_repaint_pending = true;
    } else if (!pm_time_valid()) {
      snprintf(g_gesture_banner, sizeof(g_gesture_banner), "astro: need time");
      g_clock_repaint_pending = true;
    } else if (!build_astrology_voice_message(g_astrology_voice_msg, sizeof(g_astrology_voice_msg))) {
      snprintf(g_gesture_banner, sizeof(g_gesture_banner), "astro: build msg fail");
      g_clock_repaint_pending = true;
    } else {
      pm_voice_result_free(&g_voice_result);
      g_voice_use_message = true;
      g_text_voice_route = k_tv_astro;
      g_astro_voice_active = true;
      g_astro_voice_pcm = false;
      s_astro_voice_armed = false;
      s_astro_play_armed = false;
      memset(&g_astro_highlight_plan, 0, sizeof(g_astro_highlight_plan));
      g_state = AppState::kThinking;
    }
  }

  static uint32_t s_ptt_press_ms = 0;
  const bool ptt_hold = pm_ptt_button_held();
  if (g_state == AppState::kClock) {
    if (ptt_hold) {
      if (s_ptt_press_ms == 0) {
        s_ptt_press_ms = now;
      }
    } else {
      s_ptt_press_ms = 0;
    }
  } else {
    s_ptt_press_ms = 0;
  }
  const bool ptt_armed =
      ptt_hold && s_ptt_press_ms != 0 && (now - s_ptt_press_ms >= MYNAH_PTT_ARM_MS);

  static uint32_t s_last_clock_boot_brief_ms = 0;
  if (g_state == AppState::kClock && (side_ev & PM_SIDE_BTN_BOOT) &&
      g_clock_face != ClockFace::Astrology && g_clock_face != ClockFace::Moon) {
    if (voice_last_play_begin()) {
      /* BOOT replay last TTS */
    } else {
      const bool want_calcifer = (g_clock_face == ClockFace::ClassicAnalog ||
                                  g_clock_face == ClockFace::DigitalLocal ||
                                  g_clock_face == ClockFace::CalciferCountdown);
      if (want_calcifer && pm_wifi_connected() && pm_time_valid() &&
          (now - s_last_clock_boot_brief_ms >= 3500u)) {
        s_last_clock_boot_brief_ms = now;
        pm_voice_result_free(&g_voice_result);
        g_voice_use_message = false;
        g_calcifer_briefing = true;
        g_astro_voice_active = false;
        g_astro_voice_pcm = false;
        g_moon_voice_pcm = false;
        g_state = AppState::kThinking;
      }
    }
  }

  switch (g_state) {
    case AppState::kClock: {
      static bool s_clock_paint_inited = false;
      static time_t s_prev_epoch = -3;
      static bool s_prev_wifi = false;
      static char s_prev_banner[44] = "";
      static uint32_t s_last_ntp_retry_wall = 0;
      static uint32_t s_last_no_time_redraw = 0;
      static bool s_prev_charging = false;
      static uint32_t s_last_charge_ripple_paint = 0;
      static uint32_t s_last_cycle_confirm_paint = 0;

      const bool wifi = pm_wifi_connected();
      const bool valid = pm_time_valid();
      const time_t epoch = time(nullptr);
      const bool charging = pm_pmu_charging();
      const bool charging_chg = charging != s_prev_charging;
      s_prev_charging = charging;

      struct tm tm_now = {};
      if (valid) {
        pm_time_local(&tm_now);
      }

      if (wifi && !valid && (now - s_last_ntp_retry_wall > 60000)) {
        s_last_ntp_retry_wall = now;
        pm_ntp_retry_if_stale();
      }

      static ClockFace s_prev_dial_face = ClockFace::kNumFaces;
      if (g_clock_face != s_prev_dial_face) {
        if (g_clock_face == ClockFace::Castalia) {
          pm_castalia_on_face_enter();
          g_clock_repaint_pending = true;
        } else if (g_clock_face == ClockFace::Settings) {
          pm_settings_refresh_url();
          g_clock_repaint_pending = true;
        }
        s_prev_dial_face = g_clock_face;
      }

      if (g_clock_face == ClockFace::Castalia && wifi && pm_castalia_tick_pair_start()) {
        g_clock_repaint_pending = true;
      }

      if (wifi && pm_castalia_has_session()) {
        (void)pm_castalia_tick_refresh_session();
        if (g_clock_face == ClockFace::Castalia && pm_castalia_tick_fetch_profile()) {
          g_clock_repaint_pending = true;
        }
      }

      const bool sec_tick = valid && (epoch != s_prev_epoch);
      const bool slow_no_time =
          !valid && s_clock_paint_inited && (now - s_last_no_time_redraw >= 12000);
      const bool banner_chg = strcmp(g_gesture_banner, s_prev_banner) != 0;
      const bool wifi_chg = (wifi != s_prev_wifi);
      const bool local_hm_chg =
          valid && g_clock_face != ClockFace::Castalia && g_clock_face != ClockFace::Settings &&
          (g_analog_saved_local_h < 0 || tm_now.tm_hour != g_analog_saved_local_h ||
           tm_now.tm_min != g_analog_saved_local_m);

      if (g_clock_face != ClockFace::Spotify) {
        s_spotify_have_data = false;
      }
      if (g_clock_face != ClockFace::CalciferCountdown) {
        s_calcifer_have_data = false;
      }

      const bool spotify_stale =
          g_clock_face == ClockFace::Spotify && pm_wifi_connected() && s_spotify_have_data &&
          (now - s_last_spotify_poll_ms >= MYNAH_SPOTIFY_POLL_MS);

      const bool calcifer_stale =
          g_clock_face == ClockFace::CalciferCountdown && pm_wifi_connected() && valid &&
          (!s_calcifer_have_data || (now - s_last_calcifer_poll_ms >= MYNAH_CALCIFER_POLL_MS));

      static time_t s_prev_astro_epoch_min = -1;
      static time_t s_astro_remote_attempt_min = -1;
      const time_t epoch_min_bucket = valid ? (epoch / 60) : -1;
      const bool astro_repaint =
          g_clock_face == ClockFace::Astrology && valid && epoch_min_bucket != s_prev_astro_epoch_min;

      const bool sec_tick_paint =
          sec_tick && g_clock_face != ClockFace::Castalia && g_clock_face != ClockFace::Settings &&
              g_clock_face != ClockFace::CalciferCountdown;
      const bool calcifer_sec =
          g_clock_face == ClockFace::CalciferCountdown && valid && sec_tick;
      const bool face_has_rim =
          g_clock_face != ClockFace::Castalia && g_clock_face != ClockFace::Settings;
      const bool charging_ripple_frame =
          charging && face_has_rim && (now - s_last_charge_ripple_paint >= 160u);
      const bool cycle_confirm_frame =
          g_clock_face == ClockFace::Cycle &&
          static_cast<int32_t>(now - g_cycle_confirm_until_ms) < 0 &&
          (now - s_last_cycle_confirm_paint >= 120u);
      const bool full_paint = !s_clock_paint_inited || slow_no_time || banner_chg || wifi_chg ||
                              g_clock_repaint_pending || local_hm_chg || spotify_stale || calcifer_stale ||
                              sec_tick_paint || calcifer_sec || astro_repaint || charging_chg ||
                              charging_ripple_frame || cycle_confirm_frame;

      if (full_paint) {
        s_clock_paint_inited = true;
        g_clock_repaint_pending = false;
        if (charging && face_has_rim) {
          s_last_charge_ripple_paint = now;
        }
        if (g_clock_face == ClockFace::Cycle) {
          s_last_cycle_confirm_paint = now;
        }
        if (valid) {
          s_prev_epoch = epoch;
        }
        if (g_clock_face == ClockFace::Astrology && valid) {
          s_prev_astro_epoch_min = epoch_min_bucket;
        }
        if (banner_chg) {
          strncpy(s_prev_banner, g_gesture_banner, sizeof(s_prev_banner));
          s_prev_banner[sizeof(s_prev_banner) - 1] = '\0';
        }
        s_prev_wifi = wifi;
        if (g_clock_face == ClockFace::Spotify && pm_wifi_connected()) {
          if (!s_spotify_have_data || spotify_stale) {
            pm_spotify_refresh(&g_spotify_ui);
            s_last_spotify_poll_ms = now;
            s_spotify_have_data = true;
          }
        }
        if (g_clock_face == ClockFace::CalciferCountdown && pm_wifi_connected() && valid) {
          if (!s_calcifer_have_data || calcifer_stale) {
            (void)pm_calcifer_fetch(&g_calcifer_ui, epoch);
            s_last_calcifer_poll_ms = now;
            s_calcifer_have_data = true;
          }
        }
        if (g_clock_face == ClockFace::Astrology && wifi && valid &&
            epoch_min_bucket != s_astro_remote_attempt_min) {
          const bool retry_ready = s_astro_remote_retry_after_ms == 0 ||
                                   static_cast<int32_t>(now - s_astro_remote_retry_after_ms) >= 0;
          if (s_astro_remote_have || retry_ready) {
            char err[40] = "";
            PmTransitPositions fetched = {};
            s_astro_remote_attempt_min = epoch_min_bucket;
            if (pm_ephemeris_fetch(epoch, &fetched, err, sizeof(err))) {
              g_astro_remote_tp = fetched;
              s_astro_remote_have = true;
              s_astro_remote_epoch_min = epoch_min_bucket;
              s_astro_remote_retry_after_ms = 0;
            } else if (!s_astro_remote_have) {
              s_astro_remote_retry_after_ms = now + 600000u;
            }
          }
        }
        draw_clock_face();
        if (!valid) {
          s_last_no_time_redraw = now;
        }
      }

      if (g_clock_face == ClockFace::Castalia && wifi && !full_paint && pm_castalia_tick_poll()) {
        g_clock_repaint_pending = true;
      }

      if (ptt_armed && g_pcm) {
        if (g_clock_face == ClockFace::Astrology) {
          if (!pm_wifi_connected()) {
            snprintf(g_gesture_banner, sizeof(g_gesture_banner), "astro: need WiFi");
            g_clock_repaint_pending = true;
            break;
          }
          if (!pm_time_valid()) {
            snprintf(g_gesture_banner, sizeof(g_gesture_banner), "astro: need time");
            g_clock_repaint_pending = true;
            break;
          }
          g_astro_voice_active = true;
          g_astro_voice_pcm = true;
          g_moon_voice_pcm = false;
          s_astro_voice_armed = false;
          s_astro_play_armed = false;
          memset(&g_astro_highlight_plan, 0, sizeof(g_astro_highlight_plan));
        } else if (g_clock_face == ClockFace::Moon) {
          if (!pm_wifi_connected() || !pm_time_valid()) {
            g_clock_repaint_pending = true;
            break;
          }
          g_astro_voice_active = true;
          g_astro_voice_pcm = true;
          g_moon_voice_pcm = true;
          s_astro_voice_armed = false;
          s_astro_play_armed = false;
        } else {
          g_astro_voice_active = false;
          g_astro_voice_pcm = false;
          g_moon_voice_pcm = false;
        }
        reset_recording_buffer();
        if (pm_mic_begin()) {
          pm_gesture_reset();
          s_ptt_press_ms = 0;
          s_rec_mic_on = false;
          g_state = AppState::kRecording;
        }
      }
      break;
    }
    case AppState::kRecording: {
      const size_t frame_bytes = pm_mic_frame_samples() * sizeof(int16_t);
      int16_t frame[512];
      if (pm_mic_frame_samples() > sizeof(frame) / sizeof(frame[0]) || frame_bytes == 0) {
        pm_mic_stop();
        s_rec_mic_on = false;
        recording_progress_end();
        g_astro_voice_active = false;
        g_state = AppState::kClock;
        g_clock_repaint_pending = true;
        break;
      }
      if (!s_rec_mic_on) {
        recording_progress_begin();
        s_rec_mic_on = true;
      }
      if (pm_ptt_button_held()) {
        size_t br = 0;
        if (pm_mic_read_frame(frame, pm_mic_frame_samples(), &br) && br > 0 &&
            g_pcm_len + frame_bytes <= MYNAH_VOICE_MAX_PCM_BYTES) {
          memcpy(g_pcm + g_pcm_len, frame, frame_bytes);
          g_pcm_len += frame_bytes;
        }
      }
      if (g_astro_voice_active) {
        struct tm tm = {};
        if (pm_time_valid()) {
          pm_time_local(&tm);
        }
        gfx->fillScreen(gfx->color565(12, 14, 22));
        draw_astrology_face(&tm, pm_time_valid(), -1, -1, false);
        if (pm_time_valid()) {
          draw_circumference_rainbow_24h(true);
        }
        draw_voice_waves_overlay(false, now);
        gfx->fillRect(0, 0, LCD_WIDTH, 40, gfx->color565(12, 14, 24));
        drawCenteredLine("listening", 12, gfx->color565(220, 200, 255), 1, 1);
        gfx->flush();
      } else {
        draw_voice_wave_screen(false, now, "listening");
      }
      const bool held = pm_ptt_button_held();
      const bool full = g_pcm_len + frame_bytes > MYNAH_VOICE_MAX_PCM_BYTES;
      if (held && !full) {
        break;
      }
      pm_mic_stop();
      s_rec_mic_on = false;
      recording_progress_end();
      if (g_pcm_len < frame_bytes * 2) {
        g_astro_voice_active = false;
        g_state = AppState::kClock;
        g_clock_repaint_pending = true;
        break;
      }
      pm_voice_result_free(&g_voice_result);
      g_voice_use_message = false;
      g_text_voice_route = k_tv_none;
      g_state = AppState::kThinking;
      break;
    }
    case AppState::kThinking: {
      static bool s_voice_job_armed = false;
      static uint32_t s_voice_wait_t0 = 0;
      if (!s_voice_job_armed) {
        s_voice_wait_t0 = now;
        pm_voice_result_free(&g_voice_result);
        bool started = false;
        if (g_calcifer_briefing) {
          started = pm_voice_begin_clock_agenda(&g_voice_result);
        } else if (g_text_voice_route == k_tv_moon) {
          started = pm_voice_begin_message(g_moon_voice_msg, nullptr, &g_voice_result);
        } else if (g_astro_voice_active && !g_astro_voice_pcm) {
          started = pm_voice_begin_message(g_astrology_voice_msg, kAstroVoiceSys, &g_voice_result);
        } else {
          const char *sys = nullptr;
          if (g_moon_voice_pcm) {
            if (!build_moon_system_prompt()) {
              draw_voice_wave_screen(false, now, "moon data fail");
              delay(1200);
              g_astro_voice_active = false;
              g_moon_voice_pcm = false;
              g_state = AppState::kClock;
              g_clock_repaint_pending = true;
              break;
            }
            sys = g_moon_sys_prompt;
          } else if (g_astro_voice_active) {
            if (!build_astrology_system_prompt()) {
              draw_astro_voice_screen("chart data fail", -1, -1, false);
              delay(1200);
              g_astro_voice_active = false;
              g_astro_voice_pcm = false;
              g_state = AppState::kClock;
              g_clock_repaint_pending = true;
              break;
            }
            sys = g_astrology_sys_prompt;
          }
          started = pm_voice_begin_pcm(g_pcm, g_pcm_len, sys, &g_voice_result);
        }
        if (!started) {
          if (g_astro_voice_active) {
            draw_astro_voice_screen("voice start fail", -1, -1, false);
          }
          g_astro_voice_active = false;
          g_astro_voice_pcm = false;
          g_moon_voice_pcm = false;
          g_state = AppState::kClock;
          g_clock_repaint_pending = true;
          break;
        }
        thinking_progress_begin(45000u);
        s_voice_job_armed = true;
      }
      if (g_astro_voice_active) {
        draw_astro_voice_screen(nullptr, -1, -1, false, thinking_progress_now());
      } else if (g_text_voice_route == k_tv_moon) {
        struct tm tm_moon = {};
        const bool valid_moon = pm_time_valid();
        if (valid_moon) {
          pm_time_local(&tm_moon);
        }
        gfx->fillScreen(gfx->color565(10, 12, 20));
        draw_moon_face(&tm_moon, valid_moon);
        if (thinking_progress_now() >= 0.f) {
          draw_circumference_rainbow_24h(valid_moon);
          draw_thinking_progress_ring(thinking_progress_now());
        }
        gfx->flush();
      } else {
        draw_clock_face(thinking_progress_now());
      }
      const PmVoiceStatus vs = pm_voice_poll();
      if (vs == PmVoiceStatus::Working) {
        if (s_voice_wait_t0 != 0 && (now - s_voice_wait_t0) > 100000u) {
          pm_voice_abort();
        } else {
          break;
        }
      }
      s_voice_job_armed = false;
      thinking_progress_end();
      g_voice_use_message = false;
      g_text_voice_route = k_tv_none;
      g_calcifer_briefing = false;
      if (vs != PmVoiceStatus::DoneOk) {
        if (g_astro_voice_active) {
          draw_astro_voice_screen(pm_voice_last_error(), -1, -1, false);
        } else {
          gfx->fillScreen(RGB565_BLACK);
          drawCenteredLine("voice error", 200, RGB565_RED, 2, 2);
          drawCenteredLine(pm_voice_last_error(), 232, gfx->color565(180, 120, 120), 1, 1);
          gfx->flush();
        }
        delay(1500);
        pm_voice_result_free(&g_voice_result);
        g_astro_voice_active = false;
        g_astro_voice_pcm = false;
        g_state = AppState::kClock;
        g_clock_repaint_pending = true;
        break;
      }
      if (!g_voice_result.mp3 || g_voice_result.mp3_len < 64) {
        if (g_astro_voice_active) {
          draw_astro_voice_screen("no audio reply", -1, -1, false);
          delay(1500);
          pm_voice_result_free(&g_voice_result);
          g_astro_voice_active = false;
          g_astro_voice_pcm = false;
          g_state = AppState::kClock;
          g_clock_repaint_pending = true;
          break;
        }
        if (g_voice_result.reply[0] == '\0' && g_voice_result.transcript[0] == '\0') {
          gfx->fillScreen(RGB565_BLACK);
          drawCenteredLine("no reply", 220, RGB565_RED, 2, 2);
          gfx->flush();
          delay(1200);
          pm_voice_result_free(&g_voice_result);
          g_state = AppState::kClock;
          g_clock_repaint_pending = true;
          break;
        }
      }
      if (g_astro_voice_active) {
        pm_astro_highlight_build(g_voice_result.reply, &g_astro_highlight_plan);
      }
      if (g_voice_result.mp3 && g_voice_result.mp3_len >= 64) {
        voice_last_play_save(g_voice_result.mp3, g_voice_result.mp3_len);
      }
      g_astro_voice_pcm = false;
      g_moon_voice_pcm = false;
      g_voice_play_reset = true;
      g_state = AppState::kPlaying;
      break;
    }
    case AppState::kPlaying: {
      static uint32_t s_play_wait_t0 = 0;
      static bool s_play_armed = false;
      if (g_voice_play_reset) {
        g_voice_play_reset = false;
        s_play_wait_t0 = 0;
        s_play_armed = false;
        s_astro_play_armed = false;
      }
      if (s_play_wait_t0 == 0) {
        s_play_wait_t0 = now;
      }
      if (g_astro_voice_active) {
        if (!s_astro_play_armed) {
          if (!g_voice_result.mp3 || g_voice_result.mp3_len < 64) {
            draw_astro_voice_screen("no audio", -1, -1, false);
            delay(1200);
            pm_voice_result_free(&g_voice_result);
            g_astro_voice_active = false;
            g_state = AppState::kClock;
            g_clock_repaint_pending = true;
            break;
          }
          if (!pm_speaker_play_begin(g_voice_result.mp3, g_voice_result.mp3_len)) {
            draw_astro_voice_screen("speaker busy", -1, -1, false);
            delay(1200);
            pm_voice_result_free(&g_voice_result);
            g_astro_voice_active = false;
            g_state = AppState::kClock;
            g_clock_repaint_pending = true;
            break;
          }
          s_astro_play_armed = true;
          s_play_wait_t0 = now;
        }
        int hi_body = -1;
        int hi_sign = -1;
        pm_astro_highlight_at_progress(&g_astro_highlight_plan, pm_speaker_play_progress(), &hi_body, &hi_sign);
        draw_astro_voice_screen(nullptr, hi_body, hi_sign, false);
        const PmSpeakerStatus spk = pm_speaker_poll();
        if (spk == PmSpeakerStatus::Playing) {
          const uint32_t est_ms =
              static_cast<uint32_t>((g_voice_result.mp3_len * 8u * 1000u) / 96000u) + 45000u;
          if (s_play_wait_t0 != 0 && (now - s_play_wait_t0) > est_ms) {
            pm_speaker_abort();
          } else {
            break;
          }
        }
        if (spk == PmSpeakerStatus::DoneFail) {
          draw_astro_voice_screen("playback failed", -1, -1, false);
          delay(1200);
        }
        pm_voice_result_free(&g_voice_result);
        s_astro_play_armed = false;
        g_astro_voice_active = false;
        g_state = AppState::kClock;
        g_clock_repaint_pending = true;
        break;
      }
      if (!g_voice_result.mp3 || g_voice_result.mp3_len < 64) {
        gfx->fillScreen(gfx->color565(18, 28, 42));
        const char *txt = g_voice_result.reply[0] ? g_voice_result.reply : g_voice_result.transcript;
        drawCenteredLine(txt, 210, RGB565_WHITE, 1, 1);
        gfx->flush();
        delay(4500);
        pm_voice_result_free(&g_voice_result);
        s_play_armed = false;
        s_play_wait_t0 = 0;
        g_state = AppState::kClock;
        g_clock_repaint_pending = true;
        break;
      }
      if (!s_play_armed) {
        if (!pm_speaker_play_begin(g_voice_result.mp3, g_voice_result.mp3_len)) {
          draw_voice_wave_screen(true, now, "speaker busy");
          delay(1200);
          pm_voice_result_free(&g_voice_result);
          s_play_armed = false;
          s_play_wait_t0 = 0;
          g_state = AppState::kClock;
          g_clock_repaint_pending = true;
          break;
        }
        s_play_armed = true;
      }
      draw_voice_wave_screen(true, now, "speaking");
      PmSpeakerStatus spk = pm_speaker_poll();
      if (spk == PmSpeakerStatus::Playing) {
        const uint32_t est_ms = static_cast<uint32_t>((g_voice_result.mp3_len * 8u * 1000u) / 96000u) + 30000u;
        if (s_play_wait_t0 != 0 && (now - s_play_wait_t0) > est_ms) {
          pm_speaker_abort();
          spk = pm_speaker_poll();
        } else {
          break;
        }
      }
      if (spk == PmSpeakerStatus::Playing) {
        break;
      }
      if (spk == PmSpeakerStatus::DoneFail) {
        draw_voice_wave_screen(true, now, "playback failed");
        delay(1200);
      }
      pm_voice_result_free(&g_voice_result);
      s_play_armed = false;
      s_play_wait_t0 = 0;
      g_state = AppState::kClock;
      g_clock_repaint_pending = true;
      break;
    }
  }

  delay(12);
}
