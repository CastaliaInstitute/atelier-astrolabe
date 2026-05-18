// PocketMynah MVP: WiFi + NTP hue clock + hold-to-talk (Supabase voice-pipeline: STT / LLM / TTS).

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include <cstdio>
#include <cmath>
#include <ctime>
#include <cstring>
#include <strings.h>

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
#include "pm_birth_nvs.h"
#include "pm_chart_profiles.h"
#include "pm_transit.h"
#include "pm_castalia_auth.h"
#include "pm_calcifer.h"
#include "pm_commonplace.h"
#include "pm_astro_highlight.h"
#include "faces/pm_faces.h"
#include "faces/shared/pm_face_draw.h"
#include "faces/astrology/pm_face_astrology.h"
#include "faces/moon/pm_face_moon.h"
#include "faces/spotify/pm_face_spotify.h"
#include "faces/calcifer/pm_face_calcifer.h"
#include "faces/synastry/pm_face_synastry.h"
#include "faces/metronome/pm_face_metronome.h"
#include "pm_display.h"
#include "pm_qa.h"

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
static constexpr uint8_t k_tv_synastry = 3;
static uint8_t g_text_voice_route = k_tv_none;
static bool g_calcifer_briefing = false;
static char g_moon_voice_msg[2200] = "";
static char g_moon_sys_prompt[640] = "";
static bool g_moon_voice_pcm = false;
/** Tap fortune: stay on Moon face during think/speak. */
static bool g_moon_fortune_active = false;
/** Cached last TTS MP3 for BOOT replay (PSRAM). */
static uint8_t *g_last_play_mp3 = nullptr;
static size_t g_last_play_mp3_len = 0;
/** Reset [kPlaying] static arm state on next entry. */
static bool g_voice_play_reset = false;
static char g_astrology_voice_msg[2200] = "";
static char g_astrology_sys_prompt[2800] = "";
static char g_synastry_voice_msg[2600] = "";
static char g_synastry_sys_prompt[3200] = "";
static bool s_rec_mic_on = false;
/** Astrology voice: stay on chart during record/think/speak + highlight mentions. */
static bool g_astro_voice_active = false;
/** True when astro turn uses recorded PCM (PWR hold); false for BOOT tap text reading. */
static bool g_astro_voice_pcm = false;
/** Synastry voice: stay on dual chart during record/think/speak. */
static bool g_synastry_voice_active = false;
static bool g_synastry_voice_pcm = false;
/** ClassicAnalog PWR hold → mynah-pocket-journal (no voice-pipeline TTS). */
static bool g_commonplace_journal = false;
static bool s_astro_voice_armed = false;
static bool s_astro_play_armed = false;
static bool s_synastry_play_armed = false;
static PmAstroHighlightPlan g_astro_highlight_plan = {};
/** Set when entering clock UI so the face repaints after voice/recording states. */
static bool g_clock_repaint_pending = true;
static uint8_t *g_pcm = nullptr;
static size_t g_pcm_len = 0;
static PmVoiceResult g_voice_result = {};
char g_gesture_banner[44] = "";
/** Last full clock paint background (for second-hand erasure). */
static bool s_spotify_have_data = false;
static uint32_t s_last_spotify_poll_ms = 0;

static uint32_t s_last_calcifer_poll_ms = 0;

#ifndef MYNAH_SPOTIFY_POLL_MS
#define MYNAH_SPOTIFY_POLL_MS 25000u
#endif

/** Spotify transport row (must match draw_spotify_face hit zones). */
static constexpr int kSpotifyBarY = 238;
static constexpr int kSpotifyBarH = 62;
static constexpr int kSpotifyBarPad = 20;

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

/** Apocalypso risk radar (12 axes, 5 rings) — matches apocalypso.castalia.institute RISK PROFILE widget. */
/** Zodiac glyph ring sits just inside the outer chart circle. */
/** Planet dots sit inside the sign band (glyph centers at sign radius). */
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
/** 24h rim: outermost band; hue(sec of day) matches face fill (same formula as draw_clock_face). */
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
  g_synastry_voice_active = false;
  g_synastry_voice_pcm = false;
  g_moon_voice_pcm = false;
  g_calcifer_briefing = false;
  g_voice_play_reset = true;
  g_state = AppState::kPlaying;
  return true;
}

/** `outward`: false = listening waves rim→center; true = speaking waves center→rim. */
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

static const char kAstroBootUserMsg[] =
    "Deliver today's spoken transit reading now (one flowing mini-reading, under 90 seconds).";
static const char kSynastryBootUserMsg[] =
    "Deliver the spoken synastry relationship highlight now (one flowing mini-reading, under 90 seconds).";

/** BOOT on Astrology face or serial `astro`: text-only voice-pipeline turn with full chart in system prompt. */
static bool astrology_begin_boot_reading(void) {
  if (!pm_wifi_connected()) {
    snprintf(g_gesture_banner, sizeof(g_gesture_banner), "astro: need WiFi");
    return false;
  }
  if (!pm_time_valid()) {
    snprintf(g_gesture_banner, sizeof(g_gesture_banner), "astro: need time");
    return false;
  }
  if (!pm_face_astrology_build_system_prompt(g_astrology_voice_msg, sizeof(g_astrology_voice_msg),
                                             g_astrology_sys_prompt, sizeof(g_astrology_sys_prompt))) {
    snprintf(g_gesture_banner, sizeof(g_gesture_banner), "astro: build msg fail");
    return false;
  }
  pm_voice_result_free(&g_voice_result);
  g_voice_use_message = true;
  g_text_voice_route = k_tv_astro;
  g_astro_voice_active = true;
  g_astro_voice_pcm = false;
  g_synastry_voice_active = false;
  g_synastry_voice_pcm = false;
  g_moon_fortune_active = false;
  g_moon_voice_pcm = false;
  g_calcifer_briefing = false;
  s_astro_voice_armed = false;
  s_astro_play_armed = false;
  memset(&g_astro_highlight_plan, 0, sizeof(g_astro_highlight_plan));
  g_state = AppState::kThinking;
  return true;
}

