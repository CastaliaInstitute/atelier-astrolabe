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

Arduino_DataBus *bus = new Arduino_ESP32QSPI(
    LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);

Arduino_CO5300 *tft = new Arduino_CO5300(
    bus, LCD_RESET, 0, false, LCD_WIDTH, LCD_HEIGHT, 6, 0, 0, 0);
/** Full-framebuffer canvas; flush() pushes pixels to the CO5300 (enables WiFi BMP grab). */
Arduino_Canvas *gfx = new Arduino_Canvas(LCD_WIDTH, LCD_HEIGHT, tft);

enum class AppState { kClock, kRecording, kThinking, kPlaying };

static AppState g_state = AppState::kClock;
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
static constexpr int kAnalogCy = LCD_HEIGHT / 2 + 6;
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
  drawCenteredLine("local time", 330, gfx->color565(180, 190, 210), 1, 1);
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

/** 24h rim: outermost band; hue(sec of day) matches face fill (same formula as draw_clock_face). */
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
      /** `af` = 0 at top → midnight; same mapping as `sec_of_day * (360/86400)` on the face. */
      const float sec_of_day = af * (86400.f / kTwoPi);
      hue_deg = wrap360(sec_of_day * (360.f / 86400.f));
    } else {
      hue_deg = wrap360(af * (360.f / kTwoPi) + fmodf(static_cast<float>(millis()) * 0.025f, 360.f));
    }
    const uint16_t col = color565FromHsv(gfx, hue_deg, k_clock_face_hsv_s, k_clock_face_hsv_v);
    draw_radial_annulus_slice(cx, cy, amid, r_inner, r_outer, col, k_half_w);
  }
}

static void draw_clock_face() {
  struct tm tm = {};
  int sec_of_day_for_hue = 0;
  if (pm_time_valid()) {
    pm_time_local(&tm);
    sec_of_day_for_hue = tm.tm_hour * 3600 + tm.tm_min * 60 + tm.tm_sec;
  }
  const float hue =
      pm_time_valid() ? static_cast<float>(sec_of_day_for_hue) * (360.0f / 86400.0f)
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
    default:
      break;
  }

  const int banner_y = (g_clock_face == ClockFace::Apocalypso || g_clock_face == ClockFace::Spotify)
                           ? 352
                           : 320;
  if (g_gesture_banner[0] != '\0') {
    drawCenteredLine(g_gesture_banner, banner_y, gfx->color565(255, 220, 160), 1, 1);
  }

  /** Rainbow annulus last so the gesture banner does not paint over it. */
  draw_circumference_rainbow_24h(pm_time_valid());
  g_clock_bg565 = bg;
  if (pm_time_valid()) {
    g_analog_saved_local_h = tm.tm_hour;
    g_analog_saved_local_m = tm.tm_min;
  }
  gfx->flush();
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
  tft->setBrightness(200);
  gfx->fillScreen(RGB565_BLACK);
  gfx->flush();

  (void)pm_touch_begin();
  pm_gesture_reset();
  (void)pm_side_buttons_begin();

  if (pm_wifi_begin()) {
    pm_ntp_sync_blocking();
  }
  pm_screen_http_begin(gfx);

  ensure_pcm_buffer();

  Serial.println("PocketMynah MVP ready");
}