/** BOOT on Synastry face or serial `synastry`: text-only voice-pipeline turn with active chart target. */
static bool synastry_begin_boot_reading(void) {
  if (!pm_wifi_connected()) {
    snprintf(g_gesture_banner, sizeof(g_gesture_banner), "synastry: need WiFi");
    return false;
  }
  if (!pm_face_synastry_build_system_prompt(g_synastry_voice_msg, sizeof(g_synastry_voice_msg),
                                            g_synastry_sys_prompt, sizeof(g_synastry_sys_prompt))) {
    snprintf(g_gesture_banner, sizeof(g_gesture_banner), "synastry: chart data fail");
    return false;
  }
  pm_voice_result_free(&g_voice_result);
  g_voice_use_message = true;
  g_text_voice_route = k_tv_synastry;
  g_synastry_voice_active = true;
  g_synastry_voice_pcm = false;
  g_astro_voice_active = false;
  g_astro_voice_pcm = false;
  g_moon_fortune_active = false;
  g_moon_voice_pcm = false;
  g_calcifer_briefing = false;
  s_synastry_play_armed = false;
  g_state = AppState::kThinking;
  return true;
}

static bool moon_begin_fortune() {
  if (!pm_wifi_connected()) {
    snprintf(g_gesture_banner, sizeof(g_gesture_banner), "moon: need WiFi");
    return false;
  }
  if (!pm_time_valid()) {
    snprintf(g_gesture_banner, sizeof(g_gesture_banner), "moon: need time");
    return false;
  }
  if (!pm_face_moon_build_fortune_message(g_moon_voice_msg, sizeof(g_moon_voice_msg))) {
    snprintf(g_gesture_banner, sizeof(g_gesture_banner), "moon: build fail");
    return false;
  }
  if (!pm_face_moon_build_fortune_system_prompt(g_moon_sys_prompt, sizeof(g_moon_sys_prompt))) {
    snprintf(g_gesture_banner, sizeof(g_gesture_banner), "moon: build fail");
    return false;
  }
  pm_voice_result_free(&g_voice_result);
  g_voice_use_message = true;
  g_text_voice_route = k_tv_moon;
  g_moon_fortune_active = true;
  g_astro_voice_active = false;
  g_astro_voice_pcm = false;
  g_synastry_voice_active = false;
  g_synastry_voice_pcm = false;
  g_moon_voice_pcm = false;
  g_calcifer_briefing = false;
  g_state = AppState::kThinking;
  return true;
}

static bool face_index_from_name(const char *name, int *out) {
  if (!name || !out) {
    return false;
  }
  struct {
    const char *n;
    int idx;
  } k[] = {{"classic", 0},  {"hue", 0},       {"analog", 0},    {"apocalypso", 1},
           {"digital", 2},  {"spotify", 3},   {"astro", 4},       {"astrology", 4},
           {"moon", 5},     {"calcifer", 6},  {"schedule", 6},  {"castalia", 7},
           {"synastry", 8}, {"syn", 8},     {"metronome", 9}, {"metro", 9}};
  for (const auto &e : k) {
    if (strcasecmp(name, e.n) == 0) {
      *out = e.idx;
      return true;
    }
  }
  return false;
}

static void poll_serial_birth_commands() {
  static char line[120];
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
      } else if (strncmp(line, "qa ", 3) == 0) {
        const char *args = line + 3;
        while (*args == ' ') {
          ++args;
        }
        if (strcmp(args, "status") == 0) {
          Serial.printf("qa: face=%d state=%d heap=%u wifi=%d\n",
                        static_cast<int>(pm_faces_current()), static_cast<int>(g_state),
                        static_cast<unsigned>(ESP.getFreeHeap()), pm_wifi_connected() ? 1 : 0);
        } else if (strcmp(args, "faces") == 0) {
          Serial.printf("qa: faces=%d\n", static_cast<int>(ClockFace::kNumFaces));
          Serial.println("qa: 0 classic");
          Serial.println("qa: 1 apocalypso");
          Serial.println("qa: 2 digital");
          Serial.println("qa: 3 spotify");
          Serial.println("qa: 4 astro");
          Serial.println("qa: 5 moon");
          Serial.println("qa: 6 calcifer");
          Serial.println("qa: 7 castalia");
          Serial.println("qa: 8 synastry");
          Serial.println("qa: 9 metronome");
        } else if (!pm_qa_inject_command(args)) {
          Serial.println("qa: usage: status | faces | inject …");
        }
      } else if (strncmp(line, "face ", 5) == 0) {
        int idx = -1;
        const char *p = line + 5;
        while (*p == ' ') {
          ++p;
        }
        char *end = nullptr;
        const long n = strtol(p, &end, 10);
        if (end != p && end && (*end == '\0' || *end == ' ')) {
          idx = static_cast<int>(n);
        } else if (face_index_from_name(p, &idx)) {
          /* ok */
        }
        if (idx >= 0 && idx < static_cast<int>(ClockFace::kNumFaces)) {
          pm_faces_set(static_cast<ClockFace>(idx));
          g_clock_repaint_pending = true;
          Serial.printf("face: %d\n", idx);
        } else {
          Serial.println("face: usage: face <0-9|name>");
        }
      } else if (strcmp(line, "astro") == 0) {
        if (pm_faces_current() != ClockFace::Astrology) {
          Serial.println("astro: swipe to Astrology face first (or: face astro)");
        } else if (g_state != AppState::kClock) {
          Serial.printf("astro: busy (state=%d)\n", static_cast<int>(g_state));
        } else if (astrology_begin_boot_reading()) {
          Serial.println("astro: voice reading started (BOOT/text)");
          g_clock_repaint_pending = true;
        } else {
          Serial.printf("astro: %s\n", g_gesture_banner);
        }
      } else if (strcmp(line, "synastry") == 0 || strcmp(line, "syn") == 0) {
        if (pm_faces_current() != ClockFace::Synastry) {
          Serial.println("synastry: swipe to Synastry face first (or: face synastry)");
        } else if (g_state != AppState::kClock) {
          Serial.printf("synastry: busy (state=%d)\n", static_cast<int>(g_state));
        } else if (synastry_begin_boot_reading()) {
          Serial.println("synastry: voice reading started (BOOT/text)");
          g_clock_repaint_pending = true;
        } else {
          Serial.printf("synastry: %s\n", g_gesture_banner);
        }
      } else if (strcmp(line, "profiles seed") == 0) {
        pm_chart_profiles_ensure_demo_seed();
        Serial.printf("profiles: %d saved\n", pm_chart_profile_count());
        g_clock_repaint_pending = true;
      } else if (strcmp(line, "profiles list") == 0) {
        Serial.println("profiles:");
        for (int i = 0; i < kPmChartProfileSlots; ++i) {
          PmChartProfile profile = {};
          if (!pm_chart_profile_get(i, &profile)) {
            continue;
          }
          Serial.printf("  %d%s: %s (%s) %04u-%02u-%02u %02u:%02u %s\n", i,
                        i == pm_chart_profiles_active_slot() ? "*" : "", profile.name,
                        pm_chart_role_label(profile.role), profile.year, profile.month, profile.day,
                        profile.hour, profile.minute, profile.place);
        }
      } else if (strncmp(line, "profile use ", 12) == 0) {
        int slot = -1;
        if (sscanf(line + 12, "%d", &slot) == 1 && pm_chart_profiles_set_active_slot(slot)) {
          Serial.printf("profile: active slot %d\n", slot);
        } else {
          Serial.println("profile: invalid slot");
        }
        g_clock_repaint_pending = true;
      } else if (strcmp(line, "profile next") == 0 || strcmp(line, "profile prev") == 0) {
        const int delta = strcmp(line, "profile next") == 0 ? 1 : -1;
        PmChartProfile profile = {};
        int slot = -1;
        if (pm_chart_profiles_cycle_active(delta, &slot, &profile)) {
          Serial.printf("profile: slot %d %s (%s)\n", slot, profile.name, pm_chart_role_label(profile.role));
        } else {
          Serial.println("profile: no saved profiles");
        }
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

#ifdef ASTROLABE_QEMU
  (void)pm_touch_begin();
  pm_gesture_reset();
  (void)pm_side_buttons_begin();
  pm_birth_ensure_demo();
  pm_chart_profiles_ensure_demo_seed();
  pm_display_bind(nullptr);
  ensure_pcm_buffer();
  Serial.println("PocketMynah MVP ready");
#else
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
  pm_birth_ensure_demo();
  pm_chart_profiles_ensure_demo_seed();

  if (pm_wifi_begin()) {
    pm_ntp_sync_blocking();
    pm_castalia_warmup_after_wifi();
  }
  pm_display_bind(gfx);
  pm_screen_http_begin(gfx);

  ensure_pcm_buffer();

  Serial.println("PocketMynah MVP ready");
#endif
}