void loop() {
  pm_screen_http_loop();
  const uint32_t now = millis();
  (void)pm_side_buttons_poll(now);

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
        snprintf(g_gesture_banner, sizeof(g_gesture_banner), "spotify: next");
      } else if (ge.kind == PmGestureKind::SwipeDown) {
        pm_spotify_command("previous", &g_spotify_ui);
        snprintf(g_gesture_banner, sizeof(g_gesture_banner), "spotify: prev");
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
    } else {
      snprintf(g_gesture_banner, sizeof(g_gesture_banner), "%s", gesture_label(ge.kind));
    }
    Serial.printf("[gesture] %s @ %d,%d\n", g_gesture_banner, static_cast<int>(ge.x), static_cast<int>(ge.y));
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

  switch (g_state) {
    case AppState::kClock: {
      static bool s_clock_paint_inited = false;
      static time_t s_prev_epoch = -3;
      static bool s_prev_wifi = false;
      static char s_prev_banner[44] = "";
      static uint32_t s_last_ntp_retry_wall = 0;
      static uint32_t s_last_no_time_redraw = 0;

      const bool wifi = pm_wifi_connected();
      const bool valid = pm_time_valid();
      const time_t epoch = time(nullptr);

      struct tm tm_now = {};
      if (valid) {
        pm_time_local(&tm_now);
      }

      if (wifi && !valid && (now - s_last_ntp_retry_wall > 60000)) {
        s_last_ntp_retry_wall = now;
        pm_ntp_retry_if_stale();
      }

      const bool sec_tick = valid && (epoch != s_prev_epoch);
      const bool slow_no_time =
          !valid && s_clock_paint_inited && (now - s_last_no_time_redraw >= 12000);
      const bool banner_chg = strcmp(g_gesture_banner, s_prev_banner) != 0;
      const bool wifi_chg = (wifi != s_prev_wifi);
      const bool local_hm_chg =
          valid && (g_analog_saved_local_h < 0 || tm_now.tm_hour != g_analog_saved_local_h ||
                    tm_now.tm_min != g_analog_saved_local_m);

      if (g_clock_face != ClockFace::Spotify) {
        s_spotify_have_data = false;
      }

      const bool spotify_stale =
          g_clock_face == ClockFace::Spotify && pm_wifi_connected() && s_spotify_have_data &&
          (now - s_last_spotify_poll_ms >= MYNAH_SPOTIFY_POLL_MS);

      const bool full_paint = !s_clock_paint_inited || slow_no_time || banner_chg || wifi_chg ||
                              g_clock_repaint_pending || local_hm_chg || spotify_stale ||
                              (valid && sec_tick);

      if (full_paint) {
        s_clock_paint_inited = true;
        g_clock_repaint_pending = false;
        if (valid) {
          s_prev_epoch = epoch;
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
        draw_clock_face();
        if (!valid) {
          s_last_no_time_redraw = now;
        }
      }

      if (ptt_armed && g_pcm) {
        reset_recording_buffer();
        if (pm_mic_begin()) {
          pm_gesture_reset();
          s_ptt_press_ms = 0;
          g_state = AppState::kRecording;
        }
      }
      break;
    }
    case AppState::kRecording: {
      gfx->fillScreen(RGB565_RED);
      drawCenteredLine("listening", 220, RGB565_WHITE, 2, 2);
      gfx->flush();
      const size_t frame_bytes = pm_mic_frame_samples() * sizeof(int16_t);
      int16_t frame[512];
      if (pm_mic_frame_samples() > sizeof(frame) / sizeof(frame[0])) {
        g_state = AppState::kClock;
        g_clock_repaint_pending = true;
        pm_mic_stop();
        break;
      }
      for (;;) {
        size_t br = 0;
        if (!pm_mic_read_frame(frame, pm_mic_frame_samples(), &br)) {
          break;
        }
        if (g_pcm_len + frame_bytes > MYNAH_VOICE_MAX_PCM_BYTES) {
          break;
        }
        memcpy(g_pcm + g_pcm_len, frame, frame_bytes);
        g_pcm_len += frame_bytes;
        if (!pm_ptt_button_held()) {
          break;
        }
      }
      pm_mic_stop();
      if (g_pcm_len < frame_bytes * 2) {
        g_state = AppState::kClock;
        g_clock_repaint_pending = true;
        break;
      }
      pm_voice_result_free(&g_voice_result);
      g_state = AppState::kThinking;
      break;
    }
    case AppState::kThinking: {
      gfx->fillScreen(gfx->color565(30, 30, 60));
      drawCenteredLine("thinking", 220, RGB565_WHITE, 2, 2);
      gfx->flush();
      const bool ok = pm_voice_post_pcm(g_pcm, g_pcm_len, &g_voice_result);
      if (!ok) {
        gfx->fillScreen(RGB565_BLACK);
        drawCenteredLine("voice error", 220, RGB565_RED, 2, 2);
        gfx->flush();
        delay(1500);
        g_state = AppState::kClock;
        g_clock_repaint_pending = true;
        break;
      }
      g_state = AppState::kPlaying;
      break;
    }
    case AppState::kPlaying: {
      gfx->fillScreen(gfx->color565(20, 40, 30));
      drawCenteredLine("speaking", 200, RGB565_WHITE, 2, 2);
      gfx->flush();
      (void)pm_speaker_play_mp3(g_voice_result.mp3, g_voice_result.mp3_len);
      pm_voice_result_free(&g_voice_result);
      g_state = AppState::kClock;
      g_clock_repaint_pending = true;
      break;
    }
  }

  delay(12);
}