void loop() {
#ifndef ASTROLABE_QEMU
  pm_screen_http_loop();
#endif
  const uint32_t now = millis();
  poll_serial_birth_commands();
  const uint8_t side_ev = pm_side_buttons_poll(now);

  pm_gesture_poll(now);

  PmGestureEvent ge;
  while (pm_gesture_consume(&ge)) {
    if (g_state == AppState::kClock &&
        (ge.kind == PmGestureKind::SwipeLeft || ge.kind == PmGestureKind::SwipeRight)) {
      pm_faces_cycle(ge.kind == PmGestureKind::SwipeLeft ? 1 : -1);
      g_clock_repaint_pending = true;
      g_gesture_banner[0] = '\0';
      Serial.printf("[gesture] face @ %d,%d\n", static_cast<int>(ge.x), static_cast<int>(ge.y));
      continue;
    } else if (g_state == AppState::kClock && pm_faces_current() == ClockFace::Spotify &&
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
        if (pm_face_spotify_hit_transport_bar(ge.x, ge.y, &z)) {
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
    } else if (g_state == AppState::kClock && pm_faces_current() == ClockFace::Synastry &&
               (ge.kind == PmGestureKind::SwipeUp || ge.kind == PmGestureKind::SwipeDown)) {
      if (pm_face_synastry_cycle_target(ge.kind == PmGestureKind::SwipeUp ? 1 : -1)) {
        PmChartProfile profile = {};
        if (pm_chart_profiles_active(&profile)) {
          snprintf(g_gesture_banner, sizeof(g_gesture_banner), "target: %.28s", profile.name);
        } else {
          g_gesture_banner[0] = '\0';
        }
      } else {
        snprintf(g_gesture_banner, sizeof(g_gesture_banner), "synastry: no profiles");
      }
      g_clock_repaint_pending = true;
      continue;
    } else if (g_state == AppState::kClock && pm_faces_current() == ClockFace::Moon &&
               (ge.kind == PmGestureKind::SwipeUp || ge.kind == PmGestureKind::SwipeDown)) {
      pm_faces_cycle(ge.kind == PmGestureKind::SwipeUp ? 1 : -1);
      g_clock_repaint_pending = true;
      g_gesture_banner[0] = '\0';
      continue;
    } else if (g_state == AppState::kClock && pm_faces_current() == ClockFace::Moon &&
               ge.kind == PmGestureKind::Tap) {
      if (moon_begin_fortune()) {
        g_gesture_banner[0] = '\0';
      }
      g_clock_repaint_pending = true;
    } else if (g_state == AppState::kClock && pm_faces_current() == ClockFace::Metronome &&
               (ge.kind == PmGestureKind::SwipeUp || ge.kind == PmGestureKind::SwipeDown)) {
      pm_face_metronome_adjust_bpm(ge.kind == PmGestureKind::SwipeUp ? 5 : -5);
      snprintf(g_gesture_banner, sizeof(g_gesture_banner), "metro: %d bpm", pm_face_metronome_bpm());
      g_clock_repaint_pending = true;
      continue;
    } else if (g_state == AppState::kClock && pm_faces_current() == ClockFace::Metronome &&
               ge.kind == PmGestureKind::Tap) {
      pm_face_metronome_toggle_running();
      snprintf(g_gesture_banner, sizeof(g_gesture_banner), "metro: %s",
               pm_face_metronome_running() ? "run" : "stop");
      g_clock_repaint_pending = true;
      continue;
    } else if (ge.kind != PmGestureKind::SwipeUp && ge.kind != PmGestureKind::SwipeDown) {
      snprintf(g_gesture_banner, sizeof(g_gesture_banner), "%s", gesture_label(ge.kind));
      Serial.printf("[gesture] %s @ %d,%d\n", g_gesture_banner, static_cast<int>(ge.x), static_cast<int>(ge.y));
    }
  }

  if (g_state == AppState::kClock && pm_faces_current() == ClockFace::Astrology &&
      (side_ev & PM_SIDE_BTN_BOOT) != 0) {
    if (!voice_last_play_begin()) {
      if (astrology_begin_boot_reading()) {
        g_gesture_banner[0] = '\0';
      }
      g_clock_repaint_pending = true;
    }
  }

  if (g_state == AppState::kClock && pm_faces_current() == ClockFace::Synastry &&
      (side_ev & PM_SIDE_BTN_BOOT) != 0) {
    if (!voice_last_play_begin()) {
      if (synastry_begin_boot_reading()) {
        g_gesture_banner[0] = '\0';
      }
      g_clock_repaint_pending = true;
    }
  }

  if (g_state == AppState::kClock && pm_faces_current() == ClockFace::Metronome &&
      (side_ev & PM_SIDE_BTN_BOOT) != 0) {
    pm_face_metronome_toggle_running();
    snprintf(g_gesture_banner, sizeof(g_gesture_banner), "metro: %s",
             pm_face_metronome_running() ? "run" : "stop");
    g_clock_repaint_pending = true;
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
      pm_faces_current() != ClockFace::Astrology && pm_faces_current() != ClockFace::Synastry) {
    if (voice_last_play_begin()) {
      /* BOOT replay last TTS */
    } else {
      const bool want_calcifer = (pm_faces_current() == ClockFace::ClassicAnalog ||
                                  pm_faces_current() == ClockFace::DigitalLocal ||
                                  pm_faces_current() == ClockFace::CalciferCountdown);
      if (want_calcifer && pm_wifi_connected() && pm_time_valid() &&
          (now - s_last_clock_boot_brief_ms >= 3500u)) {
        s_last_clock_boot_brief_ms = now;
        pm_voice_result_free(&g_voice_result);
        g_voice_use_message = false;
        g_calcifer_briefing = true;
        g_astro_voice_active = false;
        g_astro_voice_pcm = false;
        g_synastry_voice_active = false;
        g_synastry_voice_pcm = false;
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

      static ClockFace s_prev_dial_face = ClockFace::kNumFaces;
      if (pm_faces_current() != s_prev_dial_face) {
        if (s_prev_dial_face == ClockFace::Metronome) {
          pm_face_metronome_on_face_leave();
        }
        if (pm_faces_current() == ClockFace::Castalia) {
          pm_castalia_on_face_enter();
          g_clock_repaint_pending = true;
        }
        s_prev_dial_face = pm_faces_current();
      }

      if (pm_faces_current() == ClockFace::Metronome) {
        (void)pm_face_metronome_tick(now);
      }

      if (pm_faces_current() == ClockFace::Castalia && wifi && pm_castalia_tick_pair_start()) {
        g_clock_repaint_pending = true;
      }

      if (wifi && pm_castalia_has_session()) {
        (void)pm_castalia_tick_refresh_session();
      }

      const bool sec_tick = valid && (epoch != s_prev_epoch);
      const bool slow_no_time =
          !valid && s_clock_paint_inited && (now - s_last_no_time_redraw >= 12000);
      const bool banner_chg = strcmp(g_gesture_banner, s_prev_banner) != 0;
      const bool wifi_chg = (wifi != s_prev_wifi);
      const bool local_hm_chg =
          valid && pm_faces_local_hm_changed(tm_now.tm_hour, tm_now.tm_min);

      if (pm_faces_current() != ClockFace::Spotify) {
        s_spotify_have_data = false;
      }
      if (pm_faces_current() != ClockFace::CalciferCountdown) {
        s_calcifer_have_data = false;
      }

      const bool spotify_stale =
          pm_faces_current() == ClockFace::Spotify && pm_wifi_connected() && s_spotify_have_data &&
          (now - s_last_spotify_poll_ms >= MYNAH_SPOTIFY_POLL_MS);

      const bool calcifer_stale =
          pm_faces_current() == ClockFace::CalciferCountdown && pm_wifi_connected() && valid &&
          (!s_calcifer_have_data || (now - s_last_calcifer_poll_ms >= MYNAH_CALCIFER_POLL_MS));

      static time_t s_prev_astro_epoch_min = -1;
      const time_t epoch_min_bucket = valid ? (epoch / 60) : -1;
      const bool astro_repaint =
          pm_faces_current() == ClockFace::Astrology && valid && epoch_min_bucket != s_prev_astro_epoch_min;

      const bool sec_tick_paint =
          sec_tick && pm_faces_current() != ClockFace::Castalia && pm_faces_current() != ClockFace::CalciferCountdown &&
          pm_faces_current() != ClockFace::Synastry && pm_faces_current() != ClockFace::Metronome;
      const bool calcifer_sec =
          pm_faces_current() == ClockFace::CalciferCountdown && valid && sec_tick;
      const bool metronome_anim =
          pm_faces_current() == ClockFace::Metronome && pm_face_metronome_wants_repaint(now);
      const bool full_paint = !s_clock_paint_inited || slow_no_time || banner_chg || wifi_chg ||
                              g_clock_repaint_pending || local_hm_chg || spotify_stale || calcifer_stale ||
                              sec_tick_paint || calcifer_sec || astro_repaint || metronome_anim;

      if (full_paint) {
        s_clock_paint_inited = true;
        g_clock_repaint_pending = false;
        if (valid) {
          s_prev_epoch = epoch;
        }
        if (pm_faces_current() == ClockFace::Astrology && valid) {
          s_prev_astro_epoch_min = epoch_min_bucket;
        }
        if (banner_chg) {
          strncpy(s_prev_banner, g_gesture_banner, sizeof(s_prev_banner));
          s_prev_banner[sizeof(s_prev_banner) - 1] = '\0';
        }
        s_prev_wifi = wifi;
        if (pm_faces_current() == ClockFace::Spotify && pm_wifi_connected()) {
          if (!s_spotify_have_data || spotify_stale) {
            pm_spotify_refresh(&g_spotify_ui);
            s_last_spotify_poll_ms = now;
            s_spotify_have_data = true;
          }
        }
        if (pm_faces_current() == ClockFace::CalciferCountdown && pm_wifi_connected() && valid) {
          if (!s_calcifer_have_data || calcifer_stale) {
            (void)pm_calcifer_fetch(&g_calcifer_ui, epoch);
            s_last_calcifer_poll_ms = now;
            s_calcifer_have_data = true;
          }
        }
        if (pm_gfx) {
          pm_faces_draw();
        }
        if (!valid) {
          s_last_no_time_redraw = now;
        }
      }

      if (pm_faces_current() == ClockFace::Castalia && wifi && !full_paint && pm_castalia_tick_poll()) {
        g_clock_repaint_pending = true;
      }

      if (ptt_armed && g_pcm) {
        if (pm_faces_current() == ClockFace::Astrology) {
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
          g_commonplace_journal = false;
          g_astro_voice_active = true;
          g_astro_voice_pcm = true;
          g_moon_voice_pcm = false;
          s_astro_voice_armed = false;
          s_astro_play_armed = false;
          memset(&g_astro_highlight_plan, 0, sizeof(g_astro_highlight_plan));
        } else if (pm_faces_current() == ClockFace::Synastry) {
          if (!pm_wifi_connected()) {
            snprintf(g_gesture_banner, sizeof(g_gesture_banner), "synastry: need WiFi");
            g_clock_repaint_pending = true;
            break;
          }
          g_commonplace_journal = false;
          g_astro_voice_active = false;
          g_astro_voice_pcm = false;
          g_synastry_voice_active = true;
          g_synastry_voice_pcm = true;
          g_moon_voice_pcm = false;
          s_synastry_play_armed = false;
        } else if (pm_faces_current() == ClockFace::Moon) {
          if (!pm_wifi_connected() || !pm_time_valid()) {
            g_clock_repaint_pending = true;
            break;
          }
          g_commonplace_journal = false;
          g_astro_voice_active = true;
          g_astro_voice_pcm = true;
          g_synastry_voice_active = false;
          g_synastry_voice_pcm = false;
          g_moon_voice_pcm = true;
          s_astro_voice_armed = false;
          s_astro_play_armed = false;
        } else if (pm_faces_is_commonplace_home()) {
          if (!pm_wifi_connected()) {
            snprintf(g_gesture_banner, sizeof(g_gesture_banner), "journal: need WiFi");
            g_clock_repaint_pending = true;
            break;
          }
          if (!pm_castalia_has_session()) {
            snprintf(g_gesture_banner, sizeof(g_gesture_banner), "journal: sign in");
            g_clock_repaint_pending = true;
            break;
          }
          g_commonplace_journal = true;
          g_astro_voice_active = false;
          g_astro_voice_pcm = false;
          g_synastry_voice_active = false;
          g_synastry_voice_pcm = false;
          g_moon_voice_pcm = false;
        } else {
          g_commonplace_journal = false;
          g_astro_voice_active = false;
          g_astro_voice_pcm = false;
          g_synastry_voice_active = false;
          g_synastry_voice_pcm = false;
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
        g_synastry_voice_active = false;
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
      if (g_synastry_voice_active) {
        pm_gfx->fillScreen(pm_gfx->color565(10, 12, 22));
        pm_face_synastry_draw(nullptr, false);
        pm_face_draw_voice_waves_overlay(false, now);
        pm_gfx->fillRect(0, 0, LCD_WIDTH, 40, pm_gfx->color565(10, 12, 22));
        pm_face_draw_centered_line("listening", 12, pm_gfx->color565(230, 210, 255), 1, 1);
        pm_gfx->flush();
      } else if (g_astro_voice_active) {
        struct tm tm = {};
        if (pm_time_valid()) {
          pm_time_local(&tm);
        }
        gfx->fillScreen(gfx->color565(12, 14, 22));
        pm_face_astrology_draw(&tm, pm_time_valid(), -1, -1, false);
        if (pm_time_valid()) {
          pm_face_draw_circumference_rainbow_24h(true);
        }
        pm_face_draw_voice_waves_overlay(false, now);
        gfx->fillRect(0, 0, LCD_WIDTH, 40, gfx->color565(12, 14, 24));
        pm_face_draw_centered_line("listening", 12, gfx->color565(220, 200, 255), 1, 1);
        gfx->flush();
      } else if (g_commonplace_journal) {
        pm_face_draw_voice_wave_screen(false, now, "journal");
      } else {
        pm_face_draw_voice_wave_screen(false, now, "listening");
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
        g_synastry_voice_active = false;
        g_commonplace_journal = false;
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
      static bool s_commonplace_armed = false;
      static uint32_t s_voice_wait_t0 = 0;

      if (g_commonplace_journal) {
        if (!s_commonplace_armed) {
          s_voice_wait_t0 = now;
          if (!pm_commonplace_begin_pcm_journal(g_pcm, g_pcm_len)) {
            snprintf(g_gesture_banner, sizeof(g_gesture_banner), "journal: busy");
            g_commonplace_journal = false;
            g_state = AppState::kClock;
            g_clock_repaint_pending = true;
            break;
          }
          thinking_progress_begin(90000u);
          s_commonplace_armed = true;
        }
        pm_faces_draw(thinking_progress_now());
        pm_face_draw_centered_line("saving journal", 12, gfx->color565(200, 210, 240), 1, 1);
        gfx->flush();
        const PmCommonplaceStatus cps = pm_commonplace_poll();
        if (cps == PmCommonplaceStatus::Working) {
          if (s_voice_wait_t0 != 0 && (now - s_voice_wait_t0) > 120000u) {
            pm_commonplace_abort();
          } else {
            break;
          }
        }
        s_commonplace_armed = false;
        thinking_progress_end();
        g_commonplace_journal = false;
        if (cps == PmCommonplaceStatus::DoneOk) {
          const char *tr = pm_commonplace_last_transcript();
          if (tr && tr[0] != '\0') {
            snprintf(g_gesture_banner, sizeof(g_gesture_banner), "saved: %.30s%s", tr,
                     strlen(tr) > 30 ? "…" : "");
          } else {
            snprintf(g_gesture_banner, sizeof(g_gesture_banner), "commonplace: saved");
          }
        } else {
          snprintf(g_gesture_banner, sizeof(g_gesture_banner), "journal: %s",
                   pm_commonplace_last_error());
        }
        g_state = AppState::kClock;
        g_clock_repaint_pending = true;
        break;
      }
      s_commonplace_armed = false;

      if (!s_voice_job_armed) {
        s_voice_wait_t0 = now;
        pm_voice_result_free(&g_voice_result);
        bool started = false;
        if (g_calcifer_briefing) {
          started = pm_voice_begin_clock_agenda(&g_voice_result);
        } else if (g_text_voice_route == k_tv_moon) {
          started = pm_voice_begin_message(g_moon_voice_msg, g_moon_sys_prompt, &g_voice_result);
        } else if (g_text_voice_route == k_tv_synastry) {
          started = pm_voice_begin_message(kSynastryBootUserMsg, g_synastry_sys_prompt, &g_voice_result);
        } else if (g_astro_voice_active && !g_astro_voice_pcm) {
          started = pm_voice_begin_message(kAstroBootUserMsg, g_astrology_sys_prompt, &g_voice_result);
        } else {
          const char *sys = nullptr;
          if (g_moon_voice_pcm) {
            if (!pm_face_moon_build_system_prompt(g_moon_sys_prompt, sizeof(g_moon_sys_prompt))) {
              pm_face_draw_voice_wave_screen(false, now, "moon data fail");
              delay(1200);
              g_astro_voice_active = false;
              g_moon_voice_pcm = false;
              g_state = AppState::kClock;
              g_clock_repaint_pending = true;
              break;
            }
            sys = g_moon_sys_prompt;
          } else if (g_astro_voice_active) {
            if (!pm_face_astrology_build_system_prompt(g_astrology_voice_msg, sizeof(g_astrology_voice_msg), g_astrology_sys_prompt, sizeof(g_astrology_sys_prompt))) {
              pm_face_astrology_draw_voice_screen("chart data fail", -1, -1, false);
              delay(1200);
              g_astro_voice_active = false;
              g_astro_voice_pcm = false;
              g_state = AppState::kClock;
              g_clock_repaint_pending = true;
              break;
            }
            sys = g_astrology_sys_prompt;
          } else if (g_synastry_voice_active) {
            if (!pm_face_synastry_build_system_prompt(g_synastry_voice_msg, sizeof(g_synastry_voice_msg),
                                                      g_synastry_sys_prompt,
                                                      sizeof(g_synastry_sys_prompt))) {
              pm_face_synastry_draw_voice_screen("chart data fail");
              delay(1200);
              g_synastry_voice_active = false;
              g_synastry_voice_pcm = false;
              g_state = AppState::kClock;
              g_clock_repaint_pending = true;
              break;
            }
            sys = g_synastry_sys_prompt;
          }
          started = pm_voice_begin_pcm(g_pcm, g_pcm_len, sys, &g_voice_result);
        }
        if (!started) {
          if (g_synastry_voice_active) {
            pm_face_synastry_draw_voice_screen("voice start fail");
          } else if (g_astro_voice_active) {
            pm_face_astrology_draw_voice_screen("voice start fail", -1, -1, false);
          }
          g_astro_voice_active = false;
          g_astro_voice_pcm = false;
          g_synastry_voice_active = false;
          g_synastry_voice_pcm = false;
          g_moon_voice_pcm = false;
          g_state = AppState::kClock;
          g_clock_repaint_pending = true;
          break;
        }
        thinking_progress_begin((g_astro_voice_active || g_synastry_voice_active) ? 180000u : 45000u);
        s_voice_job_armed = true;
      }
      if (g_synastry_voice_active) {
        pm_face_synastry_draw_voice_screen(nullptr, thinking_progress_now());
      } else if (g_astro_voice_active) {
        pm_face_astrology_draw_voice_screen(nullptr, -1, -1, false, thinking_progress_now());
      } else if (g_moon_fortune_active) {
        pm_face_moon_draw_voice_screen(nullptr, thinking_progress_now());
      } else {
        pm_faces_draw(thinking_progress_now());
      }
      const PmVoiceStatus vs = pm_voice_poll();
      if (vs == PmVoiceStatus::Working) {
        const uint32_t voice_wait_ms =
            (g_moon_fortune_active || g_astro_voice_active || g_synastry_voice_active) ? 620000u : 100000u;
        if (s_voice_wait_t0 != 0 && (now - s_voice_wait_t0) > voice_wait_ms) {
          Serial.println("astro: voice wait timeout — abort");
          pm_voice_abort();
        } else {
          break;
        }
      }
      if (g_astro_voice_active) {
        Serial.printf("astro: voice done status=%d mp3=%u err=%s\n", static_cast<int>(vs),
                      static_cast<unsigned>(g_voice_result.mp3_len), pm_voice_last_error());
      } else if (g_synastry_voice_active) {
        Serial.printf("synastry: voice done status=%d mp3=%u err=%s\n", static_cast<int>(vs),
                      static_cast<unsigned>(g_voice_result.mp3_len), pm_voice_last_error());
      }
      s_voice_job_armed = false;
      thinking_progress_end();
      g_voice_use_message = false;
      g_text_voice_route = k_tv_none;
      g_calcifer_briefing = false;
      if (vs != PmVoiceStatus::DoneOk) {
        if (g_synastry_voice_active) {
          pm_face_synastry_draw_voice_screen(pm_voice_last_error());
        } else if (g_astro_voice_active) {
          pm_face_astrology_draw_voice_screen(pm_voice_last_error(), -1, -1, false);
        } else if (g_moon_fortune_active) {
          pm_face_moon_draw_voice_screen(pm_voice_last_error(), -1.f);
          delay(1500);
        } else {
          gfx->fillScreen(RGB565_BLACK);
          pm_face_draw_centered_line("voice error", 200, RGB565_RED, 2, 2);
          pm_face_draw_centered_line(pm_voice_last_error(), 232, gfx->color565(180, 120, 120), 1, 1);
          gfx->flush();
        }
        delay(1500);
        pm_voice_result_free(&g_voice_result);
        g_astro_voice_active = false;
        g_astro_voice_pcm = false;
        g_synastry_voice_active = false;
        g_synastry_voice_pcm = false;
        g_moon_fortune_active = false;
        g_state = AppState::kClock;
        g_clock_repaint_pending = true;
        break;
      }
      if (!g_voice_result.mp3 || g_voice_result.mp3_len < 64) {
        if (g_synastry_voice_active) {
          pm_face_synastry_draw_voice_screen("no audio reply");
          delay(1500);
          pm_voice_result_free(&g_voice_result);
          g_synastry_voice_active = false;
          g_synastry_voice_pcm = false;
          g_state = AppState::kClock;
          g_clock_repaint_pending = true;
          break;
        }
        if (g_astro_voice_active) {
          pm_face_astrology_draw_voice_screen("no audio reply", -1, -1, false);
          delay(1500);
          pm_voice_result_free(&g_voice_result);
          g_astro_voice_active = false;
          g_astro_voice_pcm = false;
          g_state = AppState::kClock;
          g_clock_repaint_pending = true;
          break;
        }
        if (g_moon_fortune_active) {
          pm_face_moon_draw_voice_screen("no audio reply", -1.f);
          delay(1500);
          pm_voice_result_free(&g_voice_result);
          g_moon_fortune_active = false;
          g_state = AppState::kClock;
          g_clock_repaint_pending = true;
          break;
        }
        if (g_voice_result.reply[0] == '\0' && g_voice_result.transcript[0] == '\0') {
          gfx->fillScreen(RGB565_BLACK);
          pm_face_draw_centered_line("no reply", 220, RGB565_RED, 2, 2);
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
      g_synastry_voice_pcm = false;
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
        s_synastry_play_armed = false;
      }
      if (s_play_wait_t0 == 0) {
        s_play_wait_t0 = now;
      }
      if (g_astro_voice_active) {
        if (!s_astro_play_armed) {
          if (!g_voice_result.mp3 || g_voice_result.mp3_len < 64) {
            pm_face_astrology_draw_voice_screen("no audio", -1, -1, false);
            delay(1200);
            pm_voice_result_free(&g_voice_result);
            g_astro_voice_active = false;
            g_state = AppState::kClock;
            g_clock_repaint_pending = true;
            break;
          }
          if (!pm_speaker_play_begin(g_voice_result.mp3, g_voice_result.mp3_len)) {
            pm_face_astrology_draw_voice_screen("speaker busy", -1, -1, false);
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
        pm_face_astrology_draw_voice_screen(nullptr, hi_body, hi_sign, false);
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
          pm_face_astrology_draw_voice_screen("playback failed", -1, -1, false);
          delay(1200);
        }
        pm_voice_result_free(&g_voice_result);
        s_astro_play_armed = false;
        g_astro_voice_active = false;
        g_state = AppState::kClock;
        g_clock_repaint_pending = true;
        break;
      }
      if (g_synastry_voice_active) {
        if (!s_synastry_play_armed) {
          if (!g_voice_result.mp3 || g_voice_result.mp3_len < 64) {
            pm_face_synastry_draw_voice_screen("no audio");
            delay(1200);
            pm_voice_result_free(&g_voice_result);
            g_synastry_voice_active = false;
            g_state = AppState::kClock;
            g_clock_repaint_pending = true;
            break;
          }
          if (!pm_speaker_play_begin(g_voice_result.mp3, g_voice_result.mp3_len)) {
            pm_face_synastry_draw_voice_screen("speaker busy");
            delay(1200);
            pm_voice_result_free(&g_voice_result);
            g_synastry_voice_active = false;
            g_state = AppState::kClock;
            g_clock_repaint_pending = true;
            break;
          }
          s_synastry_play_armed = true;
          s_play_wait_t0 = now;
        }
        pm_face_synastry_draw_voice_screen(nullptr);
        pm_face_draw_voice_waves_overlay(true, now);
        pm_gfx->fillRect(0, LCD_HEIGHT - 40, LCD_WIDTH, 40, pm_gfx->color565(10, 12, 22));
        pm_face_draw_centered_line("speaking", LCD_HEIGHT - 28, pm_gfx->color565(220, 210, 245), 1, 1);
        pm_gfx->flush();
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
          pm_face_synastry_draw_voice_screen("playback failed");
          delay(1200);
        }
        pm_voice_result_free(&g_voice_result);
        s_synastry_play_armed = false;
        s_play_wait_t0 = 0;
        g_synastry_voice_active = false;
        g_state = AppState::kClock;
        g_clock_repaint_pending = true;
        break;
      }
      if (g_moon_fortune_active) {
        if (!s_play_armed) {
          if (!g_voice_result.mp3 || g_voice_result.mp3_len < 64) {
            pm_face_moon_draw_voice_screen("no audio", -1.f);
            delay(1200);
            pm_voice_result_free(&g_voice_result);
            g_moon_fortune_active = false;
            g_state = AppState::kClock;
            g_clock_repaint_pending = true;
            break;
          }
          if (!pm_speaker_play_begin(g_voice_result.mp3, g_voice_result.mp3_len)) {
            pm_face_moon_draw_voice_screen("speaker busy", -1.f);
            delay(1200);
            pm_voice_result_free(&g_voice_result);
            g_moon_fortune_active = false;
            g_state = AppState::kClock;
            g_clock_repaint_pending = true;
            break;
          }
          s_play_armed = true;
          s_play_wait_t0 = now;
        }
        pm_face_moon_draw_voice_screen(nullptr, -1.f);
        pm_face_draw_voice_waves_overlay(true, now);
        pm_gfx->fillRect(0, LCD_HEIGHT - 40, LCD_WIDTH, 40, pm_gfx->color565(8, 10, 18));
        pm_face_draw_centered_line("speaking", LCD_HEIGHT - 28, pm_gfx->color565(200, 210, 230), 1, 1);
        pm_gfx->flush();
        PmSpeakerStatus spk = pm_speaker_poll();
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
          pm_face_moon_draw_voice_screen("playback failed", -1.f);
          delay(1200);
        }
        pm_voice_result_free(&g_voice_result);
        s_play_armed = false;
        s_play_wait_t0 = 0;
        g_moon_fortune_active = false;
        g_state = AppState::kClock;
        g_clock_repaint_pending = true;
        break;
      }
      if (!g_voice_result.mp3 || g_voice_result.mp3_len < 64) {
        gfx->fillScreen(gfx->color565(18, 28, 42));
        const char *txt = g_voice_result.reply[0] ? g_voice_result.reply : g_voice_result.transcript;
        pm_face_draw_centered_line(txt, 210, RGB565_WHITE, 1, 1);
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
          pm_face_draw_voice_wave_screen(true, now, "speaker busy");
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
      pm_face_draw_voice_wave_screen(true, now, "speaking");
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
        pm_face_draw_voice_wave_screen(true, now, "playback failed");
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
