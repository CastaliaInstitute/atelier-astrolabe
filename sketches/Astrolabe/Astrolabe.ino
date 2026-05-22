// Mynah Astrolabe: WiFi + NTP hue clock + hold-to-talk (Supabase voice-pipeline: STT / LLM / TTS).

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include <cstdio>
#include <cmath>
#include <ctime>
#include <cstring>
#include <strings.h>
#include <mbedtls/base64.h>

#include "esp_heap_caps.h"
#include <esp_system.h>
#include "esp32-hal-tinyusb.h"
#include "minimp3.h"

#include "pin_config.h"
#include "pm_config.h"
#include "pm_gesture.h"
#include "pm_geo_tz.h"
#include "pm_mic.h"
#include "pm_nvs.h"
#include "pm_side_buttons.h"
#include "pm_speaker.h"
#include "pm_audio_route.h"
#include "pm_usb_uac.h"
#include "pm_touch.h"
#include "pm_spotify.h"
#include "pm_voice.h"
#include "pm_wifi_creds.h"
#include "pm_wifi_ntp.h"
#include "pm_screen_http.h"
#include "pm_birth_nvs.h"
#include "pm_chart_profiles.h"
#include "pm_transit.h"
#include "pm_castalia_auth.h"
#include "pm_calcifer.h"
#include "pm_rocket.h"
#include "pm_commonplace.h"
#include "pm_cycle_nvs.h"
#include "pm_astro_highlight.h"
#include "faces/pm_faces.h"
#include "faces/shared/pm_face_draw.h"
#include "faces/alethiometer/pm_face_alethiometer.h"
#include "faces/astrology/pm_face_astrology.h"
#include "faces/bongo/pm_face_bongo.h"
#include "faces/chakra/pm_face_chakra.h"
#include "faces/ocarina/pm_face_ocarina.h"
#include "faces/pandrum/pm_face_pandrum.h"
#include "faces/piano/pm_face_piano.h"
#include "faces/tibetan_bowl/pm_face_tibetan_bowl.h"
#include "faces/moon/pm_face_moon.h"
#include "faces/runes/pm_face_runes.h"
#include "faces/tarot/pm_face_tarot.h"
#include "faces/notes/pm_face_notes.h"
#include "faces/spotify/pm_face_spotify.h"
#include "faces/calcifer/pm_face_calcifer.h"
#include "faces/level/pm_face_level.h"
#include "faces/cycle/pm_face_cycle.h"
#include "faces/weather/pm_face_weather.h"
#include "faces/settings/pm_face_settings_aec.h"
#include "faces/settings/pm_face_settings_wifi.h"
#include "pm_weather.h"
#include "faces/quotes/pm_face_quotes.h"
#include "pm_quotes.h"
#include "faces/spectrum/pm_face_spectrum.h"
#include "faces/synastry/pm_face_synastry.h"
#include "faces/radar/pm_face_radar.h"
#include "pm_presence.h"
#include "pm_motion.h"
#include "pm_faculty.h"
#include "pm_audio_analyzer.h"
#include "faces/rocket/pm_face_rocket.h"
#include "pm_display.h"
#include "pm_qa.h"
#include "pm_face_tour_info.h"
#include "pm_home_gem_pulse.h"
#include "pm_heap.h"
#include "pm_log.h"
#include "pm_resource.h"
#include "pm_settings.h"
#include "pm_tour_mode.h"
#include "pm_daily_briefing.h"
#include "pm_daily_briefing_nvs.h"
#include "pm_user_nvs.h"
#include "faces/home/pm_face_home_briefing.h"
#include "pm_speaker.h"

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
static constexpr uint8_t k_tv_face = 4;
static constexpr uint8_t k_tv_runes = 5;
static uint8_t g_text_voice_route = k_tv_none;
static bool g_calcifer_briefing = false;
/** Home / first-run: full daily LLM+TTS briefing (schedule + sky). */
static bool g_daily_briefing = false;
static constexpr bool kAutoDailyBriefingEnabled = false;
static bool s_daily_brief_auto_armed = false;
static bool s_face_tour_active = false;
static bool s_face_tour_auto_advance = false;
static int s_face_tour_idx = 0;
static int s_face_tour_loaded_idx = -1;
static uint32_t s_face_tour_last_ms = 0;
static uint32_t s_face_tour_dwell_ms = 2800;
static uint32_t s_face_tour_overlay_started_ms = 0;
static uint32_t s_face_tour_bust_retry_ms = 0;
static char s_face_tour_guide_slug[32] = "";
static bool s_face_tour_narrate = false;
static bool s_face_tour_button_test = false;
static bool s_face_tour_first_run = false;
static uint8_t s_face_tour_voice_phase = 0;
static uint32_t s_face_tour_voice_started_ms = 0;
static uint8_t s_face_tour_tts_retry = 0;
static uint8_t s_face_tour_tts_ok = 0;
static uint8_t s_face_tour_tts_fail = 0;
static uint8_t s_face_tour_tts_skip = 0;
static PmVoiceResult s_face_tour_voice_result;
static constexpr size_t kFaceTourVoiceMsgCap = 2200;
static constexpr size_t kFaceTourSysPromptCap = 3200;
static constexpr size_t kMoonVoiceMsgCap = 2200;
static constexpr size_t kMoonSysPromptCap = 640;
static constexpr size_t kRunesVoiceMsgCap = 900;
static constexpr size_t kRunesSysPromptCap = 900;
static char *s_face_tour_voice_msg = nullptr;
static char *s_face_tour_sys_prompt = nullptr;
static char s_face_voice_face[24] = "";
static char s_face_voice_faculty_slug[64] = "";
static char s_face_voice_faculty_name[96] = "";
static char *g_moon_voice_msg = nullptr;
static char *g_moon_sys_prompt = nullptr;
static char *g_runes_voice_msg = nullptr;
static char *g_runes_sys_prompt = nullptr;
static bool g_moon_voice_pcm = false;

static constexpr const char *kTourPrefsNs = "tour";
static constexpr const char *kTourPlayedKey = "played";
static constexpr const char *kTourModeKey = "mode";
static constexpr const char *kTourSeenKey = "seen";

static uint32_t tour_all_seen_mask(void) {
  const uint32_t n = static_cast<uint32_t>(ClockFace::kNumFaces);
  return n >= 32u ? 0xffffffffu : ((1u << n) - 1u);
}

static uint32_t tour_seen_mask_load(void) {
  return pm_nvs_get_u32(kTourPrefsNs, kTourSeenKey, 0) & tour_all_seen_mask();
}

static void tour_seen_mask_set(uint32_t mask) {
  (void)pm_nvs_set_u32(kTourPrefsNs, kTourSeenKey, mask & tour_all_seen_mask());
}

static bool tour_face_seen(int idx) {
  if (idx < 0 || idx >= static_cast<int>(ClockFace::kNumFaces)) {
    return false;
  }
  return (tour_seen_mask_load() & (1u << static_cast<uint32_t>(idx))) != 0;
}

static void tour_mark_face_seen(int idx) {
  if (idx < 0 || idx >= static_cast<int>(ClockFace::kNumFaces)) {
    return;
  }
  const uint32_t bit = 1u << static_cast<uint32_t>(idx);
  const uint32_t mask = tour_seen_mask_load();
  if ((mask & bit) == 0) {
    const uint32_t next = mask | bit;
    tour_seen_mask_set(next);
    if (next == tour_all_seen_mask()) {
      (void)pm_nvs_set_bool(kTourPrefsNs, kTourPlayedKey, true);
    }
  }
}

static void tour_seen_reset(void) {
  tour_seen_mask_set(0);
  (void)pm_nvs_set_bool(kTourPrefsNs, kTourPlayedKey, false);
}

uint8_t pm_tour_seen_count(void) {
  uint32_t mask = tour_seen_mask_load();
  uint8_t n = 0;
  while (mask) {
    n += static_cast<uint8_t>(mask & 1u);
    mask >>= 1;
  }
  return n;
}

uint8_t pm_tour_total_count(void) {
  return static_cast<uint8_t>(ClockFace::kNumFaces);
}

bool pm_tour_mode_enabled(void) {
  return pm_nvs_get_bool(kTourPrefsNs, kTourModeKey, false);
}

static void tour_mode_set_enabled(bool enabled) {
  (void)pm_nvs_set_bool(kTourPrefsNs, kTourModeKey, enabled);
}

static bool tour_played_load(void) {
  return pm_nvs_get_bool(kTourPrefsNs, kTourPlayedKey, false) ||
         tour_seen_mask_load() == tour_all_seen_mask();
}

static void tour_played_set(bool played) {
  (void)pm_nvs_set_bool(kTourPrefsNs, kTourPlayedKey, played);
  tour_seen_mask_set(played ? tour_all_seen_mask() : 0);
}
/** Tap fortune: stay on Moon face during think/speak. */
static bool g_moon_fortune_active = false;
/** Tap fortune: stay on Runes face during think/speak. */
static bool g_runes_fortune_active = false;
static constexpr size_t kAstrologyVoiceMsgCap = 2200;
static constexpr size_t kAstrologySysPromptCap = 2800;
static constexpr size_t kSynastryVoiceMsgCap = 2600;
static constexpr size_t kSynastrySysPromptCap = 3200;
static char *g_astrology_voice_msg = nullptr;
static char *g_astrology_sys_prompt = nullptr;
static char *g_synastry_voice_msg = nullptr;
static char *g_synastry_sys_prompt = nullptr;
/** Cached last TTS MP3 for BOOT replay (PSRAM). */
static uint8_t *g_last_play_mp3 = nullptr;
static size_t g_last_play_mp3_len = 0;
/** Reset [kPlaying] static arm state on next entry. */
static bool g_voice_play_reset = false;

static char *voice_psram_buffer(size_t cap) {
  char *p = static_cast<char *>(heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (p) {
    p[0] = '\0';
  }
  return p;
}

static bool voice_prompt_buffers_ensure(void) {
  if (!s_face_tour_voice_msg) s_face_tour_voice_msg = voice_psram_buffer(kFaceTourVoiceMsgCap);
  if (!s_face_tour_sys_prompt) s_face_tour_sys_prompt = voice_psram_buffer(kFaceTourSysPromptCap);
  if (!g_moon_voice_msg) g_moon_voice_msg = voice_psram_buffer(kMoonVoiceMsgCap);
  if (!g_moon_sys_prompt) g_moon_sys_prompt = voice_psram_buffer(kMoonSysPromptCap);
  if (!g_runes_voice_msg) g_runes_voice_msg = voice_psram_buffer(kRunesVoiceMsgCap);
  if (!g_runes_sys_prompt) g_runes_sys_prompt = voice_psram_buffer(kRunesSysPromptCap);
  if (!g_astrology_voice_msg) g_astrology_voice_msg = voice_psram_buffer(kAstrologyVoiceMsgCap);
  if (!g_astrology_sys_prompt) g_astrology_sys_prompt = voice_psram_buffer(kAstrologySysPromptCap);
  if (!g_synastry_voice_msg) g_synastry_voice_msg = voice_psram_buffer(kSynastryVoiceMsgCap);
  if (!g_synastry_sys_prompt) g_synastry_sys_prompt = voice_psram_buffer(kSynastrySysPromptCap);
  return s_face_tour_voice_msg && s_face_tour_sys_prompt && g_moon_voice_msg && g_moon_sys_prompt &&
         g_runes_voice_msg && g_runes_sys_prompt && g_astrology_voice_msg && g_astrology_sys_prompt &&
         g_synastry_voice_msg && g_synastry_sys_prompt;
}
static bool s_rec_mic_on = false;
/** Astrology voice: stay on chart during record/think/speak + highlight mentions. */
static bool g_astro_voice_active = false;
/** True when astro turn uses recorded PCM (PWR hold); false for BOOT tap text reading. */
static bool g_astro_voice_pcm = false;
/** Synastry voice: stay on dual chart during record/think/speak. */
static bool g_synastry_voice_active = false;
static bool g_synastry_voice_pcm = false;
/** Alethiometer voice: stay on compass during record/think/speak. */
static bool g_alethiometer_voice_active = false;
static bool g_alethiometer_voice_pcm = false;
/** ClassicAnalog PWR hold → mynah-pocket-journal (no voice-pipeline TTS). */
static bool g_commonplace_journal = false;
/** Notes face PWR hold → flash queue first, then Commonplace when online. */
static bool g_commonplace_note_face = false;
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
static bool instrument_stack_contains(ClockFace face);
/** Last full clock paint background (for second-hand erasure). */
static bool s_spotify_have_data = false;
static uint32_t s_last_spotify_poll_ms = 0;

static uint32_t s_last_calcifer_poll_ms = 0;
static bool s_weather_have_data = false;
static uint32_t s_last_weather_poll_ms = 0;
static bool s_quotes_have_data = false;
static uint32_t s_last_quotes_poll_ms = 0;
static uint32_t s_last_rocket_poll_ms = 0;

#ifndef MYNAH_SPOTIFY_POLL_MS
#define MYNAH_SPOTIFY_POLL_MS 25000u
#endif

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

/** Apocalypso risk radar (12 axes, 5 rings) — matches the web RISK PROFILE widget. */
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

/** Stop voice/PTT and return to clock (swipe away from Moon fortune, astro, etc.). */
static void gesture_end_voice_ui(void) {
  pm_voice_abort();
  pm_commonplace_abort();
  pm_speaker_abort();
  pm_mic_stop();
  pm_voice_result_free(&g_voice_result);
  thinking_progress_end();
  recording_progress_end();
  g_voice_use_message = false;
  g_text_voice_route = k_tv_none;
  g_calcifer_briefing = false;
  g_daily_briefing = false;
  pm_speaker_set_max_play_seconds(180);
  g_moon_fortune_active = false;
  g_astro_voice_active = false;
  g_astro_voice_pcm = false;
  g_synastry_voice_active = false;
  g_synastry_voice_pcm = false;
  g_moon_voice_pcm = false;
  g_commonplace_journal = false;
  g_commonplace_note_face = false;
  s_rec_mic_on = false;
  g_voice_play_reset = true;
  g_state = AppState::kClock;
}

static bool home_begin_daily_briefing(void) {
  if (s_face_tour_active) {
    snprintf(g_gesture_banner, sizeof(g_gesture_banner), "brief: tour active");
    return false;
  }
  if (!pm_wifi_connected()) {
    snprintf(g_gesture_banner, sizeof(g_gesture_banner), "brief: need WiFi");
    return false;
  }
  if (!pm_time_valid()) {
    snprintf(g_gesture_banner, sizeof(g_gesture_banner), "brief: need time");
    return false;
  }
  if (g_state != AppState::kClock) {
    gesture_end_voice_ui();
  }
  pm_voice_result_free(&g_voice_result);
  g_voice_use_message = false;
  g_text_voice_route = k_tv_none;
  g_calcifer_briefing = false;
  g_astro_voice_active = false;
  g_astro_voice_pcm = false;
  g_synastry_voice_active = false;
  g_synastry_voice_pcm = false;
  g_alethiometer_voice_active = false;
  g_alethiometer_voice_pcm = false;
  g_moon_fortune_active = false;
  g_moon_voice_pcm = false;
  g_commonplace_journal = false;
  g_commonplace_note_face = false;
  g_daily_briefing = true;
  g_voice_play_reset = true;
  g_state = AppState::kThinking;
  if (pm_gfx) {
    pm_face_home_briefing_draw_thinking(0.f);
  }
  return true;
}

static bool gesture_cycle_face(int delta) {
  if (g_state != AppState::kClock) {
    gesture_end_voice_ui();
  }
  if (pm_faces_current() == ClockFace::Settings) {
    pm_settings_on_leave();
    pm_faces_set(delta >= 0 ? ClockFace::Synastry : ClockFace::CalciferCountdown);
    g_gesture_banner[0] = '\0';
    g_clock_repaint_pending = true;
    Serial.printf("[gesture] face -> %d\n", static_cast<int>(pm_faces_current()));
    return true;
  }
  if (instrument_stack_contains(pm_faces_current())) {
    pm_faces_set(delta >= 0 ? ClockFace::Rocket : ClockFace::Spectrum);
    g_gesture_banner[0] = '\0';
    g_clock_repaint_pending = true;
    Serial.printf("[gesture] face -> %d\n", static_cast<int>(pm_faces_current()));
    return true;
  }
  pm_faces_cycle(delta);
  g_gesture_banner[0] = '\0';
  g_clock_repaint_pending = true;
  Serial.printf("[gesture] face -> %d\n", static_cast<int>(pm_faces_current()));
  return true;
}

static void global_horizontal_nav_touch_tick() {
  int16_t xs[1];
  int16_t ys[1];
  const uint8_t n = pm_touch_sample(xs, ys, 1);
  static bool down = false;
  static bool emitted = false;
  static int16_t x0 = 0;
  static int16_t y0 = 0;
  static int16_t max_dx = 0;
  static int16_t max_dy = 0;

  if (n == 0) {
    down = false;
    emitted = false;
    max_dx = 0;
    max_dy = 0;
    return;
  }
  if (!down) {
    down = true;
    emitted = false;
    x0 = xs[0];
    y0 = ys[0];
    max_dx = 0;
    max_dy = 0;
    return;
  }
  const int16_t dx = static_cast<int16_t>(xs[0] - x0);
  const int16_t dy = static_cast<int16_t>(ys[0] - y0);
  const int16_t adx = dx < 0 ? static_cast<int16_t>(-dx) : dx;
  const int16_t ady = dy < 0 ? static_cast<int16_t>(-dy) : dy;
  if (adx > max_dx) {
    max_dx = adx;
  }
  if (ady > max_dy) {
    max_dy = ady;
  }
  if (emitted || pm_ptt_button_held()) {
    return;
  }
  if (max_dx >= 28 && max_dx > max_dy + 4) {
    emitted = true;
    pm_gesture_reset();
    (void)gesture_cycle_face(dx < 0 ? 1 : -1);
  }
}

static void handle_usb_audio_stream_event(void) {
  if (!pm_usb_uac_consume_speaker_stream_event()) {
    return;
  }
  if (!pm_audio_route_uac_available()) {
    return;
  }
  if (g_state != AppState::kClock) {
    gesture_end_voice_ui();
  }
  s_face_tour_active = false;
  pm_audio_route_set(PmAudioRoute::Usb);
  if (pm_faces_current() != ClockFace::Spectrum) {
    pm_faces_set(ClockFace::Spectrum);
  }
  snprintf(g_gesture_banner, sizeof(g_gesture_banner), "USB audio");
  g_clock_repaint_pending = true;
  pm_log_printf(false, "uac: stream active -> spectrum face route=USB face=%d heap=%u largest=%u",
                static_cast<int>(pm_faces_current()), static_cast<unsigned>(pm_heap_internal_free()),
                static_cast<unsigned>(pm_heap_internal_largest()));
  Serial.println("uac: stream active -> spectrum face");
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
  if (!voice_prompt_buffers_ensure()) {
    snprintf(g_gesture_banner, sizeof(g_gesture_banner), "astro: PSRAM OOM");
    return false;
  }
  if (!pm_wifi_connected()) {
    snprintf(g_gesture_banner, sizeof(g_gesture_banner), "astro: need WiFi");
    return false;
  }
  if (!pm_time_valid()) {
    snprintf(g_gesture_banner, sizeof(g_gesture_banner), "astro: need time");
    return false;
  }
  if (!pm_face_astrology_build_system_prompt(g_astrology_voice_msg, kAstrologyVoiceMsgCap,
                                             g_astrology_sys_prompt, kAstrologySysPromptCap)) {
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
  g_runes_fortune_active = false;
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
  if (!voice_prompt_buffers_ensure()) {
    snprintf(g_gesture_banner, sizeof(g_gesture_banner), "synastry: PSRAM OOM");
    return false;
  }
  if (!pm_wifi_connected()) {
    snprintf(g_gesture_banner, sizeof(g_gesture_banner), "synastry: need WiFi");
    return false;
  }
  if (!pm_face_synastry_build_system_prompt(g_synastry_voice_msg, kSynastryVoiceMsgCap,
                                            g_synastry_sys_prompt, kSynastrySysPromptCap)) {
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
  g_runes_fortune_active = false;
  g_moon_voice_pcm = false;
  g_calcifer_briefing = false;
  s_synastry_play_armed = false;
  g_state = AppState::kThinking;
  return true;
}

static bool moon_begin_fortune() {
  if (!voice_prompt_buffers_ensure()) {
    snprintf(g_gesture_banner, sizeof(g_gesture_banner), "moon: PSRAM OOM");
    return false;
  }
  if (!pm_wifi_connected()) {
    snprintf(g_gesture_banner, sizeof(g_gesture_banner), "moon: need WiFi");
    return false;
  }
  if (!pm_time_valid()) {
    snprintf(g_gesture_banner, sizeof(g_gesture_banner), "moon: need time");
    return false;
  }
  if (!pm_face_moon_build_fortune_message(g_moon_voice_msg, kMoonVoiceMsgCap)) {
    snprintf(g_gesture_banner, sizeof(g_gesture_banner), "moon: build fail");
    return false;
  }
  if (!pm_face_moon_build_fortune_system_prompt(g_moon_sys_prompt, kMoonSysPromptCap)) {
    snprintf(g_gesture_banner, sizeof(g_gesture_banner), "moon: build fail");
    return false;
  }
  pm_voice_result_free(&g_voice_result);
  g_voice_use_message = true;
  g_text_voice_route = k_tv_moon;
  g_moon_fortune_active = true;
  g_runes_fortune_active = false;
  g_astro_voice_active = false;
  g_astro_voice_pcm = false;
  g_synastry_voice_active = false;
  g_synastry_voice_pcm = false;
  g_moon_voice_pcm = false;
  g_calcifer_briefing = false;
  g_state = AppState::kThinking;
  return true;
}

static bool runes_begin_fortune() {
  if (!voice_prompt_buffers_ensure()) {
    snprintf(g_gesture_banner, sizeof(g_gesture_banner), "runes: PSRAM OOM");
    return false;
  }
  if (!pm_wifi_connected()) {
    snprintf(g_gesture_banner, sizeof(g_gesture_banner), "runes: need WiFi");
    return false;
  }
  struct tm local = {};
  const bool valid = pm_time_valid();
  if (valid) {
    pm_time_local(&local);
  }
  pm_face_runes_cast(valid ? &local : nullptr, valid);
  if (!pm_face_runes_build_fortune_message(g_runes_voice_msg, kRunesVoiceMsgCap)) {
    snprintf(g_gesture_banner, sizeof(g_gesture_banner), "runes: build fail");
    return false;
  }
  if (!pm_face_runes_build_system_prompt(g_runes_sys_prompt, kRunesSysPromptCap)) {
    snprintf(g_gesture_banner, sizeof(g_gesture_banner), "runes: build fail");
    return false;
  }
  pm_voice_result_free(&g_voice_result);
  g_voice_use_message = true;
  g_text_voice_route = k_tv_runes;
  g_runes_fortune_active = true;
  g_moon_fortune_active = false;
  g_astro_voice_active = false;
  g_astro_voice_pcm = false;
  g_synastry_voice_active = false;
  g_synastry_voice_pcm = false;
  g_alethiometer_voice_active = false;
  g_alethiometer_voice_pcm = false;
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
  } k[] = {{"classic", 0},     {"hue", 0},          {"analog", 0},       {"apocalypso", 1},
           {"digital", 2},     {"spotify", 3},      {"astro", 4},        {"astrology", 4},
           {"moon", 5},        {"calcifer", 6},     {"schedule", 6},     {"castalia", 7},
           {"settings", 8},    {"wifi", 8},         {"synastry", 9},     {"syn", 9},
           {"spectrum", 10},   {"fft", 10},         {"audio", 10},       {"sound", 10},
           {"chakra", 11},     {"bowl", 12},        {"tibetan", 12},     {"tibetan_bowl", 12},
           {"rocket", 13},     {"launch", 13},      {"launchclock", 13}, {"radar", 14},
           {"presence", 14},   {"peers", 14},       {"locator", 14},     {"locations", 14},
           {"faculty", 15},    {"fac", 15},         {"weather", 16},    {"quotes", 17},
           {"quote", 17},      {"qotd", 17},        {"transits", 18},   {"live_transits", 18},
           {"live-transits", 18}, {"live", 18},      {"tarot", 19},      {"cards", 19},
           {"card", 19},       {"arcana", 19},       {"notes", 20},      {"note", 20},
           {"commonplace", 20}, {"notebook", 20},    {"ocarina", 21},    {"ocarina_face", 21},
           {"flute", 21},      {"bongo", 22},
           {"drum", 22},       {"drums", 22},        {"conga", 22},      {"piano", 23},
           {"keys", 23},       {"keyboard", 23},    {"level", 24},      {"bubble", 24},
           {"bubble_level", 24}, {"imu", 24},        {"tuning", 25},     {"tuner", 25},
           {"staff", 25},      {"pitch", 25},       {"pandrum", 26},    {"pan_drum", 26},
           {"pan-drum", 26},    {"pandrom", 26},     {"pandrom_face", 26}, {"handpan", 26},
           {"hang", 26},        {"alethiometer", 27}, {"aleth", 27},     {"compass", 27},
           {"golden_compass", 27}, {"runes", 28},     {"rune", 28},      {"futhark", 28},
           {"fortune", 28},        {"cycle", 29},     {"menstrual", 29}, {"period", 29}};
  for (const auto &e : k) {
    if (strcasecmp(name, e.n) == 0) {
      *out = e.idx;
      return true;
    }
  }
  return false;
}

struct FaceTourInfo {
  const char *name;
  const char *summary;
  const char *tts_focus;
  const char *ok;
  const char *warn;
  bool needs_wifi;
  bool needs_time;
};

static const FaceTourInfo k_face_tour[] = {
    {"classic", "the home base: time, mood, and a living gem that makes the watch feel awake",
     "a short daily orientation from the home clock",
     "drawing locally", "heap is low", false, false},
    {"apocalypso", "a civilizational-risk observatory for the thirteen apocalypses, with live APOC tickers and a risk-profile radar",
     "the Observatory's civilizational risk profile across nuclear, bio, AI, cyber, infrastructure, market, state, epistemic, climate, biosphere, solar, impact, and biblical risk",
     "risk radar is drawing", "risk ticker refresh needs WiFi", true, false},
    {"digital", "a clean, glanceable clock for when the device should simply be a watch",
     "a concise spoken local-time check-in", "drawing local time",
     "time is not synced", false, true},
    {"spotify", "a music companion face for playback, context, and listening rituals",
     "a musical listening prompt for the current moment",
     "WiFi is available for refresh", "offline, transport is display-only", true, false},
    {"astro", "a live sky wheel that turns current transits into a spoken personal weather report",
     "the current astrology transits and sky wheel",
     "time and WiFi are ready", "needs WiFi and time for live reading", true, true},
    {"moon", "a lunar ritual face for phase, mood, and a small daily fortune",
     "today's lunar phase and fortune",
     "time and WiFi are ready", "needs WiFi and time for fortune voice", true, true},
    {"calcifer", "a calendar creature that turns meetings and time blocks into a living agenda wheel",
     "the next calendar moment and schedule rhythm",
     "calendar refresh can run", "needs WiFi and time for calendar", true, true},
    {"castalia", "the sign-in gateway for cloud memory, voice services, and Castalia context",
     "Castalia sign-in status and what pairing unlocks",
     "WiFi is available for pairing", "offline, pairing QR only", true, false},
    {"settings", "the control room for network, identity, time, and device health",
     "a settings health check for WiFi, auth, heap, and time",
     "settings UI is drawing", "settings UI is drawing", false, false},
    {"synastry", "a relationship lens that compares charts and surfaces patterns with care",
     "the active synastry relationship highlight",
     "time and WiFi are ready", "needs WiFi and time for voice", true, true},
    {"spectrum", "a pocket sound lab for seeing the room, voice, and music as live frequency",
     "a sound-check prompt for the audio spectrum face",
     "local audio analyzer is drawing", "audio analyzer is local only", false, false},
    {"chakra", "a tone-and-symbol meditation face for embodied attention",
     "the current chakra tone and embodied attention",
     "local tone controls are available", "local tone controls are available", false, false},
    {"bowl", "a touchable singing bowl for slowing down with sound and motion",
     "a short singing-bowl meditation prompt",
     "local rim instrument is available", "local rim instrument is available", false, false},
    {"rocket", "a launch-window clock for following spaceflight as a live ritual",
     "the next launch window and mission context",
     "launch refresh can run", "needs WiFi and time for launches", true, true},
    {"radar", "a nearby-presence radar for people, devices, and ambient proximity",
     "nearby BLE peers and spatial presence",
     "BLE radar can start", "heap is tight after BLE", false, false},
    {"faculty", "a conversation face where named Castalia faculty become recurring guides",
     "the active faculty persona and recent conversation",
     "WiFi is available for portraits", "offline, cached portraits only", true, false},
    {"weather", "a radial forecast that makes the next day of weather glanceable",
     "the local 24-hour weather ring",
     "weather refresh can run", "needs WiFi and time for forecast", true, true},
    {"quotes", "a daily passage face for carrying a faculty quote into the day",
     "the quote of the day and its faculty context",
     "quote refresh can run", "offline demo quote only", true, false},
    {"transits", "a live celestial dashboard for planets, motion, and the next Moon change",
     "live transits and the next Moon ingress",
     "time and ephemeris are ready", "needs time for live transits", false, true},
    {"tarot", "a daily Major Arcana reflection and deck browser",
     "the active Major Arcana card",
     "drawing local Major Arcana", "drawing local Major Arcana", false, false},
    {"notes", "a quick-capture face for voice notes that sync to Commonplace later",
     "the offline note capture queue",
     "flash note queue is available", "flash note queue is available", false, false},
    {"ocarina", "a tiny playable wind instrument mapped onto the screen",
     "the active ocarina key and breath note",
     "local ocarina tones are available", "local ocarina tones are available", false, false},
    {"bongo", "a touch drum whose center, rim, and motion change the feel of each hit",
     "the last bongo tap pitch and drum feel",
     "local bongo hits are available", "local bongo hits are available", false, false},
    {"piano", "a circular one-octave piano for quick melodies and tonal tests",
     "the active piano key and note",
     "local piano tones are available", "local piano tones are available", false, false},
    {"level", "a motion-aware level that turns the watch into a tiny physical instrument",
     "the current level nudge", "IMU level is drawing", "IMU unavailable", false, false},
    {"tuning", "a pitch detector for tuning instruments and showing notes as they happen",
     "the currently detected pitch and cents",
     "local pitch detector is listening", "local pitch detector is listening", false, false},
    {"pandrum", "a 14-note handpan-style surface for resonant touch performance",
     "the active pan drum note and resonance",
     "local pan drum tones are available", "local pan drum tones are available", false, false},
    {"alethiometer", "a symbolic compass for asking questions and turning symbols into interpretation",
     "the active alethiometer symbols and narrative interpretation",
     "WiFi is available for LLM interpretation", "offline, compass animation only", true, false},
    {"runes", "a three-rune spread for turning a question into a past, present, future reading",
     "the selected rune spread and spoken fortune",
     "WiFi is available for TTS fortune", "offline, visual spread only", true, false},
    {"cycle", "a private menstrual cycle ring for day-one logging, fertile window, and lunar context",
     "the current cycle day and next embodied check-in",
     "local cycle data is available", "set day one with a tap", false, true},
};

static const FaceTourInfo *face_tour_info(int idx) {
  if (idx < 0 || idx >= static_cast<int>(sizeof(k_face_tour) / sizeof(k_face_tour[0]))) {
    return nullptr;
  }
  return &k_face_tour[idx];
}

static bool face_tour_face_healthy(const FaceTourInfo *info) {
  if (!info) {
    return false;
  }
  if (pm_heap_internal_largest() < 70000u) {
    return false;
  }
  if (info->needs_wifi && !pm_wifi_connected()) {
    return false;
  }
  if (info->needs_time && !pm_time_valid()) {
    return false;
  }
  return true;
}

static const char *face_tour_health_text(const FaceTourInfo *info) {
  if (!info) {
    return "missing face metadata";
  }
  if (pm_heap_internal_largest() < 70000u) {
    return "heap is low";
  }
  if (info->needs_wifi && !pm_wifi_connected()) {
    return info->warn;
  }
  if (info->needs_time && !pm_time_valid()) {
    return info->warn;
  }
  return info->ok;
}

static const char *face_tour_demo_text(const FaceTourInfo *info) {
  if (!info) {
    return "demo metadata is unavailable";
  }
  if (info->needs_wifi && !pm_wifi_connected()) {
    return info->warn;
  }
  if (info->needs_time && !pm_time_valid()) {
    return info->warn;
  }
  return info->ok;
}

static const ClockFace k_instrument_stack[] = {
    ClockFace::Chakra, ClockFace::Ocarina, ClockFace::Bongo,
    ClockFace::Piano,  ClockFace::PanDrum, ClockFace::Tuning,
};

static const char *instrument_stack_label(ClockFace face) {
  switch (face) {
    case ClockFace::Chakra:
      return "chakra";
    case ClockFace::TibetanBowl:
      return "bowl";
    case ClockFace::Ocarina:
      return "ocarina";
    case ClockFace::Bongo:
      return "bongo";
    case ClockFace::Piano:
      return "piano";
    case ClockFace::PanDrum:
      return "pandrum";
    case ClockFace::Tuning:
      return "tuning";
    default:
      return "instrument";
  }
}

static bool instrument_stack_contains(ClockFace face) {
  constexpr int n = static_cast<int>(sizeof(k_instrument_stack) / sizeof(k_instrument_stack[0]));
  for (int i = 0; i < n; ++i) {
    if (k_instrument_stack[i] == face) {
      return true;
    }
  }
  return false;
}

static bool instrument_stack_horizontal_exit(PmGestureKind kind) {
  if (kind != PmGestureKind::SwipeLeft && kind != PmGestureKind::SwipeRight) {
    return false;
  }
  if (!instrument_stack_contains(pm_faces_current())) {
    return false;
  }
  const ClockFace next = kind == PmGestureKind::SwipeLeft ? ClockFace::Rocket : ClockFace::Spectrum;
  pm_faces_set(next);
  g_gesture_banner[0] = '\0';
  g_clock_repaint_pending = true;
  Serial.printf("[gesture] face -> %d\n", static_cast<int>(pm_faces_current()));
  return true;
}

static bool instrument_stack_swipe(PmGestureKind kind) {
  if (kind != PmGestureKind::SwipeUp && kind != PmGestureKind::SwipeDown) {
    return false;
  }
  const ClockFace cur = pm_faces_current();
  constexpr int n = static_cast<int>(sizeof(k_instrument_stack) / sizeof(k_instrument_stack[0]));
  int idx = -1;
  for (int i = 0; i < n; ++i) {
    if (k_instrument_stack[i] == cur) {
      idx = i;
      break;
    }
  }
  if (idx < 0) {
    return false;
  }
  const int delta = kind == PmGestureKind::SwipeUp ? 1 : -1;
  idx = (idx + delta + n) % n;
  const ClockFace next = k_instrument_stack[idx];
  pm_faces_set(next);
  snprintf(g_gesture_banner, sizeof(g_gesture_banner), "%s %d/%d", instrument_stack_label(next), idx + 1, n);
  return true;
}

static void face_tour_format_clock(char *out, size_t cap) {
  if (!out || cap == 0) {
    return;
  }
  if (!pm_time_valid()) {
    snprintf(out, cap, "time is not synced");
    return;
  }
  struct tm tm = {};
  pm_time_local(&tm);
  strftime(out, cap, "%A %H:%M local time", &tm);
}

static bool face_voice_build_prompt(const FaceTourInfo *info, int idx, char *msg, size_t msg_cap,
                                    char *sys, size_t sys_cap, bool tour_test) {
  if (!info || !msg || msg_cap == 0 || !sys || sys_cap == 0) {
    return false;
  }
  msg[0] = '\0';
  sys[0] = '\0';
  s_face_voice_face[0] = '\0';
  s_face_voice_faculty_slug[0] = '\0';
  s_face_voice_faculty_name[0] = '\0';
  const ClockFace face = static_cast<ClockFace>(idx);
  const char *health = face_tour_health_text(info);
  const char *demo = face_tour_demo_text(info);
  char when[40];
  face_tour_format_clock(when, sizeof(when));

  if (face == ClockFace::Astrology) {
    if (tour_test) {
      snprintf(sys, sys_cap,
               "You are giving a first-time product tour of the Mynah Astrolabe astrology face. Explain the "
               "product purpose, not a live reading. Keep it under 22 seconds. Do not claim fixed fate.");
      snprintf(msg, msg_cap,
               "Tour face %d of %d: astro. Product purpose: %s. Explain that this face turns the current sky "
               "into a glanceable wheel and a spoken context report for the wearer.",
               idx + 1, static_cast<int>(ClockFace::kNumFaces), info->summary);
      return true;
    }
    if (!pm_time_valid()) {
      snprintf(g_gesture_banner, sizeof(g_gesture_banner), "astro: need time");
      return false;
    }
    if (!pm_face_astrology_build_system_prompt(g_astrology_voice_msg, kAstrologyVoiceMsgCap, sys,
                                               sys_cap)) {
      snprintf(g_gesture_banner, sizeof(g_gesture_banner), "astro: build fail");
      return false;
    }
    snprintf(msg, msg_cap, "%sGive a concise live transit reading for %s.",
             tour_test ? "Tour-test the astrology TTS button. " : "", when);
    return true;
  }
  if (face == ClockFace::Moon) {
    if (tour_test) {
      snprintf(sys, sys_cap,
               "You are giving a first-time product tour of the Mynah Astrolabe moon face. Explain the product "
               "purpose, not a live fortune. Keep it under 22 seconds. Do not claim fixed fate.");
      snprintf(msg, msg_cap,
               "Tour face %d of %d: moon. Product purpose: %s. Explain that this face gives the wearer a "
               "lunar phase, a mood cue, and a small daily ritual.",
               idx + 1, static_cast<int>(ClockFace::kNumFaces), info->summary);
      return true;
    }
    if (!pm_time_valid() || !pm_face_moon_build_fortune_message(msg, msg_cap) ||
        !pm_face_moon_build_fortune_system_prompt(sys, sys_cap)) {
      snprintf(g_gesture_banner, sizeof(g_gesture_banner), "moon: build fail");
      return false;
    }
    return true;
  }
  if (face == ClockFace::Synastry) {
    if (tour_test) {
      snprintf(sys, sys_cap,
               "You are giving a first-time product tour of the Mynah Astrolabe synastry face. Explain the "
               "product purpose, not a live relationship reading. Keep it under 22 seconds. Do not claim fixed fate.");
      snprintf(msg, msg_cap,
               "Tour face %d of %d: synastry. Product purpose: %s. Explain that this face compares two charts "
               "to surface relationship patterns carefully, without treating them as destiny.",
               idx + 1, static_cast<int>(ClockFace::kNumFaces), info->summary);
      return true;
    }
    if (!pm_face_synastry_build_system_prompt(g_synastry_voice_msg, kSynastryVoiceMsgCap, sys,
                                              sys_cap)) {
      snprintf(sys, sys_cap,
               "You narrate the Mynah Astrolabe Synastry face like a gentle fortune teller reading from a "
               "brass astrolabe. The full synastry chart snapshot is not available right now, so speak about "
               "what the face is for: comparing the user's birth chart with a selected partner or family "
               "profile, highlighting relational patterns with care and agency. Offer one small omen, one "
               "counsel, and one image. Keep it concise and avoid deterministic claims.");
    }
    snprintf(msg, msg_cap, "%sGive one concise relationship highlight.",
             tour_test ? "Tour-test the synastry TTS button. " : "");
    return true;
  }

  snprintf(sys, sys_cap,
           "You are giving a first-time product tour of the Mynah Astrolabe, a tiny wrist computer made of "
           "clock faces, rituals, instruments, sensors, and cloud memory. For each face, explain what the face "
           "is meant to do for a person, not just what it currently shows. Be concrete enough that someone "
           "watching the demo understands the product. Speak warmly, with a little brass-astrolabe mystery, "
           "but stay grounded. Keep it under 22 seconds. Do not claim fixed fate. Do not read stage directions, "
           "Markdown, heap, memory, internal diagnostics, or QA status aloud.");

  switch (face) {
    case ClockFace::ClassicAnalog:
      snprintf(msg, msg_cap,
               "Tour face %d of %d: Classic. Product purpose: %s. Current state: %s; %s. Introduce Astrolabe "
               "as more than a clock: a small, glanceable companion whose faces turn time, context, and mood "
               "into rituals.",
               idx + 1, static_cast<int>(ClockFace::kNumFaces), info->summary, when, demo);
      break;
    case ClockFace::Apocalypso:
      snprintf(msg, msg_cap,
               "Tour face %d of %d: Apocalypso. Product purpose: %s. Current state: %s; %s. Explain that "
               "Apocalypso is the Inquiry Institute Observatory of Civilizational Risk: a live ledger, not "
               "prophecy, tracking APOC risk tickers across thirteen apocalypses and turning them into a "
               "glanceable radar on the watch.",
               idx + 1, static_cast<int>(ClockFace::kNumFaces), info->summary, when, demo);
      break;
    case ClockFace::DigitalLocal:
      snprintf(msg, msg_cap,
               "Tour face %d of %d: Digital. Product purpose: %s. Current state: %s; %s. Explain why a "
               "simple utility face matters inside a stranger, richer watch.",
               idx + 1, static_cast<int>(ClockFace::kNumFaces), info->summary, when, demo);
      break;
    case ClockFace::Spotify:
      snprintf(msg, msg_cap,
               "Face: Spotify. Current state: %s. Give a listening prompt for the current moment; if playback "
               "metadata is unavailable, say so gracefully.",
               demo);
      break;
    case ClockFace::CalciferCountdown:
      if (!s_face_tour_active && !g_calcifer_ui.ok && pm_wifi_connected() && pm_time_valid() &&
          ESP.getFreeHeap() >= MYNAH_FACE_FETCH_MIN_HEAP) {
        (void)pm_calcifer_fetch(&g_calcifer_ui, time(nullptr));
        s_calcifer_have_data = true;
      }
      if (g_calcifer_ui.current.valid) {
        snprintf(msg, msg_cap,
                 "Face: Calcifer agenda daywheel. Now: %s. Current event: %s. Give a concise spoken schedule "
                 "brief.",
                 when, g_calcifer_ui.current.summary);
      } else if (g_calcifer_ui.next.valid) {
        snprintf(msg, msg_cap,
                 "Face: Calcifer agenda daywheel. Now: %s. Next event: %s. Give a concise spoken schedule "
                 "brief.",
                 when, g_calcifer_ui.next.summary);
      } else {
        snprintf(msg, msg_cap,
                 "Face: Calcifer agenda daywheel. Now: %s. Calendar state: %s. Explain that this face turns "
                 "meetings and open time into a living agenda wheel.",
                 when, g_calcifer_ui.error[0] ? g_calcifer_ui.error : demo);
      }
      break;
    case ClockFace::Castalia:
      snprintf(msg, msg_cap,
               "Face: Castalia pairing. Status: WiFi %s, time %s. Explain what signing in unlocks on the watch: "
               "cloud memory, voice services, Commonplace notes, and personal context.",
               pm_wifi_connected() ? "connected" : "offline", pm_time_valid() ? "synced" : "unsynced");
      break;
    case ClockFace::Settings:
      snprintf(msg, msg_cap,
               "Face: settings. WiFi %s, time %s, local clock %s. Explain this as the control room for network, "
               "identity, time, and device health.",
               pm_wifi_connected() ? "connected" : "offline", pm_time_valid() ? "synced" : "unsynced", when);
      break;
    case ClockFace::Spectrum:
      snprintf(msg, msg_cap,
               "Face: audio spectrum. Current state: local microphone visualizer. Give a short sound-check "
               "prompt for using the spectrum face.");
      break;
    case ClockFace::Chakra:
      snprintf(msg, msg_cap,
               "Face: chakra tone. Current state: local solfeggio tone controls. Give a short embodied "
               "attention prompt for this face.");
      break;
    case ClockFace::TibetanBowl:
      snprintf(msg, msg_cap,
               "Face: Tibetan bowl. Current state: rim instrument ready. Speak a short bowl meditation cue.");
      break;
    case ClockFace::Rocket: {
      if (!s_face_tour_active && (!g_rocket_ui.ok || g_rocket_ui.count <= 0) &&
          pm_wifi_connected() && pm_time_valid()) {
        pm_faculty_release_bust_cache();
        (void)pm_speaker_release_idle_task();
        (void)pm_voice_release_idle_task();
        (void)pm_rocket_request_fetch();
      }
      const PmRocketLaunch *launch = pm_rocket_next(&g_rocket_ui);
      if (launch) {
        snprintf(msg, msg_cap,
                 "Face: rocket launch clock. Next launch: %s by %s from %s. Status: %s. Give a concise "
                 "mission-context briefing.",
                 launch->name, launch->provider, launch->location, launch->status_abbrev);
      } else {
        snprintf(msg, msg_cap,
                 "Face: rocket launch clock. Launch data state: %s. Explain that this face should make spaceflight "
                 "feel like a live ritual with countdowns, agencies, pads, and mission context.",
                 g_rocket_ui.error[0] ? g_rocket_ui.error : demo);
      }
      break;
    }
    case ClockFace::Radar:
      snprintf(msg, msg_cap,
               "Face: BLE radar. Nearby peer count: %u. Explain this as a presence face for noticing nearby "
               "devices, people, and ambient proximity without opening an app.",
               static_cast<unsigned>(pm_presence_peer_count()));
      break;
    case ClockFace::Faculty: {
      PmFacultyProfile faculty = {};
      if (pm_faculty_active(&faculty)) {
        snprintf(s_face_voice_face, sizeof(s_face_voice_face), "faculty");
        snprintf(s_face_voice_faculty_slug, sizeof(s_face_voice_faculty_slug), "%s", faculty.slug);
        snprintf(s_face_voice_faculty_name, sizeof(s_face_voice_faculty_name), "%s", faculty.name);
        snprintf(msg, msg_cap,
                 "Face: faculty. Active faculty: %s, slug %s. Last user line: %.120s. Last reply: %.160s. "
                 "Give a concise faculty-context prompt.",
                 faculty.name, faculty.slug, faculty.last_user, faculty.last_reply);
      } else {
        snprintf(msg, msg_cap,
                 "Face: faculty. No active faculty saved. Explain briefly how the faculty face will speak "
                 "with named Castalia faculty.");
      }
      break;
    }
    case ClockFace::Weather:
      if (!s_face_tour_active && !g_weather_ui.ok && pm_wifi_connected() &&
          ESP.getFreeHeap() >= MYNAH_FACE_FETCH_MIN_HEAP) {
        (void)pm_weather_fetch(&g_weather_ui);
      }
      if (g_weather_ui.ok) {
        snprintf(msg, msg_cap,
                 "Face: weather. Location: %s. Current: %d Celsius, %s. High %d, low %d. Give a concise "
                 "24-hour weather ring briefing.",
                 g_weather_ui.location, static_cast<int>(g_weather_ui.current_temp_c), g_weather_ui.condition,
                 static_cast<int>(g_weather_ui.hi_c), static_cast<int>(g_weather_ui.lo_c));
      } else {
        snprintf(msg, msg_cap,
                 "Tour face %d of %d: weather. Product purpose: %s. Live forecast is not loaded for this demo. "
                 "Explain that the face should make the next day of weather glanceable as a radial ring.",
                 idx + 1, static_cast<int>(ClockFace::kNumFaces), info->summary);
      }
      break;
    case ClockFace::Quotes:
      if (!s_face_tour_active && !g_quotes_ui.ok && pm_wifi_connected() &&
          ESP.getFreeHeap() >= MYNAH_FACE_FETCH_MIN_HEAP) {
        (void)pm_quotes_fetch(&g_quotes_ui);
        s_quotes_have_data = true;
      }
      if (g_quotes_ui.ok) {
        snprintf(s_face_voice_face, sizeof(s_face_voice_face), "quotes");
        snprintf(s_face_voice_faculty_slug, sizeof(s_face_voice_faculty_slug), "%s", g_quotes_ui.faculty_slug);
        snprintf(s_face_voice_faculty_name, sizeof(s_face_voice_faculty_name), "%s", g_quotes_ui.faculty_name);
        snprintf(msg, msg_cap,
                 "Face: quote of the day. Faculty: %s. Source: %s. Quote: %.220s. Give a concise reflection "
                 "on this quote.",
                 g_quotes_ui.faculty_name, g_quotes_ui.book_title[0] ? g_quotes_ui.book_title : g_quotes_ui.passage,
                 g_quotes_ui.quote);
      } else {
        snprintf(msg, msg_cap, "Face: quote of the day. Quote state: %s. Explain what should appear here.",
                 g_quotes_ui.error[0] ? g_quotes_ui.error : health);
      }
      break;
    case ClockFace::LiveTransits:
      snprintf(msg, msg_cap,
               "Face: live transits. Current state: %s; %s. Give a concise sky-status readout focused on "
               "live planets and the next Moon ingress.",
               when, demo);
      break;
    case ClockFace::Tarot: {
      struct tm local = {};
      const bool valid = pm_time_valid();
      if (valid) {
        pm_time_local(&local);
      }
      const int tarot_idx = pm_face_tarot_index(&local, valid);
      snprintf(msg, msg_cap,
               "Face: tarot. Active Major Arcana card: %02d %s. Asset manifest: %s. Give a concise tarot "
               "reading for the watch face: one omen, one counsel, and one image. Make it reflective, not "
               "deterministic.",
               tarot_idx, pm_face_tarot_title(tarot_idx), pm_face_tarot_manifest_url());
      break;
    }
    case ClockFace::Notes:
      snprintf(msg, msg_cap,
               "Face: notes. Offline queued notes: %u. Status: WiFi %s, Castalia session %s. Explain that "
               "PWR hold records a note for Commonplace and queues it to flash when offline.",
               static_cast<unsigned>(pm_commonplace_offline_note_count()),
               pm_wifi_connected() ? "connected" : "offline",
               pm_castalia_has_session() ? "signed in" : "not signed in");
      break;
    case ClockFace::Ocarina:
      snprintf(msg, msg_cap,
               "Face: ocarina. Active key: %s. Current state: local touch instrument. Give a short breath "
               "and listening cue for playing the ocarina face.",
               pm_face_ocarina_key_label());
      break;
    case ClockFace::Bongo:
      snprintf(msg, msg_cap,
               "Face: bongo. Last hit pitch: %.0f hertz, radius %.0f percent from center, force %.0f percent. "
               "Current state: local touch drum where center taps are low, rim taps are high, and IMU impact "
               "boosts loudness. Give a short rhythmic cue.",
               static_cast<double>(pm_face_bongo_last_hz()),
               static_cast<double>(pm_face_bongo_last_radius_norm() * 100.f),
               static_cast<double>(pm_face_bongo_last_force() * 100.f));
      break;
    case ClockFace::Piano:
      snprintf(msg, msg_cap,
               "Face: piano. Active note: %s. Current state: local one-octave circular piano with white "
               "keys on the outer ring and black keys inside. Give a short melodic cue.",
               pm_face_piano_note_label()[0] ? pm_face_piano_note_label() : "none");
      break;
    case ClockFace::PanDrum:
      snprintf(msg, msg_cap,
               "Face: pandrum. Active note: %s. Last pitch: %.0f hertz, force %.0f percent. Current state: "
               "local 14-note handpan-style touch instrument with a center ding, surrounding tone fields, "
               "and IMU impact-sensitive taps. Give a short resonant playing cue.",
               pm_face_pandrum_note_label()[0] ? pm_face_pandrum_note_label() : "none",
               static_cast<double>(pm_face_pandrum_last_hz()),
               static_cast<double>(pm_face_pandrum_last_force() * 100.f));
      break;
    case ClockFace::Level: {
      const char *guidance = pm_face_level_guidance();
      if (tour_test && (!guidance || strcmp(guidance, "IMU unavailable") == 0)) {
        snprintf(sys, sys_cap,
                 "You are giving a first-time product tour of the Mynah Astrolabe level face. Keep it under "
                 "18 seconds. Do not mention unavailable sensors, heap, QA, or diagnostics.");
        snprintf(msg, msg_cap,
                 "Tour face %d of %d: level. Product purpose: %s. Explain that this face turns motion sensing "
                 "into a tiny physical instrument for leveling, balance, and orientation.",
                 idx + 1, static_cast<int>(ClockFace::kNumFaces), info->summary);
      } else {
        snprintf(sys, sys_cap,
                 "You are the Mynah Astrolabe level face TTS button. Say exactly the supplied leveling nudge, "
                 "with no preamble and no extra words. The top of the display is forward.");
        snprintf(msg, msg_cap, "Leveling nudge to speak exactly: %s.", guidance);
      }
      break;
    }
    case ClockFace::Alethiometer:
      if (tour_test) {
        snprintf(sys, sys_cap,
                 "You are giving a first-time product tour of the Mynah Astrolabe alethiometer face. Explain "
                 "the product purpose, not a live divination. Keep it under 22 seconds. Do not claim fixed fate.");
        snprintf(msg, msg_cap,
                 "Tour face %d of %d: alethiometer. Product purpose: %s. Explain that the face is a symbolic "
                 "question compass: the wearer asks something, symbols move, and voice turns the pattern into "
                 "a reflective interpretation.",
                 idx + 1, static_cast<int>(ClockFace::kNumFaces), info->summary);
      } else {
        if (!pm_face_alethiometer_build_system_prompt(sys, sys_cap)) {
          snprintf(g_gesture_banner, sizeof(g_gesture_banner), "aleth: build fail");
          return false;
        }
        snprintf(msg, msg_cap,
                 "%sFace: alethiometer. Current compass state: short needles on %s, %s, and %s; long answer "
                 "needle on %s. Give a concise experimental interpretation of this symbol layout.",
                 tour_test ? "Tour-test the alethiometer TTS button. " : "",
                 pm_face_alethiometer_needle_symbol_name(0), pm_face_alethiometer_needle_symbol_name(1),
                 pm_face_alethiometer_needle_symbol_name(2), pm_face_alethiometer_needle_symbol_name(3));
      }
      break;
    case ClockFace::Runes:
      if (tour_test) {
        snprintf(sys, sys_cap,
                 "You are giving a first-time product tour of the Mynah Astrolabe runes face. Explain the "
                 "product purpose, not a live fortune. Keep it under 22 seconds. Do not claim fixed fate.");
        snprintf(msg, msg_cap,
                 "Tour face %d of %d: runes. Product purpose: %s. Explain that the face turns a question into "
                 "a compact past, present, future spread for reflection.",
                 idx + 1, static_cast<int>(ClockFace::kNumFaces), info->summary);
      } else {
        if (!pm_face_runes_build_system_prompt(sys, sys_cap)) {
          snprintf(g_gesture_banner, sizeof(g_gesture_banner), "runes: build fail");
          return false;
        }
        snprintf(msg, msg_cap,
                 "%sFace: runes. Active spread: past %s (%s), present %s (%s), future %s (%s). "
                 "Give a concise spoken fortune for this past-present-future layout.",
                 tour_test ? "Tour-test the runes TTS button. " : "",
                 pm_face_runes_name(0), pm_face_runes_keyword(0), pm_face_runes_name(1),
                 pm_face_runes_keyword(1), pm_face_runes_name(2), pm_face_runes_keyword(2));
      }
      break;
    default:
      snprintf(msg, msg_cap,
               "Tour face %d of %d: %s. Product purpose: %s. Demo state: %s. Explain what this face should "
               "do for the wearer, even if the current build is still catching up.",
               idx + 1, static_cast<int>(ClockFace::kNumFaces), info->name, info->summary, demo);
      break;
  }
  return msg[0] != '\0';
}

static void face_tour_voice_reset(void) {
  if (s_face_tour_voice_phase == 1) {
    pm_voice_abort();
  }
  pm_voice_result_free(&s_face_tour_voice_result);
  s_face_tour_voice_phase = 0;
  s_face_tour_voice_started_ms = 0;
}

static void face_tour_voice_start(const FaceTourInfo *info, int idx) {
  if ((!s_face_tour_narrate && !s_face_tour_button_test) || !info) {
    return;
  }
  if (!voice_prompt_buffers_ensure()) {
    Serial.printf("tour: tts skipped %d %s reason=psram oom\n", idx, info->name);
    ++s_face_tour_tts_skip;
    return;
  }
  if (idx == static_cast<int>(ClockFace::Radar)) {
    pm_presence_ble_set_suppressed(true);
    for (uint8_t i = 0; i < 8; ++i) {
      pm_presence_tick(millis());
      pm_presence_ble_end();
      pm_wifi_resume_after_ble();
      pm_wifi_poll();
      delay(75);
      if (pm_wifi_connected()) {
        break;
      }
    }
    Serial.printf("tour: radar BLE paused for TTS heap=%u largest=%u wifi=%s\n",
                  static_cast<unsigned>(pm_heap_internal_free()),
                  static_cast<unsigned>(pm_heap_internal_largest()),
                  pm_wifi_connected() ? "yes" : "no");
  }
  if (!pm_wifi_connected()) {
    Serial.printf("tour: %s skipped %d %s reason=no wifi\n", s_face_tour_button_test ? "tts" : "narrate", idx,
                  info->name);
    ++s_face_tour_tts_skip;
    return;
  }
  if (pm_speaker_is_playing()) {
    return;
  }
  face_tour_voice_reset();
  bool started = false;
  if (s_face_tour_button_test) {
    if (!face_voice_build_prompt(info, idx, s_face_tour_voice_msg, kFaceTourVoiceMsgCap,
                                 s_face_tour_sys_prompt, kFaceTourSysPromptCap, true)) {
      Serial.printf("tour: tts skipped %d %s reason=%s\n", idx, info->name, g_gesture_banner);
      ++s_face_tour_tts_skip;
      return;
    }
    started = pm_voice_begin_message_ex(s_face_tour_voice_msg, s_face_tour_sys_prompt, s_face_voice_face,
                                        s_face_voice_faculty_slug, s_face_voice_faculty_name,
                                        &s_face_tour_voice_result);
  } else {
    PmFacultyProfile guide = {};
    if (pm_faculty_active(&guide)) {
      snprintf(s_face_voice_faculty_slug, sizeof(s_face_voice_faculty_slug), "%s", guide.slug);
      snprintf(s_face_voice_faculty_name, sizeof(s_face_voice_faculty_name), "%s", guide.name);
    } else {
      s_face_voice_faculty_slug[0] = '\0';
      s_face_voice_faculty_name[0] = '\0';
    }
    const char *health = face_tour_health_text(info);
    snprintf(s_face_tour_voice_msg, kFaceTourVoiceMsgCap,
             "Astrolabe tour face %d of %d, %s. Tour guide: %s. Product purpose: %s. Demo state: %s. "
             "Introduce what this face is meant to do for someone seeing the device for the first time.",
             idx + 1, static_cast<int>(ClockFace::kNumFaces), info->name,
             guide.valid ? guide.name : "Mynah", info->summary, health);
    started = pm_voice_begin_message_ex(s_face_tour_voice_msg,
                                        "You narrate a first-time product tour for the Mynah Astrolabe. Speak as "
                                        "the selected faculty guide when one is provided. Be warm, concrete, and "
                                        "brief. Explain intended product value, not implementation details, unless "
                                        "something is unavailable.",
                                        "tour", s_face_voice_faculty_slug, s_face_voice_faculty_name,
                                        &s_face_tour_voice_result);
  }
  if (!started) {
    Serial.printf("tour: narrate skipped %s err=%s\n", info->name, pm_voice_last_error());
    ++s_face_tour_tts_skip;
    return;
  }
  s_face_tour_voice_phase = 1;
  s_face_tour_voice_started_ms = millis();
  Serial.printf("tour: %s %d %s\n", s_face_tour_button_test ? "tts" : "narrating", idx, info->name);
}

static bool face_tour_voice_retry(const char *reason) {
  if ((!s_face_tour_narrate && !s_face_tour_button_test) || s_face_tour_idx < 0 ||
      s_face_tour_idx >= static_cast<int>(ClockFace::kNumFaces)) {
    return false;
  }
  if (s_face_tour_tts_retry >= 2) {
    return false;
  }
  const FaceTourInfo *info = face_tour_info(s_face_tour_idx);
  if (!info) {
    return false;
  }
  ++s_face_tour_tts_retry;
  Serial.printf("tour: retry %d %s attempt=%u reason=%s\n", s_face_tour_idx, info->name,
                static_cast<unsigned>(s_face_tour_tts_retry), reason ? reason : "failed");
  face_tour_voice_reset();
  pm_voice_release_idle_task();
  delay(350);
  face_tour_voice_start(info, s_face_tour_idx);
  return s_face_tour_voice_phase == 1;
}

static void face_tour_prefetch_pump(uint32_t wait_ms) {
  const uint32_t start = millis();
  do {
#ifndef ASTROLABE_QEMU
    pm_screen_http_loop();
    pm_wifi_poll();
#endif
    if (pm_rocket_consume_fetch(&g_rocket_ui)) {
      s_rocket_have_data = true;
      s_last_rocket_poll_ms = millis();
    }
    if (pm_castalia_tick_background_pairing() || pm_castalia_tick_pair_start()) {
      g_clock_repaint_pending = true;
    }
    if (pm_castalia_has_session()) {
      (void)pm_castalia_tick_refresh_session();
    }
    (void)pm_castalia_release_idle_task();
    (void)pm_rocket_release_idle_task();
    (void)pm_speaker_poll();
    (void)pm_voice_release_idle_task();
    if (wait_ms == 0) {
      break;
    }
    delay(50);
  } while (millis() - start < wait_ms);
}

static void face_tour_wait_for_bust(uint32_t timeout_ms) {
  const uint32_t start = millis();
  while (pm_faculty_bust_status() == PmFacultyBustStatus::Working &&
         millis() - start < timeout_ms) {
    face_tour_prefetch_pump(50);
  }
}

static bool face_tour_guide_bust_ensure(void) {
  PmFacultyProfile guide = {};
  if (!pm_faculty_active(&guide)) {
    pm_faculty_ensure_demo_seed();
    (void)pm_faculty_active(&guide);
  }
  if (!guide.valid) {
    return false;
  }
  if (strcmp(s_face_tour_guide_slug, guide.slug) != 0) {
    strncpy(s_face_tour_guide_slug, guide.slug, sizeof(s_face_tour_guide_slug) - 1);
    s_face_tour_guide_slug[sizeof(s_face_tour_guide_slug) - 1] = '\0';
    s_face_tour_bust_retry_ms = 0;
  }
  pm_faculty_pin_bust(guide.slug);
  if (pm_faculty_bust_ready_for(guide.slug)) {
    return true;
  }
  const uint32_t now = millis();
  if (s_face_tour_bust_retry_ms != 0 && now - s_face_tour_bust_retry_ms < 1500u) {
    return false;
  }
  s_face_tour_bust_retry_ms = now;
  (void)pm_faculty_request_bust(guide.slug);
  return pm_faculty_bust_ready_for(guide.slug);
}

static bool face_tour_wait_for_heap(uint32_t min_free, uint32_t min_largest, uint32_t timeout_ms) {
  const uint32_t start = millis();
  while ((pm_heap_internal_free() < min_free || pm_heap_internal_largest() < min_largest) &&
         millis() - start < timeout_ms) {
    face_tour_prefetch_pump(100);
  }
  return pm_heap_internal_free() >= min_free && pm_heap_internal_largest() >= min_largest;
}

static void face_tour_prefetch(void) {
  Serial.printf("tour: prefetch begin heap=%u largest=%u psram=%u\n",
                static_cast<unsigned>(pm_heap_internal_free()),
                static_cast<unsigned>(pm_heap_internal_largest()),
                static_cast<unsigned>(pm_heap_psram_free()));
  face_tour_prefetch_pump(0);

  if (!pm_wifi_connected()) {
    Serial.println("tour: prefetch offline");
    return;
  }

  if (pm_time_valid() && !g_calcifer_ui.ok &&
      pm_heap_internal_free() >= MYNAH_FACE_FETCH_MIN_HEAP) {
    (void)pm_calcifer_fetch(&g_calcifer_ui, time(nullptr));
    s_calcifer_have_data = true;
    s_last_calcifer_poll_ms = millis();
    Serial.printf("tour: prefetch calcifer ok=%d heap=%u largest=%u\n", g_calcifer_ui.ok ? 1 : 0,
                  static_cast<unsigned>(pm_heap_internal_free()),
                  static_cast<unsigned>(pm_heap_internal_largest()));
  }

  if (!g_weather_ui.ok && pm_heap_internal_free() >= MYNAH_FACE_FETCH_MIN_HEAP) {
    (void)pm_weather_fetch(&g_weather_ui);
    if (!g_weather_ui.ok) {
      struct tm local = {};
      const bool valid = pm_time_valid();
      if (valid) {
        pm_time_local(&local);
      }
      pm_weather_fill_demo(&g_weather_ui, valid ? local.tm_hour : 12);
    }
    s_weather_have_data = true;
    s_last_weather_poll_ms = millis();
    Serial.printf("tour: prefetch weather ok=%d heap=%u largest=%u\n", g_weather_ui.ok ? 1 : 0,
                  static_cast<unsigned>(pm_heap_internal_free()),
                  static_cast<unsigned>(pm_heap_internal_largest()));
  }

  if (!g_quotes_ui.ok && pm_heap_internal_free() >= MYNAH_FACE_FETCH_MIN_HEAP) {
    (void)pm_quotes_fetch(&g_quotes_ui);
    if (!g_quotes_ui.ok) {
      pm_quotes_fill_demo(&g_quotes_ui);
    }
    s_quotes_have_data = true;
    s_last_quotes_poll_ms = millis();
    Serial.printf("tour: prefetch quotes ok=%d faculty=%s heap=%u largest=%u\n", g_quotes_ui.ok ? 1 : 0,
                  g_quotes_ui.faculty_slug,
                  static_cast<unsigned>(pm_heap_internal_free()),
                  static_cast<unsigned>(pm_heap_internal_largest()));
  }

  PmFacultyProfile guide = {};
  if (pm_faculty_active(&guide)) {
    (void)pm_faculty_request_bust(guide.slug);
    face_tour_wait_for_bust(8000);
  }
  if (g_quotes_ui.ok && g_quotes_ui.faculty_slug[0]) {
    (void)pm_faculty_request_bust(g_quotes_ui.faculty_slug);
    face_tour_wait_for_bust(8000);
  }

  if (pm_time_valid() && (!g_rocket_ui.ok || g_rocket_ui.count <= 0)) {
    if (pm_rocket_request_fetch()) {
      face_tour_prefetch_pump(16000);
    }
    if (pm_rocket_consume_fetch(&g_rocket_ui)) {
      s_rocket_have_data = true;
      s_last_rocket_poll_ms = millis();
    }
    Serial.printf("tour: prefetch rocket ok=%d count=%d heap=%u largest=%u\n",
                  g_rocket_ui.ok ? 1 : 0, g_rocket_ui.count,
                  static_cast<unsigned>(pm_heap_internal_free()),
                  static_cast<unsigned>(pm_heap_internal_largest()));
  }

  pm_castalia_warmup_after_wifi();
  face_tour_prefetch_pump(14000);
  if (!face_tour_wait_for_heap(50000u, 28000u, 12000u)) {
    Serial.printf("tour: prefetch heap still low heap=%u largest=%u\n",
                  static_cast<unsigned>(pm_heap_internal_free()),
                  static_cast<unsigned>(pm_heap_internal_largest()));
  }
  Serial.printf("tour: prefetch done heap=%u largest=%u psram=%u\n",
                static_cast<unsigned>(pm_heap_internal_free()),
                static_cast<unsigned>(pm_heap_internal_largest()),
                static_cast<unsigned>(pm_heap_psram_free()));
}

static void face_tour_select(int idx) {
  const FaceTourInfo *info = face_tour_info(idx);
  if (!info) {
    return;
  }
  if (idx == static_cast<int>(ClockFace::Settings)) {
    pm_settings_set_page(SettingsPage::WiFi);
  }
  pm_presence_ble_set_suppressed((s_face_tour_narrate || s_face_tour_button_test) &&
                                 idx == static_cast<int>(ClockFace::Radar));
  pm_faces_set(static_cast<ClockFace>(idx));
  if (pm_faces_current() == ClockFace::CalciferCountdown && g_calcifer_ui.ok) {
    s_calcifer_have_data = true;
  }
  if (pm_faces_current() == ClockFace::Weather && g_weather_ui.ok) {
    s_weather_have_data = true;
  }
  if (pm_faces_current() == ClockFace::Rocket && g_rocket_ui.ok) {
    s_rocket_have_data = true;
  }
  if ((s_face_tour_narrate || s_face_tour_button_test) &&
      (idx == static_cast<int>(ClockFace::Spectrum) || idx == static_cast<int>(ClockFace::Tuning))) {
    pm_audio_analyzer_mic_end();
  }
  PmFacultyProfile guide = {};
  if (pm_faculty_active(&guide)) {
    (void)pm_faculty_request_bust(guide.slug);
  }
  (void)face_tour_guide_bust_ensure();
  s_face_tour_tts_retry = 0;
  s_face_tour_loaded_idx = idx;
  s_face_tour_overlay_started_ms = millis();
  snprintf(g_gesture_banner, sizeof(g_gesture_banner), "tour: %.28s", info->name);
  g_clock_repaint_pending = true;
  const char *health = face_tour_health_text(info);
  Serial.printf("tour: loading %d %s heap=%u largest=%u psram=%u health=%s - %s\n", idx, info->name,
                static_cast<unsigned>(pm_heap_internal_free()), static_cast<unsigned>(pm_heap_internal_largest()),
                static_cast<unsigned>(pm_heap_psram_free()), health, info->summary);
  face_tour_voice_start(info, idx);
}

static void face_tour_note_current_face(void) {
  if (!s_face_tour_active || s_face_tour_auto_advance || g_state != AppState::kClock) {
    return;
  }
  const int idx = static_cast<int>(pm_faces_current());
  const FaceTourInfo *info = face_tour_info(idx);
  if (!info || idx == s_face_tour_loaded_idx) {
    return;
  }
  if (idx == static_cast<int>(ClockFace::Settings) && pm_settings_page() == SettingsPage::Tour) {
    return;
  }
  face_tour_voice_reset();
  s_face_tour_idx = idx;
  s_face_tour_tts_retry = 0;
  s_face_tour_loaded_idx = idx;
  s_face_tour_overlay_started_ms = millis();
  tour_mark_face_seen(idx);
  (void)face_tour_guide_bust_ensure();
  snprintf(g_gesture_banner, sizeof(g_gesture_banner), "tour: %.28s", info->name);
  g_clock_repaint_pending = true;
  Serial.printf("tour: seen %d %s progress=%u/%u\n", idx, info->name,
                static_cast<unsigned>(pm_tour_seen_count()),
                static_cast<unsigned>(pm_tour_total_count()));
  face_tour_voice_start(info, idx);
}

static void face_tour_start(uint32_t dwell_ms, bool narrate = false, bool button_test = false,
                            bool auto_advance = true) {
  if (dwell_ms < 900u) {
    dwell_ms = 900u;
  } else if (dwell_ms > 45000u) {
    dwell_ms = 45000u;
  }
  if (g_state != AppState::kClock) {
    gesture_end_voice_ui();
  }
  face_tour_voice_reset();
  s_face_tour_active = true;
  s_face_tour_auto_advance = auto_advance;
  s_face_tour_narrate = narrate;
  s_face_tour_button_test = button_test;
  s_face_tour_tts_ok = 0;
  s_face_tour_tts_fail = 0;
  s_face_tour_tts_skip = 0;
  s_face_tour_tts_retry = 0;
  s_face_tour_idx = 0;
  s_face_tour_loaded_idx = -1;
  s_face_tour_overlay_started_ms = 0;
  s_face_tour_dwell_ms = dwell_ms;
  s_face_tour_last_ms = 0;
  s_face_tour_tts_retry = 0;
  Serial.printf("tour: start faces=%d dwell_ms=%u narrate=%d tts=%d wifi=%d time=%d\n",
                static_cast<int>(ClockFace::kNumFaces), static_cast<unsigned>(s_face_tour_dwell_ms),
                s_face_tour_narrate ? 1 : 0, s_face_tour_button_test ? 1 : 0, pm_wifi_connected() ? 1 : 0,
                pm_time_valid() ? 1 : 0);
  if (s_face_tour_narrate || s_face_tour_button_test) {
    face_tour_prefetch();
    if (!pm_speaker_prepare()) {
      Serial.printf("tour: speaker prepare failed heap=%u largest=%u\n",
                    static_cast<unsigned>(pm_heap_internal_free()),
                    static_cast<unsigned>(pm_heap_internal_largest()));
    }
    pm_speaker_set_auto_release(false);
  }
  if (s_face_tour_auto_advance) {
    face_tour_select(s_face_tour_idx);
  } else {
    face_tour_note_current_face();
  }
}

static void face_tour_stop(void) {
  if (!s_face_tour_active) {
    Serial.println("tour: stopped");
    return;
  }
  s_face_tour_active = false;
  s_face_tour_auto_advance = false;
  s_face_tour_narrate = false;
  s_face_tour_button_test = false;
  s_face_tour_idx = 0;
  s_face_tour_loaded_idx = -1;
  s_face_tour_last_ms = 0;
  s_face_tour_overlay_started_ms = 0;
  s_face_tour_bust_retry_ms = 0;
  s_face_tour_guide_slug[0] = '\0';
  pm_presence_ble_set_suppressed(false);
  face_tour_voice_reset();
  pm_faculty_unpin_bust();
  pm_speaker_set_auto_release(true);
  tour_mode_set_enabled(false);
  g_gesture_banner[0] = '\0';
  g_clock_repaint_pending = true;
  Serial.println("tour: stopped");
}

static void face_tour_draw_guide_overlay(void) {
  if (!s_face_tour_active || !pm_gfx) {
    return;
  }
  PmFacultyProfile guide = {};
  if (!pm_faculty_active(&guide)) {
    pm_faculty_ensure_demo_seed();
    (void)pm_faculty_active(&guide);
  }
  if (!guide.valid) {
    return;
  }
  if (!pm_faculty_bust_ready_for(guide.slug)) {
    return;
  }
  const int box = LCD_WIDTH / 4;
  const int pad = 8;
  const uint32_t elapsed = millis() - s_face_tour_overlay_started_ms;
  const uint32_t dur = 520u;
  const int start_x = -box - 8;
  const int end_x = pad;
  const int x = (elapsed >= dur) ? end_x
                                 : start_x + static_cast<int>((end_x - start_x) * elapsed / dur);
  const int y = LCD_HEIGHT - box - pad;
  pm_gfx->fillRoundRect(x - 3, y - 3, box + 6, box + 6, 6, pm_gfx->color565(6, 8, 16));
  pm_gfx->drawRoundRect(x - 3, y - 3, box + 6, box + 6, 6, pm_gfx->color565(120, 150, 220));
  (void)pm_faculty_draw_real_bust_for_at(&guide, x + box / 2, y + box - 2, box, box);
}

static bool face_voice_begin_current(void) {
  if (!voice_prompt_buffers_ensure()) {
    snprintf(g_gesture_banner, sizeof(g_gesture_banner), "voice: PSRAM OOM");
    return false;
  }
  if (!pm_wifi_connected()) {
    snprintf(g_gesture_banner, sizeof(g_gesture_banner), "voice: need WiFi");
    return false;
  }
  const int idx = static_cast<int>(pm_faces_current());
  const FaceTourInfo *info = face_tour_info(idx);
  if (!info) {
    snprintf(g_gesture_banner, sizeof(g_gesture_banner), "voice: no face prompt");
    return false;
  }
  if (!face_voice_build_prompt(info, idx, s_face_tour_voice_msg, kFaceTourVoiceMsgCap,
                               s_face_tour_sys_prompt, kFaceTourSysPromptCap, false)) {
    return false;
  }
  pm_voice_result_free(&g_voice_result);
  g_voice_use_message = true;
  g_text_voice_route = k_tv_face;
  g_calcifer_briefing = false;
  g_daily_briefing = false;
  g_astro_voice_active = false;
  g_astro_voice_pcm = false;
  g_synastry_voice_active = false;
  g_synastry_voice_pcm = false;
  g_moon_fortune_active = false;
  g_moon_voice_pcm = false;
  g_voice_play_reset = true;
  g_state = AppState::kThinking;
  return true;
}

static void face_tour_tick(uint32_t now) {
  if (!s_face_tour_active || g_state != AppState::kClock) {
    return;
  }
  if (pm_faculty_tick(now)) {
    g_clock_repaint_pending = true;
  }
  (void)face_tour_guide_bust_ensure();
  if (pm_faculty_bust_status() == PmFacultyBustStatus::Working) {
    g_clock_repaint_pending = true;
  }
  if (!s_face_tour_auto_advance && s_face_tour_overlay_started_ms != 0 &&
      now - s_face_tour_overlay_started_ms < 620u) {
    g_clock_repaint_pending = true;
  }
  if (s_face_tour_voice_phase == 1) {
    const PmVoiceStatus vs = pm_voice_poll();
    if (vs == PmVoiceStatus::Working) {
      if (s_face_tour_voice_started_ms != 0 && static_cast<int32_t>(now - s_face_tour_voice_started_ms) < 0) {
        return;
      }
      if (s_face_tour_voice_started_ms != 0 && now - s_face_tour_voice_started_ms > 120000u) {
        Serial.printf("tour: narrate timeout %d err=%s\n", s_face_tour_idx, pm_voice_last_error());
        if (face_tour_voice_retry("timeout")) {
          return;
        }
        ++s_face_tour_tts_fail;
        face_tour_voice_reset();
      } else {
        return;
      }
    } else if (vs == PmVoiceStatus::DoneOk && s_face_tour_voice_result.audio_streamed) {
      pm_voice_release_idle_task();
      ++s_face_tour_tts_ok;
      Serial.printf("tour: tts ok %d\n", s_face_tour_idx);
      face_tour_voice_reset();
    } else if (vs == PmVoiceStatus::DoneOk && s_face_tour_voice_result.mp3 &&
               s_face_tour_voice_result.mp3_len >= 64) {
      pm_voice_release_idle_task();
      if (pm_speaker_play_begin(s_face_tour_voice_result.mp3, s_face_tour_voice_result.mp3_len)) {
        s_face_tour_voice_phase = 2;
        return;
      }
      Serial.printf("tour: narrate speaker busy %d\n", s_face_tour_idx);
      if (face_tour_voice_retry("speaker busy")) {
        return;
      }
      ++s_face_tour_tts_fail;
      face_tour_voice_reset();
    } else if (vs == PmVoiceStatus::DoneOk) {
      Serial.printf("tour: narrate no audio %d\n", s_face_tour_idx);
      if (face_tour_voice_retry("no audio")) {
        return;
      }
      ++s_face_tour_tts_fail;
      face_tour_voice_reset();
    } else if (vs == PmVoiceStatus::DoneFail) {
      Serial.printf("tour: narrate failed %d err=%s\n", s_face_tour_idx, pm_voice_last_error());
      if (face_tour_voice_retry(pm_voice_last_error())) {
        return;
      }
      ++s_face_tour_tts_fail;
      face_tour_voice_reset();
    }
  }
  if (s_face_tour_voice_phase == 2) {
    const PmSpeakerStatus spk = pm_speaker_poll();
    if (spk == PmSpeakerStatus::Playing) {
      return;
    }
    if (spk == PmSpeakerStatus::DoneOk) {
      ++s_face_tour_tts_ok;
      Serial.printf("tour: tts ok %d\n", s_face_tour_idx);
    } else if (spk == PmSpeakerStatus::DoneFail) {
      if (face_tour_voice_retry("speaker failed")) {
        return;
      }
      ++s_face_tour_tts_fail;
      Serial.printf("tour: tts speaker failed %d\n", s_face_tour_idx);
    }
    face_tour_voice_reset();
  }
  if (!s_face_tour_auto_advance) {
    return;
  }
  if (s_face_tour_last_ms == 0) {
    s_face_tour_last_ms = now;
    return;
  }
  if (now - s_face_tour_last_ms < s_face_tour_dwell_ms) {
    return;
  }
  s_face_tour_last_ms = now;
  ++s_face_tour_idx;
  if (s_face_tour_idx >= static_cast<int>(ClockFace::kNumFaces)) {
    const bool report_tts_tour = s_face_tour_narrate || s_face_tour_button_test;
    s_face_tour_active = false;
    s_face_tour_auto_advance = false;
    s_face_tour_narrate = false;
    s_face_tour_button_test = false;
    s_face_tour_idx = 0;
    if (s_face_tour_first_run) {
      tour_played_set(true);
      s_face_tour_first_run = false;
    }
    pm_presence_ble_set_suppressed(false);
    face_tour_voice_reset();
    pm_faculty_unpin_bust();
    pm_speaker_set_auto_release(true);
    g_gesture_banner[0] = '\0';
    g_clock_repaint_pending = true;
    if (report_tts_tour) {
      Serial.printf("tour: summary tts_ok=%u tts_fail=%u tts_skip=%u\n",
                    static_cast<unsigned>(s_face_tour_tts_ok), static_cast<unsigned>(s_face_tour_tts_fail),
                    static_cast<unsigned>(s_face_tour_tts_skip));
      if (s_face_tour_tts_ok == static_cast<uint8_t>(ClockFace::kNumFaces) && s_face_tour_tts_fail == 0 &&
          s_face_tour_tts_skip == 0) {
        Serial.println("TTS_TOUR PASS");
      } else {
        Serial.println("TTS_TOUR FAIL");
      }
    }
    Serial.println("tour: done");
    return;
  }
  face_tour_select(s_face_tour_idx);
}

static void enter_rom_bootloader_from_serial(bool dfu) {
  pm_log_printf(false, "serial: entering %s bootloader", dfu ? "DFU" : "USB CDC");
  Serial.printf("bootloader: entering %s bootloader\n", dfu ? "DFU" : "USB CDC");
  Serial.flush();
  delay(100);
  usb_persist_restart(dfu ? RESTART_BOOTLOADER_DFU : RESTART_BOOTLOADER);
}

static bool parse_token(char **cursor, char *out, size_t out_sz) {
  if (!cursor || !*cursor || !out || out_sz == 0) {
    return false;
  }
  char *p = *cursor;
  while (*p == ' ') {
    ++p;
  }
  if (*p == '\0') {
    out[0] = '\0';
    *cursor = p;
    return false;
  }
  size_t o = 0;
  if (*p == '"') {
    ++p;
    while (*p && *p != '"' && o + 1 < out_sz) {
      out[o++] = *p++;
    }
    if (*p == '"') {
      ++p;
    }
  } else {
    while (*p && *p != ' ' && o + 1 < out_sz) {
      out[o++] = *p++;
    }
  }
  out[o] = '\0';
  while (*p == ' ') {
    ++p;
  }
  *cursor = p;
  return o > 0;
}

static bool handle_wifi_serial_command(char *line) {
  if (!line || strncmp(line, "wifi", 4) != 0 || (line[4] != '\0' && line[4] != ' ')) {
    return false;
  }
  char *p = line + 4;
  char cmd[16];
  if (!parse_token(&p, cmd, sizeof(cmd))) {
    char ssid[64];
    char pass[64];
    const bool have = pm_wifi_credentials_load(ssid, sizeof(ssid), pass, sizeof(pass));
    Serial.printf("wifi: connected=%d status=%d host=%s mac=%s ssid=%s ip=%s rssi=%d nvs=%d\n",
                  pm_wifi_connected() ? 1 : 0,
                  pm_wifi_status_code(), pm_wifi_mdns_name(), pm_wifi_mac_string(), have ? ssid : "",
                  pm_wifi_local_ip(), pm_wifi_rssi(),
                  have ? 1 : 0);
    return true;
  }
  if (strcmp(cmd, "scan") == 0) {
    pm_wifi_print_scan();
    return true;
  }
  if (strcmp(cmd, "clear") == 0) {
    if (pm_wifi_credentials_clear()) {
      Serial.println("wifi: cleared NVS credentials");
    } else {
      Serial.println("wifi: clear failed");
    }
    return true;
  }
  if (strcmp(cmd, "reconnect") == 0) {
    const bool ok = pm_wifi_reconnect();
    Serial.printf("wifi: reconnect %s ip=%s; ntp/http handled by loop\n", ok ? "ok" : "failed",
                  pm_wifi_local_ip());
    g_clock_repaint_pending = true;
    return true;
  }
  char ssid[64];
  char pass[64];
  strncpy(ssid, cmd, sizeof(ssid) - 1);
  ssid[sizeof(ssid) - 1] = '\0';
  if (!parse_token(&p, pass, sizeof(pass))) {
    pass[0] = '\0';
  }
  if (!pm_wifi_credentials_save(ssid, pass)) {
    Serial.println("wifi: save failed; usage: wifi \"SSID\" \"password\"");
    return true;
  }
  Serial.printf("wifi: saved ssid=%s, reconnecting\n", ssid);
  const bool ok = pm_wifi_reconnect();
  Serial.printf("wifi: reconnect %s ip=%s; ntp/http handled by loop\n", ok ? "ok" : "failed",
                pm_wifi_local_ip());
  g_clock_repaint_pending = true;
  return true;
}

static void handle_tour_command(const char *args) {
  const char *p = args ? args : "";
  while (*p == ' ') {
    ++p;
  }
  if (strcmp(p, "stop") == 0) {
    face_tour_stop();
    return;
  }
  if (strcmp(p, "reset") == 0 || strcmp(p, "unplayed") == 0) {
    tour_seen_reset();
    s_face_tour_loaded_idx = -1;
    Serial.println("tour: seen reset");
    return;
  }
  if (strcmp(p, "mode on") == 0 || strcmp(p, "on") == 0) {
    tour_mode_set_enabled(true);
    face_tour_start(1200u, true, false, false);
    Serial.println("tour: mode on");
    return;
  }
  if (strcmp(p, "mode off") == 0 || strcmp(p, "off") == 0) {
    face_tour_stop();
    Serial.println("tour: mode off");
    return;
  }
  if (strcmp(p, "played") == 0 || strcmp(p, "status") == 0) {
    Serial.printf("tour: played=%d active=%d mode=%d seen=%u/%u guide=",
                  tour_played_load() ? 1 : 0, s_face_tour_active ? 1 : 0,
                  pm_tour_mode_enabled() ? 1 : 0,
                  static_cast<unsigned>(pm_tour_seen_count()),
                  static_cast<unsigned>(pm_tour_total_count()));
    PmFacultyProfile guide = {};
    if (pm_faculty_active(&guide)) {
      Serial.printf("%s (%s)\n", guide.name, guide.slug);
    } else {
      Serial.println("-");
    }
    return;
  }
  bool narrate = false;
  bool button_test = false;
  if (strncmp(p, "narrate", 7) == 0 && (p[7] == '\0' || p[7] == ' ')) {
    narrate = true;
    p += 7;
  } else if (strncmp(p, "voice", 5) == 0 && (p[5] == '\0' || p[5] == ' ')) {
    narrate = true;
    p += 5;
  } else if (strncmp(p, "tts", 3) == 0 && (p[3] == '\0' || p[3] == ' ')) {
    narrate = true;
    button_test = true;
    p += 3;
  } else if (strncmp(p, "button", 6) == 0 && (p[6] == '\0' || p[6] == ' ')) {
    narrate = true;
    button_test = true;
    p += 6;
  } else if (strncmp(p, "press", 5) == 0 && (p[5] == '\0' || p[5] == ' ')) {
    narrate = true;
    button_test = true;
    p += 5;
  }
  while (*p == ' ') {
    ++p;
  }
  char *end = nullptr;
  const long dwell = strtol(p, &end, 10);
  face_tour_start((end != p && dwell > 0) ? static_cast<uint32_t>(dwell) : (narrate ? 1200u : 2800u),
                  narrate, button_test);
}

static void print_time_status(const char *prefix) {
  const time_t epoch = time(nullptr);
  struct tm utc = {};
  struct tm local = {};
  pm_time_utc(&utc);
  pm_time_local(&local);
  char utc_s[28];
  char local_s[28];
  strftime(utc_s, sizeof(utc_s), "%Y-%m-%dT%H:%M:%SZ", &utc);
  strftime(local_s, sizeof(local_s), "%Y-%m-%d %H:%M:%S", &local);
  Serial.printf("%s: valid=%d epoch=%lld utc=%s local=%s offset_sec=%ld wifi=%d ip=%s\n",
                prefix ? prefix : "time", pm_time_valid() ? 1 : 0, static_cast<long long>(epoch), utc_s, local_s,
                static_cast<long>(pm_geo_tz_offset_sec()), pm_wifi_connected() ? 1 : 0,
                pm_wifi_local_ip());
}

static void qa_mic_probe() {
  if (!pm_mic_begin()) {
    Serial.println("qa: mic begin failed");
    return;
  }
  const size_t ns = pm_mic_frame_samples();
  const int nch = pm_mic_i2s_channels();
  static int16_t raw[512 * 4];
  if (nch <= 0 || ns == 0 || ns * static_cast<size_t>(nch) > sizeof(raw) / sizeof(raw[0])) {
    Serial.printf("qa: mic bad geometry ns=%u channels=%d\n", static_cast<unsigned>(ns), nch);
    pm_mic_stop();
    return;
  }
  uint32_t peak[4] = {};
  uint64_t sum_abs[4] = {};
  uint32_t frames_ok = 0;
  for (uint8_t frame = 0; frame < 20; ++frame) {
    size_t br = 0;
    if (!pm_mic_read_frame(raw, ns, &br) || br == 0) {
      continue;
    }
    ++frames_ok;
    for (size_t i = 0; i < ns; ++i) {
      for (int ch = 0; ch < nch && ch < 4; ++ch) {
        const int32_t sample = raw[i * static_cast<size_t>(nch) + static_cast<size_t>(ch)];
        const uint32_t a = static_cast<uint32_t>(sample < 0 ? -sample : sample);
        if (a > peak[ch]) {
          peak[ch] = a;
        }
        sum_abs[ch] += a;
      }
    }
  }
  pm_mic_stop();
  Serial.printf("qa: mic frames=%u ns=%u channels=%d capture_slots=%d,%d", static_cast<unsigned>(frames_ok),
                static_cast<unsigned>(ns), nch, pm_mic_capture_slot(0), pm_mic_capture_slot(1));
  for (int ch = 0; ch < nch && ch < 4; ++ch) {
    const uint32_t avg = frames_ok > 0 ? static_cast<uint32_t>(sum_abs[ch] / (frames_ok * ns)) : 0u;
    Serial.printf(" ch%d_peak=%u ch%d_avg=%u", ch, static_cast<unsigned>(peak[ch]), ch, static_cast<unsigned>(avg));
  }
  Serial.println();
}

static void qa_note_capture() {
  if (!pm_mic_begin()) {
    Serial.println("qa: note mic begin failed");
    return;
  }
  const size_t ns = pm_mic_frame_samples();
  const int nch = pm_mic_i2s_channels();
  static int16_t raw[512 * 4];
  static int16_t mono[512];
  const size_t frame_bytes = ns * sizeof(int16_t);
  if (nch <= 0 || ns == 0 || ns > sizeof(mono) / sizeof(mono[0]) ||
      ns * static_cast<size_t>(nch) > sizeof(raw) / sizeof(raw[0]) || !g_pcm ||
      frame_bytes == 0) {
    Serial.printf("qa: note bad geometry ns=%u channels=%d pcm=%d\n", static_cast<unsigned>(ns), nch,
                  g_pcm ? 1 : 0);
    pm_mic_stop();
    return;
  }
  size_t pcm_len = 0;
  uint32_t capture_peak = 0;
  uint32_t frames_ok = 0;
  const uint8_t target_frames = 34;  // About one second at 30 ms per frame.
  for (uint8_t frame = 0; frame < target_frames && pcm_len + frame_bytes <= MYNAH_VOICE_MAX_PCM_BYTES; ++frame) {
    size_t br = 0;
    if (!pm_mic_read_frame(raw, ns, &br) || br == 0) {
      continue;
    }
    ++frames_ok;
    int best_ch = 0;
    uint32_t best_peak = 0;
    const int capture_ch = pm_mic_capture_channels();
    for (int ch = 0; ch < capture_ch; ++ch) {
      const int slot = pm_mic_capture_slot(ch);
      if (slot < 0 || slot >= nch) {
        continue;
      }
      uint32_t peak = 0;
      for (size_t i = 0; i < ns; ++i) {
        const int32_t sample = raw[i * static_cast<size_t>(nch) + static_cast<size_t>(slot)];
        const uint32_t a = static_cast<uint32_t>(sample < 0 ? -sample : sample);
        if (a > peak) {
          peak = a;
        }
      }
      if (peak > best_peak) {
        best_peak = peak;
        best_ch = ch;
      }
    }
    if (best_peak > capture_peak) {
      capture_peak = best_peak;
    }
    if (!pm_mic_mix_capture_channels(raw, ns, mono)) {
      pm_mic_pick_capture_channel(raw, ns, best_ch, mono);
    }
    (void)pm_mic_repair_sparse_mono(mono, ns);
    pm_audio_analyzer_cancel_echo_channel(0, mono, ns);
    memcpy(g_pcm + pcm_len, mono, frame_bytes);
    pcm_len += frame_bytes;
  }
  pm_mic_stop();
  if (pcm_len < frame_bytes * 2) {
    Serial.printf("qa: note capture too short frames=%u bytes=%u peak=%u\n", static_cast<unsigned>(frames_ok),
                  static_cast<unsigned>(pcm_len), static_cast<unsigned>(capture_peak));
    return;
  }
  const bool ok = pm_commonplace_save_offline_note(g_pcm, pcm_len);
  if (ok) {
    Serial.printf("qa: note capture ok frames=%u bytes=%u peak=%u queued=%u\n", static_cast<unsigned>(frames_ok),
                  static_cast<unsigned>(pcm_len), static_cast<unsigned>(capture_peak),
                  static_cast<unsigned>(pm_commonplace_offline_note_count()));
  } else {
    Serial.printf("qa: note capture failed frames=%u bytes=%u peak=%u queued=%u err=%s\n",
                  static_cast<unsigned>(frames_ok), static_cast<unsigned>(pcm_len),
                  static_cast<unsigned>(capture_peak), static_cast<unsigned>(pm_commonplace_offline_note_count()),
                  pm_commonplace_last_error());
  }
}

static void qa_pcm_dump(uint32_t duration_ms) {
  if (duration_ms == 0 || duration_ms > 5000u) {
    duration_ms = 3000u;
  }
  ensure_pcm_buffer();
  if (!g_pcm) {
    Serial.println("qa: pcm dump no buffer");
    return;
  }
  if (!pm_mic_begin()) {
    Serial.println("qa: pcm dump mic begin failed");
    return;
  }

  const size_t ns = pm_mic_frame_samples();
  const int nch = pm_mic_i2s_channels();
  static int16_t raw[512 * 4];
  static int16_t mono[512];
  const size_t frame_bytes = ns * sizeof(int16_t);
  if (nch <= 0 || ns == 0 || ns > sizeof(mono) / sizeof(mono[0]) ||
      ns * static_cast<size_t>(nch) > sizeof(raw) / sizeof(raw[0]) || frame_bytes == 0) {
    Serial.printf("qa: pcm dump bad geometry ns=%u channels=%d\n", static_cast<unsigned>(ns), nch);
    pm_mic_stop();
    return;
  }

  const uint32_t target_frames = (duration_ms + 29u) / 30u;
  size_t pcm_len = 0;
  uint32_t peak = 0;
  uint32_t frames_ok = 0;
  Serial.printf("qa: pcm dump capture duration_ms=%u frames=%u\n", static_cast<unsigned>(duration_ms),
                static_cast<unsigned>(target_frames));
  for (uint32_t frame_idx = 0; frame_idx < target_frames &&
                               pcm_len + frame_bytes <= MYNAH_VOICE_MAX_PCM_BYTES;
       ++frame_idx) {
    size_t br = 0;
    if (!pm_mic_read_frame(raw, ns, &br) || br == 0) {
      continue;
    }
    ++frames_ok;
    if (!pm_mic_mix_capture_channels(raw, ns, mono)) {
      pm_mic_pick_capture_channel(raw, ns, 0, mono);
    }
    (void)pm_mic_repair_sparse_mono(mono, ns);
    pm_audio_analyzer_cancel_echo_channel(0, mono, ns);
    for (size_t i = 0; i < ns; ++i) {
      const int32_t sample = mono[i];
      const uint32_t a = static_cast<uint32_t>(sample < 0 ? -sample : sample);
      if (a > peak) {
        peak = a;
      }
    }
    memcpy(g_pcm + pcm_len, mono, frame_bytes);
    pcm_len += frame_bytes;
  }
  pm_mic_stop();

  size_t b64_len = 0;
  const int probe = mbedtls_base64_encode(nullptr, 0, &b64_len, g_pcm, pcm_len);
  if (probe != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL || b64_len == 0) {
    Serial.printf("qa: pcm dump b64 size failed rc=%d bytes=%u\n", probe, static_cast<unsigned>(pcm_len));
    return;
  }
  uint8_t *b64 = static_cast<uint8_t *>(heap_caps_malloc(b64_len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!b64) {
    b64 = static_cast<uint8_t *>(malloc(b64_len + 1));
  }
  if (!b64) {
    Serial.printf("qa: pcm dump oom b64 bytes=%u b64=%u\n", static_cast<unsigned>(pcm_len),
                  static_cast<unsigned>(b64_len));
    return;
  }
  size_t olen = 0;
  if (mbedtls_base64_encode(b64, b64_len + 1, &olen, g_pcm, pcm_len) != 0) {
    free(b64);
    Serial.println("qa: pcm dump b64 encode failed");
    return;
  }
  b64[olen] = '\0';

  Serial.printf("QA_PCM_BEGIN bytes=%u sample_rate=16000 channels=1 encoding=s16le frames=%u peak=%u\n",
                static_cast<unsigned>(pcm_len), static_cast<unsigned>(frames_ok), static_cast<unsigned>(peak));
  for (size_t off = 0; off < olen; off += 512u) {
    const size_t n = (olen - off > 512u) ? 512u : (olen - off);
    Serial.write(b64 + off, n);
    Serial.write('\n');
    delay(2);
  }
  Serial.println("QA_PCM_END");
  free(b64);
}

static bool qa_decode_mp3_to_16k_mono(const uint8_t *mp3, size_t mp3_len, uint8_t *pcm_out, size_t pcm_cap,
                                      size_t *pcm_len_out, uint32_t *peak_out) {
  if (!mp3 || mp3_len == 0 || !pcm_out || pcm_cap == 0 || !pcm_len_out || !peak_out) {
    return false;
  }
  *pcm_len_out = 0;
  *peak_out = 0;
  mp3dec_t *dec = static_cast<mp3dec_t *>(heap_caps_malloc(sizeof(mp3dec_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!dec) {
    dec = static_cast<mp3dec_t *>(malloc(sizeof(mp3dec_t)));
  }
  int16_t *frame_pcm = static_cast<int16_t *>(heap_caps_malloc(MINIMP3_MAX_SAMPLES_PER_FRAME * sizeof(int16_t),
                                                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!frame_pcm) {
    frame_pcm = static_cast<int16_t *>(malloc(MINIMP3_MAX_SAMPLES_PER_FRAME * sizeof(int16_t)));
  }
  if (!dec || !frame_pcm) {
    free(dec);
    free(frame_pcm);
    return false;
  }
  mp3dec_init(dec);
  const uint8_t *buf = mp3;
  int bytes_left = static_cast<int>(mp3_len);
  double src_pos = 0.0;
  int frame_hz = 0;

  while (bytes_left > 0 && *pcm_len_out + sizeof(int16_t) <= pcm_cap) {
    mp3dec_frame_info_t info = {};
    const int samples_per_ch = mp3dec_decode_frame(dec, buf, bytes_left, frame_pcm, &info);
    if (info.frame_bytes <= 0) {
      break;
    }
    buf += info.frame_bytes;
    bytes_left -= info.frame_bytes;
    if (samples_per_ch <= 0 || info.hz <= 0 || info.channels <= 0) {
      continue;
    }
    if (frame_hz != info.hz) {
      frame_hz = info.hz;
      src_pos = 0.0;
    }
    const double step = static_cast<double>(info.hz) / 16000.0;
    while (src_pos < samples_per_ch && *pcm_len_out + sizeof(int16_t) <= pcm_cap) {
      const int idx = static_cast<int>(src_pos);
      int32_t mixed = 0;
      for (int ch = 0; ch < info.channels; ++ch) {
        mixed += frame_pcm[idx * info.channels + ch];
      }
      int16_t sample = static_cast<int16_t>(mixed / info.channels);
      const int32_t a = sample < 0 ? -static_cast<int32_t>(sample) : static_cast<int32_t>(sample);
      if (static_cast<uint32_t>(a) > *peak_out) {
        *peak_out = static_cast<uint32_t>(a);
      }
      memcpy(pcm_out + *pcm_len_out, &sample, sizeof(sample));
      *pcm_len_out += sizeof(sample);
      src_pos += step;
    }
    src_pos -= samples_per_ch;
  }
  free(frame_pcm);
  free(dec);
  return *pcm_len_out > 0;
}

static void qa_stt_tts_loop() {
  static const char kPhrase[] = "Mynah test phrase.";
  static const char kTtsSys[] = "Speak the user's sentence exactly. Do not add words.";
  static const char kSttSys[] = "Transcribe the audio. Return only what was said.";

  ensure_pcm_buffer();
  if (!g_pcm) {
    Serial.println("qa: stt tts no pcm buffer");
    return;
  }
  if (!pm_wifi_connected()) {
    Serial.println("qa: stt tts need wifi");
    return;
  }
  pm_faculty_release_bust_cache();
  (void)pm_speaker_release_idle_task();
  (void)pm_voice_release_idle_task();
  Serial.printf("qa: stt tts heap_before=%u largest=%u psram=%u\n",
                static_cast<unsigned>(pm_heap_internal_free()),
                static_cast<unsigned>(pm_heap_internal_largest()),
                static_cast<unsigned>(pm_heap_psram_free()));

  PmVoiceResult tts = {};
  PmVoiceResult stt = {};
  Serial.printf("qa: stt tts phrase=\"%s\"\n", kPhrase);
  if (!pm_voice_post_message(kPhrase, kTtsSys, &tts) || !tts.mp3 || tts.mp3_len == 0) {
    Serial.printf("qa: stt tts tts failed err=%s\n", pm_voice_last_error());
    pm_voice_result_free(&tts);
    return;
  }
  Serial.printf("qa: stt tts mp3=%u\n", static_cast<unsigned>(tts.mp3_len));

  size_t pcm_len = 0;
  uint32_t peak = 0;
  if (!qa_decode_mp3_to_16k_mono(tts.mp3, tts.mp3_len, g_pcm, MYNAH_VOICE_MAX_PCM_BYTES, &pcm_len, &peak)) {
    Serial.println("qa: stt tts decode failed");
    pm_voice_result_free(&tts);
    return;
  }
  Serial.printf("qa: stt tts decoded pcm16k=%u peak=%u\n", static_cast<unsigned>(pcm_len),
                static_cast<unsigned>(peak));
  pm_voice_result_free(&tts);
  Serial.printf("qa: stt tts heap_pre_stt=%u largest=%u psram=%u\n",
                static_cast<unsigned>(pm_heap_internal_free()),
                static_cast<unsigned>(pm_heap_internal_largest()),
                static_cast<unsigned>(pm_heap_psram_free()));

  if (pm_voice_post_pcm(g_pcm, pcm_len, kSttSys, &stt)) {
    Serial.printf("qa: stt tts transcript=\"%s\" reply=\"%s\" mp3=%u\n", stt.transcript, stt.reply,
                  static_cast<unsigned>(stt.mp3_len));
  } else {
    Serial.printf("qa: stt tts stt failed err=%s\n", pm_voice_last_error());
  }
  pm_voice_result_free(&stt);
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
      if (pm_home_gem_pulse_serial_command(line)) {
        g_clock_repaint_pending = true;
      } else if (handle_wifi_serial_command(line)) {
        g_clock_repaint_pending = true;
      } else if (pm_user_serial_command(line)) {
        /* name saved */
      } else if (pm_castalia_serial_command(line)) {
        g_clock_repaint_pending = true;
      } else if (strncmp(line, "birth ", 6) == 0) {
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
          Serial.printf("qa: face=%d state=%d heap=%u iheap=%u largest=%u psram=%u psram_largest=%u res=0x%08x voice_stack_hw=%u spk_stack_hw=%u rocket_stack_hw=%u wifi=%d time=%d ip=%s name=%s banner=\"%s\"\n",
                        static_cast<int>(pm_faces_current()), static_cast<int>(g_state),
                        static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(pm_heap_internal_free()),
                        static_cast<unsigned>(pm_heap_internal_largest()), static_cast<unsigned>(pm_heap_psram_free()),
                        static_cast<unsigned>(pm_heap_psram_largest()),
                        static_cast<unsigned>(pm_resource_owners()),
                        static_cast<unsigned>(pm_voice_stack_high_water()),
                        static_cast<unsigned>(pm_speaker_stack_high_water()),
                        static_cast<unsigned>(pm_rocket_fetch_stack_high_water()), pm_wifi_connected() ? 1 : 0,
                        pm_time_valid() ? 1 : 0, pm_wifi_local_ip(), pm_user_display_name(),
                        g_gesture_banner);
        } else if (strcmp(args, "heap") == 0) {
          pm_heap_log("qa");
        } else if (strcmp(args, "audio") == 0) {
          PmAudioAnalyzerDebug dbg = {};
          pm_audio_analyzer_debug(&dbg);
          Serial.printf("qa: audio level=%.3f in0=%.3f/%u in1=%.3f/%u out=%.3f/%u route=%s\n",
                        static_cast<double>(dbg.level),
                        static_cast<double>(dbg.in_peak[0]), static_cast<unsigned>(dbg.in_blocks[0]),
                        static_cast<double>(dbg.in_peak[1]), static_cast<unsigned>(dbg.in_blocks[1]),
                        static_cast<double>(dbg.out_peak), static_cast<unsigned>(dbg.out_blocks),
                        pm_audio_route_get() == PmAudioRoute::Usb ? "usb" : "onboard");
        } else if (strcmp(args, "aec") == 0 || strcmp(args, "aec tone") == 0) {
          if (strcmp(args, "aec tone") == 0) {
            Serial.printf("qa: aec tone %s\n", pm_face_settings_aec_tap() ? "toggled" : "busy");
          }
          PmAudioAnalyzerDebug dbg = {};
          PmAudioAecDebug aec = {};
          pm_audio_analyzer_debug(&dbg);
          pm_audio_analyzer_aec_debug(&aec);
          Serial.printf("qa: aec ref=%.4f err0=%.4f err1=%.4f adapt0=%u adapt1=%u in0=%.4f in1=%.4f out=%.4f\n",
                        static_cast<double>(aec.ref_rms), static_cast<double>(aec.err_rms[0]),
                        static_cast<double>(aec.err_rms[1]), static_cast<unsigned>(aec.adapt_blocks[0]),
                        static_cast<unsigned>(aec.adapt_blocks[1]), static_cast<double>(dbg.in_peak[0]),
                        static_cast<double>(dbg.in_peak[1]), static_cast<double>(dbg.out_peak));
        } else if (strncmp(args, "aec pos ", 8) == 0) {
          float distance_m = 0.f;
          float azimuth_deg = 0.f;
          float elevation_deg = 0.f;
          if (sscanf(args + 8, "%f %f %f", &distance_m, &azimuth_deg, &elevation_deg) == 3) {
            pm_face_settings_aec_set_position(distance_m, azimuth_deg, elevation_deg);
            g_clock_repaint_pending = true;
            Serial.printf("qa: aec pos %.2fm az=%.1f el=%.1f\n", static_cast<double>(distance_m),
                          static_cast<double>(azimuth_deg), static_cast<double>(elevation_deg));
          } else {
            Serial.println("qa: usage: qa aec pos <distance_m> <az_deg> <el_deg>");
          }
        } else if (strcmp(args, "mic") == 0) {
          qa_mic_probe();
        } else if (strcmp(args, "note") == 0) {
          qa_note_capture();
        } else if (strcmp(args, "pcm") == 0 || strncmp(args, "pcm ", 4) == 0) {
          uint32_t duration_ms = 3000u;
          if (strncmp(args, "pcm ", 4) == 0) {
            const long parsed = strtol(args + 4, nullptr, 10);
            if (parsed > 0) {
              duration_ms = static_cast<uint32_t>(parsed);
            }
          }
          qa_pcm_dump(duration_ms);
        } else if (strcmp(args, "stt tts") == 0 || strcmp(args, "tts stt") == 0) {
          qa_stt_tts_loop();
        } else if (strcmp(args, "time") == 0) {
          print_time_status("qa time");
        } else if (strcmp(args, "briefing") == 0 || strcmp(args, "brief") == 0) {
          Serial.printf("qa: briefing %s\n", home_begin_daily_briefing() ? "started" : "blocked");
        } else if (strcmp(args, "tone") == 0) {
          Serial.printf("qa: tone %s\n", pm_speaker_play_tone_begin(528.f, 1200u) ? "started" : "failed");
        } else if (strcmp(args, "bowl") == 0) {
          Serial.printf("qa: bowl %s\n", pm_speaker_bowl_voice_test(320.f, 1800u) ? "done" : "failed");
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
          Serial.println("qa: 8 settings");
          Serial.println("qa: 9 synastry");
          Serial.println("qa: 10 spectrum");
          Serial.println("qa: 11 chakra");
          Serial.println("qa: 12 bowl");
          Serial.println("qa: 13 rocket");
          Serial.println("qa: 14 radar");
          Serial.println("qa: 15 faculty");
          Serial.println("qa: 16 weather");
          Serial.println("qa: 17 quotes");
          Serial.println("qa: 18 transits");
          Serial.println("qa: 19 tarot");
          Serial.println("qa: 20 notes");
          Serial.println("qa: 21 ocarina");
          Serial.println("qa: 22 bongo");
          Serial.println("qa: 23 piano");
          Serial.println("qa: 24 level");
          Serial.println("qa: 25 tuning");
          Serial.println("qa: 26 pandrum");
          Serial.println("qa: 27 alethiometer");
          Serial.println("qa: 28 runes");
          Serial.println("qa: 29 cycle");
        } else if (strncmp(args, "tour", 4) == 0 && (args[4] == '\0' || args[4] == ' ')) {
          handle_tour_command(args + 4);
        } else if (!pm_qa_inject_command(args)) {
          Serial.println("qa: usage: status | heap | audio | mic | note | pcm [ms] | stt tts | time | briefing | tone | bowl | faces | tour [narrate|tts] [dwell_ms] | tour stop | inject …");
        }
      } else if (strncmp(line, "face ", 5) == 0) {
        if (s_face_tour_auto_advance) {
          s_face_tour_active = false;
          s_face_tour_auto_advance = false;
        }
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
          Serial.printf("face: usage: face <0-%d|name>\n", static_cast<int>(ClockFace::kNumFaces) - 1);
        }
      } else if (strncmp(line, "settings ", 9) == 0) {
        const char *p = line + 9;
        while (*p == ' ') {
          ++p;
        }
        SettingsPage page = SettingsPage::WiFi;
        bool ok = true;
        if (strcmp(p, "wifi") == 0) {
          page = SettingsPage::WiFi;
        } else if (strcmp(p, "castalia") == 0) {
          page = SettingsPage::Castalia;
        } else if (strcmp(p, "tour") == 0) {
          page = SettingsPage::Tour;
        } else if (strcmp(p, "aec") == 0 || strcmp(p, "echo") == 0) {
          page = SettingsPage::Aec;
        } else {
          ok = false;
        }
        if (ok) {
          if (s_face_tour_auto_advance) {
            s_face_tour_active = false;
            s_face_tour_auto_advance = false;
          }
          if (page == SettingsPage::Aec) {
            gesture_end_voice_ui();
          }
          pm_faces_set(ClockFace::Settings);
          pm_settings_set_page(page);
          g_clock_repaint_pending = true;
          Serial.printf("settings: %s\n", pm_settings_page_name(page));
        } else {
          Serial.println("settings: usage: settings wifi|castalia|tour|aec");
        }
      } else if (strncmp(line, "tour", 4) == 0 && (line[4] == '\0' || line[4] == ' ')) {
        handle_tour_command(line + 4);
      } else if (strcmp(line, "time") == 0) {
        print_time_status("time");
      } else if (strcmp(line, "ntp") == 0 || strcmp(line, "time sync") == 0) {
        if (!pm_wifi_connected()) {
          Serial.println("ntp: no wifi");
        } else {
          pm_ntp_sync_blocking();
          print_time_status("ntp");
        }
      } else if (strcmp(line, "bootloader") == 0 || strcmp(line, "download") == 0 || strcmp(line, "flash") == 0) {
        enter_rom_bootloader_from_serial(false);
      } else if (strcmp(line, "dfu") == 0 || strcmp(line, "bootloader dfu") == 0) {
        enter_rom_bootloader_from_serial(true);
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
      } else if (strcmp(line, "faculty list") == 0) {
        pm_faculty_ensure_demo_seed();
        Serial.println("faculty:");
        PmFacultyProfile active = {};
        const bool have_active = pm_faculty_active(&active);
        for (int i = 0; i < kPmFacultySlots; ++i) {
          PmFacultyProfile f = {};
          if (pm_faculty_get_slot(i, &f)) {
            Serial.printf("  %d%s: %s (%s)\n", i,
                          (have_active && strcmp(active.slug, f.slug) == 0) ? "*" : "", f.name, f.slug);
          }
        }
      } else if (strcmp(line, "faculty next") == 0 || strcmp(line, "faculty prev") == 0) {
        PmFacultyProfile f = {};
        const int delta = strcmp(line, "faculty next") == 0 ? 1 : -1;
        if (pm_faculty_cycle_active(delta, &f)) {
          Serial.printf("faculty: %s (%s)\n", f.name, f.slug);
          (void)pm_faculty_tick_bust_fetch();
        } else {
          Serial.println("faculty: no recent faculty");
        }
        g_clock_repaint_pending = true;
      } else if (strncmp(line, "faculty use ", 12) == 0) {
        const char *p = line + 12;
        while (*p == ' ') {
          ++p;
        }
        if (pm_faculty_set_active_slug(p, nullptr)) {
          PmFacultyProfile f = {};
          (void)pm_faculty_active(&f);
          Serial.printf("faculty: active %s (%s)\n", f.name, f.slug);
          (void)pm_faculty_tick_bust_fetch();
        } else {
          Serial.println("faculty: usage: faculty use <slug>  (e.g. a.einstein or einstein)");
        }
        g_clock_repaint_pending = true;
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

static void log_crash_reset_reason(void) {
  const esp_reset_reason_t r = esp_reset_reason();
  if (r == ESP_RST_PANIC || r == ESP_RST_INT_WDT || r == ESP_RST_TASK_WDT || r == ESP_RST_WDT) {
    pm_log_printf(false, "ASTROLABE_ALERT last_reset=%d", static_cast<int>(r));
    Serial.printf("ASTROLABE_ALERT last_reset=%d\n", static_cast<int>(r));
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  pm_log_begin();
  log_crash_reset_reason();
  if (!voice_prompt_buffers_ensure()) {
    Serial.println("voice: PSRAM prompt buffer allocation failed");
  }

#ifndef ASTROLABE_QEMU
  Wire.begin(IIC_SDA, IIC_SCL);
#endif

#if !defined(ASTROLABE_UAC_DISABLED) && defined(CONFIG_UAC_SPEAKER_CHANNEL_NUM) && CONFIG_UAC_SPEAKER_CHANNEL_NUM > 0
  if (pm_usb_uac_begin()) {
    pm_log_printf(false, "uac: speaker ready");
    Serial.println("USB UAC speaker ready (host output -> ES8311)");
  } else {
    pm_log_printf(false, "uac: init failed");
    Serial.println("USB UAC init failed");
  }
#endif

#ifdef ASTROLABE_QEMU
  pm_gesture_reset();
  pm_display_bind(nullptr);
  ensure_pcm_buffer();
  pm_log_printf(false, "boot: Mynah Astrolabe ready qemu");
  Serial.println("Mynah Astrolabe ready");
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
  pm_chart_profiles_load_family_demo();
  pm_faculty_ensure_demo_seed();
  pm_home_gem_pulse_begin();
  pm_user_begin();

  if (pm_wifi_begin()) {
    pm_ntp_sync_blocking();
    pm_castalia_warmup_after_wifi();
  }
  pm_display_bind(gfx);
  pm_screen_http_begin(gfx);
  pm_faces_draw();

  ensure_pcm_buffer();

  pm_audio_route_begin();

  (void)pm_motion_begin();
  (void)pm_presence_begin();

  pm_log_printf(false, "boot: Mynah Astrolabe ready host=%s mac=%s ip=%s heap=%u largest=%u psram=%u",
                pm_wifi_mdns_name(), pm_wifi_mac_string(), pm_wifi_local_ip(), static_cast<unsigned>(pm_heap_internal_free()),
                static_cast<unsigned>(pm_heap_internal_largest()), static_cast<unsigned>(pm_heap_psram_free()));
  Serial.println("Mynah Astrolabe ready");
  if (pm_tour_mode_enabled() || !tour_played_load()) {
    s_face_tour_first_run = true;
    tour_mode_set_enabled(true);
    face_tour_start(1200u, true, false, false);
  }
#endif
}

void loop() {
#ifndef ASTROLABE_QEMU
  pm_screen_http_loop();
  pm_wifi_poll();
#endif
  const uint32_t now = millis();
  pm_gesture_poll(now);
  global_horizontal_nav_touch_tick();
  pm_presence_tick(now);
  poll_serial_birth_commands();
  face_tour_tick(now);
  face_tour_note_current_face();
  handle_usb_audio_stream_event();
  (void)pm_speaker_poll();
  (void)pm_voice_release_idle_task();
  (void)pm_castalia_release_idle_task();
  (void)pm_rocket_release_idle_task();
  const uint8_t side_ev = pm_side_buttons_poll(now);

  if (g_state == AppState::kClock && pm_faces_current() == ClockFace::TibetanBowl) {
    if (pm_face_tibetan_bowl_touch_tick(now)) {
      g_clock_repaint_pending = true;
    }
  }

  PmGestureEvent ge;
  while (pm_gesture_consume(&ge)) {
    if (ge.kind == PmGestureKind::SwipeLeft || ge.kind == PmGestureKind::SwipeRight) {
      const int delta = ge.kind == PmGestureKind::SwipeLeft ? 1 : -1;
      (void)gesture_cycle_face(delta);
      continue;
    }
    if (g_state == AppState::kClock && pm_faces_is_commonplace_home() &&
        ge.kind == PmGestureKind::Tap) {
      if (home_begin_daily_briefing()) {
        g_gesture_banner[0] = '\0';
        g_clock_repaint_pending = false;
        continue;
      }
    }
    if (g_state == AppState::kClock && pm_faces_is_commonplace_home() &&
        ge.kind == PmGestureKind::SwipeDown) {
      pm_faces_open_settings();
      g_gesture_banner[0] = '\0';
      g_clock_repaint_pending = false;
      if (pm_gfx) {
        pm_faces_draw();
      }
      continue;
    }
    if (g_state == AppState::kClock && pm_faces_current() == ClockFace::Settings) {
      if (ge.kind == PmGestureKind::Tap && pm_settings_page() == SettingsPage::WiFi) {
        pm_face_settings_wifi_mark_reconnecting();
        snprintf(g_gesture_banner, sizeof(g_gesture_banner), "wifi: reconnecting");
        if (pm_gfx) {
          pm_faces_draw();
        }
        const bool ok = pm_face_settings_wifi_tap_reconnect();
        snprintf(g_gesture_banner, sizeof(g_gesture_banner), "wifi: %s", ok ? "connected" : "failed");
        g_clock_repaint_pending = false;
        if (pm_gfx) {
          pm_faces_draw();
        }
        continue;
      }
      if (ge.kind == PmGestureKind::Tap && pm_settings_page() == SettingsPage::Aec) {
        const bool ok = pm_face_settings_aec_tap();
        snprintf(g_gesture_banner, sizeof(g_gesture_banner), "aec: %s", ok ? "tone" : "busy");
        g_clock_repaint_pending = false;
        if (pm_gfx) {
          pm_faces_draw();
        }
        continue;
      }
      if (pm_settings_page() == SettingsPage::Tour) {
        if (ge.kind == PmGestureKind::Tap) {
          const bool enabled = !s_face_tour_active;
          s_face_tour_first_run = false;
          tour_mode_set_enabled(enabled);
          if (enabled) {
            snprintf(g_gesture_banner, sizeof(g_gesture_banner), "tour: mode on");
            face_tour_start(1200u, true, false, false);
          } else {
            face_tour_stop();
            snprintf(g_gesture_banner, sizeof(g_gesture_banner), "tour: mode off");
          }
          continue;
        }
        if (ge.kind == PmGestureKind::LongPress) {
          tour_seen_reset();
          s_face_tour_loaded_idx = -1;
          snprintf(g_gesture_banner, sizeof(g_gesture_banner), "tour: reset seen");
          g_clock_repaint_pending = false;
          if (pm_gfx) {
            pm_faces_draw();
          }
          continue;
        }
        if (ge.kind == PmGestureKind::SwipeUp || ge.kind == PmGestureKind::SwipeDown) {
          PmFacultyProfile faculty = {};
          if (pm_faculty_cycle_active(ge.kind == PmGestureKind::SwipeDown ? 1 : -1, &faculty)) {
            snprintf(g_gesture_banner, sizeof(g_gesture_banner), "guide: %.25s", faculty.name);
            (void)pm_faculty_tick_bust_fetch();
          } else {
            snprintf(g_gesture_banner, sizeof(g_gesture_banner), "guide: no faculty");
          }
          g_clock_repaint_pending = false;
          if (pm_gfx) {
            pm_faces_draw();
          }
          continue;
        }
      }
      if (ge.kind == PmGestureKind::SwipeUp) {
        pm_settings_on_leave();
        pm_faces_set(ClockFace::ClassicAnalog);
        g_gesture_banner[0] = '\0';
        g_clock_repaint_pending = false;
        if (pm_gfx) {
          pm_faces_draw();
        }
        continue;
      }
      if (ge.kind == PmGestureKind::SwipeLeft || ge.kind == PmGestureKind::SwipeRight) {
        const SettingsPage prev = pm_settings_page();
        const int delta = (ge.kind == PmGestureKind::SwipeLeft) ? 1 : -1;
        pm_settings_cycle(delta);
        if (prev != pm_settings_page() && pm_settings_page() == SettingsPage::Castalia) {
          pm_castalia_on_face_enter();
        }
        snprintf(g_gesture_banner, sizeof(g_gesture_banner), "settings: %s",
                 pm_settings_page_name(pm_settings_page()));
        g_clock_repaint_pending = false;
        if (pm_gfx) {
          pm_faces_draw();
        }
        continue;
      }
    }
    if (ge.kind == PmGestureKind::SwipeLeft || ge.kind == PmGestureKind::SwipeRight ||
        (pm_faces_current() == ClockFace::Moon &&
         (ge.kind == PmGestureKind::SwipeUp || ge.kind == PmGestureKind::SwipeDown)) ||
        (pm_faces_current() == ClockFace::Radar &&
         (ge.kind == PmGestureKind::SwipeUp || ge.kind == PmGestureKind::SwipeDown))) {
      if (pm_faces_current() == ClockFace::Settings) {
        continue;
      }
      if (g_state == AppState::kClock && instrument_stack_horizontal_exit(ge.kind)) {
        continue;
      }
      if (g_state == AppState::kClock &&
          (ge.kind == PmGestureKind::SwipeLeft || ge.kind == PmGestureKind::SwipeRight)) {
        if (pm_faces_current() == ClockFace::TibetanBowl &&
            pm_face_tibetan_bowl_consume_rim_swipe_block()) {
          g_clock_repaint_pending = true;
          continue;
        }
      }
      const int delta =
          (ge.kind == PmGestureKind::SwipeLeft || ge.kind == PmGestureKind::SwipeUp) ? 1 : -1;
      (void)gesture_cycle_face(delta);
      continue;
    } else if (g_state == AppState::kClock &&
               pm_audio_route_handle_gesture(ge.kind, pm_faces_current(), g_gesture_banner,
                                             sizeof(g_gesture_banner))) {
      if (pm_faces_current() == ClockFace::Spectrum) {
        pm_audio_analyzer_mic_end();
        (void)pm_audio_analyzer_mic_begin();
      }
      g_clock_repaint_pending = true;
      continue;
    } else if (g_state == AppState::kClock && pm_faces_current() == ClockFace::Spotify &&
               (ge.kind == PmGestureKind::SwipeUp || ge.kind == PmGestureKind::SwipeDown ||
                ge.kind == PmGestureKind::Tap || ge.kind == PmGestureKind::DoubleTap ||
                ge.kind == PmGestureKind::LongPress)) {
      if (pm_face_spotify_on_gesture(ge.kind, ge.x, ge.y, g_gesture_banner,
                                     sizeof(g_gesture_banner))) {
        g_clock_repaint_pending = true;
      }
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
    } else if (g_state == AppState::kClock && pm_faces_current() == ClockFace::Synastry &&
               ge.kind == PmGestureKind::Tap) {
      if (synastry_begin_boot_reading()) {
        g_gesture_banner[0] = '\0';
      }
      g_clock_repaint_pending = true;
      continue;
    } else if (g_state == AppState::kClock && instrument_stack_swipe(ge.kind)) {
      g_clock_repaint_pending = true;
      continue;
    } else if (g_state == AppState::kClock && pm_faces_current() == ClockFace::Spectrum &&
               (ge.kind == PmGestureKind::SwipeUp || ge.kind == PmGestureKind::SwipeDown)) {
      pm_face_spectrum_cycle(ge.kind == PmGestureKind::SwipeUp ? 1 : -1);
      snprintf(g_gesture_banner, sizeof(g_gesture_banner), "viz %s", pm_face_spectrum_mode_label());
      g_clock_repaint_pending = true;
      continue;
    } else if (g_state == AppState::kClock && pm_faces_current() == ClockFace::Chakra &&
               (ge.kind == PmGestureKind::SwipeUp || ge.kind == PmGestureKind::SwipeDown)) {
      const int idx = pm_face_chakra_cycle(ge.kind == PmGestureKind::SwipeUp ? 1 : -1);
      snprintf(g_gesture_banner, sizeof(g_gesture_banner), "chakra %d/7", idx + 1);
      g_clock_repaint_pending = true;
      continue;
    } else if (g_state == AppState::kClock && pm_faces_current() == ClockFace::TibetanBowl &&
               (ge.kind == PmGestureKind::SwipeUp || ge.kind == PmGestureKind::SwipeDown)) {
      const int idx = pm_face_tibetan_bowl_cycle_chakra(ge.kind == PmGestureKind::SwipeUp ? 1 : -1);
      snprintf(g_gesture_banner, sizeof(g_gesture_banner), "bowl %d/7", idx + 1);
      g_clock_repaint_pending = true;
      continue;
    } else if (g_state == AppState::kClock && pm_faces_current() == ClockFace::Cycle &&
               (ge.kind == PmGestureKind::SwipeUp || ge.kind == PmGestureKind::SwipeDown)) {
      const uint8_t len = pm_cycle_adjust_cycle_length_preset(ge.kind == PmGestureKind::SwipeUp ? 1 : -1);
      snprintf(g_gesture_banner, sizeof(g_gesture_banner), "cycle: %u days", len);
      g_clock_repaint_pending = true;
      continue;
    } else if (g_state == AppState::kClock && pm_faces_current() == ClockFace::Chakra &&
               ge.kind == PmGestureKind::Tap) {
      if (pm_face_chakra_strike()) {
        snprintf(g_gesture_banner, sizeof(g_gesture_banner), "chakra strike");
      } else {
        snprintf(g_gesture_banner, sizeof(g_gesture_banner), "tone busy");
      }
      g_clock_repaint_pending = true;
      continue;
    } else if (g_state == AppState::kClock && pm_faces_current() == ClockFace::CalciferCountdown &&
               ge.kind == PmGestureKind::Tap) {
      if (pm_face_calcifer_tap(ge.x, ge.y, g_gesture_banner, sizeof(g_gesture_banner))) {
        g_clock_repaint_pending = true;
        continue;
      }
    } else if (g_state == AppState::kClock && pm_faces_current() == ClockFace::TibetanBowl &&
               ge.kind == PmGestureKind::Tap) {
      pm_face_tibetan_bowl_touch_tick(now);
      g_clock_repaint_pending = true;
      continue;
    } else if (g_state == AppState::kClock && pm_faces_current() == ClockFace::Cycle &&
               ge.kind == PmGestureKind::Tap) {
      struct tm local = {};
      if (pm_time_valid()) {
        pm_time_local(&local);
      }
      if (pm_cycle_log_period_started_today(pm_time_valid() ? &local : nullptr)) {
        pm_face_cycle_flash_confirm(now + 1200u);
        pm_face_cycle_on_period_logged();
        pm_face_cycle_schedule_full_ring(now + 1300u);
        snprintf(g_gesture_banner, sizeof(g_gesture_banner), "cycle: day 1");
      } else {
        snprintf(g_gesture_banner, sizeof(g_gesture_banner), "cycle: need time");
      }
      g_clock_repaint_pending = true;
      continue;
    } else if (g_state == AppState::kClock && pm_faces_current() == ClockFace::Cycle &&
               ge.kind == PmGestureKind::LongPress) {
      pm_cycle_clear();
      snprintf(g_gesture_banner, sizeof(g_gesture_banner), "cycle: cleared");
      g_clock_repaint_pending = true;
      continue;
    } else if (g_state == AppState::kClock && pm_faces_current() == ClockFace::Ocarina &&
               ge.kind == PmGestureKind::Tap) {
      if (pm_face_ocarina_play_at(ge.x, ge.y)) {
        snprintf(g_gesture_banner, sizeof(g_gesture_banner), "ocarina: note %d",
                 pm_face_ocarina_note_index() + 1);
      } else {
        snprintf(g_gesture_banner, sizeof(g_gesture_banner), "ocarina: busy");
      }
      g_clock_repaint_pending = true;
      continue;
    } else if (g_state == AppState::kClock && pm_faces_current() == ClockFace::Bongo &&
               ge.kind == PmGestureKind::Tap) {
      if (pm_face_bongo_play_at(ge.x, ge.y)) {
        snprintf(g_gesture_banner, sizeof(g_gesture_banner), "bongo: %.0f Hz",
                 static_cast<double>(pm_face_bongo_last_hz()));
      } else {
        snprintf(g_gesture_banner, sizeof(g_gesture_banner), "bongo: busy");
      }
      g_clock_repaint_pending = true;
      continue;
    } else if (g_state == AppState::kClock && pm_faces_current() == ClockFace::Piano &&
               ge.kind == PmGestureKind::Tap) {
      if (pm_face_piano_play_at(ge.x, ge.y)) {
        snprintf(g_gesture_banner, sizeof(g_gesture_banner), "piano: %s", pm_face_piano_note_label());
      } else {
        snprintf(g_gesture_banner, sizeof(g_gesture_banner), "piano: key?");
      }
      g_clock_repaint_pending = true;
      continue;
    } else if (g_state == AppState::kClock && pm_faces_current() == ClockFace::PanDrum &&
               ge.kind == PmGestureKind::Tap) {
      if (pm_face_pandrum_play_at(ge.x, ge.y)) {
        snprintf(g_gesture_banner, sizeof(g_gesture_banner), "pandrum: %s",
                 pm_face_pandrum_note_label());
      } else {
        snprintf(g_gesture_banner, sizeof(g_gesture_banner), "pandrum: field?");
      }
      g_clock_repaint_pending = true;
      continue;
    } else if (g_state == AppState::kClock && pm_faces_current() == ClockFace::Faculty &&
               (ge.kind == PmGestureKind::SwipeUp || ge.kind == PmGestureKind::SwipeDown)) {
      PmFacultyProfile faculty = {};
      if (pm_faculty_cycle_active(ge.kind == PmGestureKind::SwipeDown ? 1 : -1, &faculty)) {
        snprintf(g_gesture_banner, sizeof(g_gesture_banner), "faculty: %.25s", faculty.name);
        (void)pm_faculty_tick_bust_fetch();
      } else {
        snprintf(g_gesture_banner, sizeof(g_gesture_banner), "faculty: no recents");
      }
      g_clock_repaint_pending = true;
      continue;
    } else if (g_state == AppState::kClock && pm_faces_current() == ClockFace::Moon &&
               ge.kind == PmGestureKind::Tap) {
      if (moon_begin_fortune()) {
        g_gesture_banner[0] = '\0';
      }
      g_clock_repaint_pending = true;
    } else if (g_state == AppState::kClock && pm_faces_current() == ClockFace::Runes &&
               ge.kind == PmGestureKind::Tap) {
      if (runes_begin_fortune()) {
        g_gesture_banner[0] = '\0';
      }
      g_clock_repaint_pending = true;
    } else if (g_state == AppState::kClock && pm_faces_current() == ClockFace::Tarot &&
               (ge.kind == PmGestureKind::SwipeUp || ge.kind == PmGestureKind::SwipeDown)) {
      pm_face_tarot_cycle(ge.kind == PmGestureKind::SwipeUp ? 1 : -1);
      struct tm local = {};
      const bool valid = pm_time_valid();
      if (valid) {
        pm_time_local(&local);
      }
      const int tarot_idx = pm_face_tarot_index(&local, valid);
      snprintf(g_gesture_banner, sizeof(g_gesture_banner), "tarot %02d %.24s", tarot_idx,
               pm_face_tarot_title(tarot_idx));
      g_clock_repaint_pending = true;
      continue;
    } else if (g_state == AppState::kClock && pm_faces_current() == ClockFace::Tarot &&
               ge.kind == PmGestureKind::Tap) {
      pm_face_tarot_reset_daily();
      snprintf(g_gesture_banner, sizeof(g_gesture_banner), "tarot: daily");
      g_clock_repaint_pending = true;
      continue;
    } else if (g_state == AppState::kClock && pm_faces_current() == ClockFace::Rocket &&
               (ge.kind == PmGestureKind::SwipeUp || ge.kind == PmGestureKind::SwipeDown)) {
      if (pm_face_rocket_cycle_launch(ge.kind == PmGestureKind::SwipeUp ? 1 : -1)) {
        snprintf(g_gesture_banner, sizeof(g_gesture_banner), "launch %d/%d",
                 pm_face_rocket_selected_index() + 1, g_rocket_ui.count);
      } else {
        snprintf(g_gesture_banner, sizeof(g_gesture_banner), "launch: no data");
      }
      g_clock_repaint_pending = true;
      continue;
    } else if (g_state == AppState::kClock && pm_faces_current() == ClockFace::Rocket &&
               ge.kind == PmGestureKind::Tap) {
      (void)pm_face_rocket_tap(ge.x, ge.y, g_gesture_banner, sizeof(g_gesture_banner));
      g_clock_repaint_pending = true;
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
  if (g_state == AppState::kClock && (side_ev & PM_SIDE_BTN_BOOT) && pm_faces_voice_input_enabled() &&
      pm_faces_current() != ClockFace::Astrology && pm_faces_current() != ClockFace::Synastry) {
    if (voice_last_play_begin()) {
      /* BOOT replay last TTS */
    } else {
      if (pm_wifi_connected() && (now - s_last_clock_boot_brief_ms >= 3500u)) {
        s_last_clock_boot_brief_ms = now;
        (void)face_voice_begin_current();
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
      static uint32_t s_gem_pulse_last_ms = 0;

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
        if (pm_faces_castalia_active()) {
          pm_castalia_on_face_enter();
          g_clock_repaint_pending = true;
        }
        if (pm_faces_current() == ClockFace::Settings && pm_settings_page() == SettingsPage::WiFi) {
          g_clock_repaint_pending = true;
        }
        if (pm_faces_current() == ClockFace::Spectrum) {
          s_ptt_press_ms = 0;
          g_clock_repaint_pending = true;
        }
        if (pm_faces_current() == ClockFace::Radar) {
          g_clock_repaint_pending = true;
        }
        if (pm_faces_current() == ClockFace::Level) {
          g_clock_repaint_pending = true;
        }
        if (pm_faces_current() == ClockFace::Alethiometer) {
          g_clock_repaint_pending = true;
        }
        if (pm_faces_current() == ClockFace::Faculty) {
          g_clock_repaint_pending = true;
        }
        s_prev_dial_face = pm_faces_current();
      }

      static uint32_t s_last_aec_settings_ms = 0;
      const bool aec_settings_anim =
          pm_faces_current() == ClockFace::Settings && pm_settings_page() == SettingsPage::Aec &&
          g_state == AppState::kClock && (now - s_last_aec_settings_ms >= 50u);
      if (aec_settings_anim) {
        s_last_aec_settings_ms = now;
        pm_face_settings_aec_tick();
      }

      if (pm_faces_castalia_active() && wifi && pm_castalia_tick_pair_start()) {
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

      static ClockFace s_prev_clock_face = ClockFace::ClassicAnalog;
      if (pm_faces_current() == ClockFace::Spotify && s_prev_clock_face != ClockFace::Spotify) {
        pm_face_spotify_reset();
      }
      s_prev_clock_face = pm_faces_current();
      if (pm_faces_current() != ClockFace::Spotify) {
        s_spotify_have_data = false;
      }
      if (pm_faces_current() == ClockFace::Spotify) {
        if (pm_faces_tick(now)) {
          g_clock_repaint_pending = true;
        }
      }
      if (!s_face_tour_active && pm_faces_current() != ClockFace::CalciferCountdown) {
        s_calcifer_have_data = false;
      }
      if (!s_face_tour_active && pm_faces_current() != ClockFace::Weather) {
        s_weather_have_data = false;
      }
      if (!s_face_tour_active && pm_faces_current() != ClockFace::Rocket) {
        s_rocket_have_data = false;
        pm_face_rocket_set_stream_qr_visible(false);
        pm_rocket_pad_image_release();
      }

      const bool spotify_stale =
          pm_faces_current() == ClockFace::Spotify && pm_wifi_connected() && s_spotify_have_data &&
          (now - s_last_spotify_poll_ms >= MYNAH_SPOTIFY_POLL_MS);

      const bool calcifer_stale =
          pm_faces_current() == ClockFace::CalciferCountdown && pm_wifi_connected() && valid &&
          (!s_calcifer_have_data || (now - s_last_calcifer_poll_ms >= MYNAH_CALCIFER_POLL_MS));

      const bool weather_stale =
          pm_faces_current() == ClockFace::Weather &&
          (!s_weather_have_data || (now - s_last_weather_poll_ms >= MYNAH_WEATHER_POLL_MS));
      const bool quotes_stale =
          !s_quotes_have_data || (now - s_last_quotes_poll_ms >= MYNAH_QUOTES_POLL_MS);
      const bool quotes_face_stale = pm_faces_current() == ClockFace::Quotes && quotes_stale;
      const bool quotes_preload_due =
          pm_wifi_connected() && quotes_stale && sec_tick && pm_faces_current() != ClockFace::Quotes &&
          pm_faces_current() != ClockFace::Rocket && pm_faces_current() != ClockFace::Radar;
      const bool rocket_stale =
          pm_faces_current() == ClockFace::Rocket && pm_wifi_connected() && valid &&
          (!s_rocket_have_data || (now - s_last_rocket_poll_ms >= MYNAH_ROCKET_POLL_MS));
      if (pm_rocket_consume_fetch(&g_rocket_ui)) {
        s_rocket_have_data = true;
        g_clock_repaint_pending = true;
      }

      static time_t s_prev_astro_epoch_min = -1;
      const time_t epoch_min_bucket = valid ? (epoch / 60) : -1;
      const bool astro_repaint =
          (pm_faces_current() == ClockFace::Astrology || pm_faces_current() == ClockFace::LiveTransits) &&
          valid && epoch_min_bucket != s_prev_astro_epoch_min;

      const bool face_anim = pm_faces_current() != ClockFace::Spotify && pm_faces_tick(now);
      const bool home_gem_breath =
          pm_faces_current() == ClockFace::ClassicAnalog && pm_home_gem_pulse_enabled();
      const bool sec_tick_paint =
          sec_tick && !pm_faces_castalia_active() && pm_faces_current() != ClockFace::Settings &&
          pm_faces_current() != ClockFace::CalciferCountdown &&
          pm_faces_current() != ClockFace::Synastry && pm_faces_current() != ClockFace::Spectrum &&
          pm_faces_current() != ClockFace::Chakra && pm_faces_current() != ClockFace::TibetanBowl &&
          pm_faces_current() != ClockFace::Rocket && pm_faces_current() != ClockFace::Radar &&
          pm_faces_current() != ClockFace::Faculty && pm_faces_current() != ClockFace::Quotes &&
          pm_faces_current() != ClockFace::LiveTransits && pm_faces_current() != ClockFace::Tarot &&
          pm_faces_current() != ClockFace::Ocarina && pm_faces_current() != ClockFace::Bongo &&
          pm_faces_current() != ClockFace::Piano && pm_faces_current() != ClockFace::Level &&
          pm_faces_current() != ClockFace::PanDrum && pm_faces_current() != ClockFace::Alethiometer &&
          pm_faces_current() != ClockFace::Runes && pm_faces_current() != ClockFace::Cycle &&
          !home_gem_breath;
      const bool calcifer_sec =
          pm_faces_current() == ClockFace::CalciferCountdown && valid && sec_tick;
      const bool rocket_sec = pm_faces_current() == ClockFace::Rocket && valid && sec_tick;
      const bool rocket_anim =
          pm_faces_current() == ClockFace::Rocket && valid && pm_face_rocket_needs_repaint(now);
      static uint32_t s_last_wifi_settings_graph_ms = 0;
      bool wifi_settings_graph = false;
      if (pm_faces_current() == ClockFace::Settings && pm_settings_page() == SettingsPage::WiFi &&
          now - s_last_wifi_settings_graph_ms >= 1000u) {
        s_last_wifi_settings_graph_ms = now;
        wifi_settings_graph = true;
      }
#if MYNAH_HUE_HOME_ONLY
      bool gem_pulse_paint = false;
      if (home_gem_breath && !pm_gesture_touch_down()) {
        const uint32_t pulse_iv = pm_home_gem_pulse_repaint_interval_ms();
        if (now - s_gem_pulse_last_ms >= pulse_iv) {
          s_gem_pulse_last_ms = now;
          gem_pulse_paint = true;
        }
      }
#else
      const bool gem_pulse_paint = false;
#endif
      const bool non_gem_paint = !s_clock_paint_inited || slow_no_time || banner_chg || wifi_chg ||
                                 g_clock_repaint_pending || local_hm_chg || spotify_stale || calcifer_stale ||
                                 weather_stale || quotes_face_stale || quotes_preload_due || rocket_stale || sec_tick_paint || calcifer_sec || rocket_sec || rocket_anim ||
                                 astro_repaint || face_anim || wifi_settings_graph || aec_settings_anim;
#if MYNAH_HUE_HOME_ONLY
      const bool gem_only_paint = gem_pulse_paint && s_clock_paint_inited && !non_gem_paint;
      const bool full_paint = non_gem_paint || gem_pulse_paint;
#else
      const bool gem_only_paint = false;
      const bool full_paint = non_gem_paint || gem_pulse_paint;
#endif

      if (full_paint) {
        s_clock_paint_inited = true;
        g_clock_repaint_pending = false;
        if (valid) {
          s_prev_epoch = epoch;
        }
        if ((pm_faces_current() == ClockFace::Astrology || pm_faces_current() == ClockFace::LiveTransits) && valid) {
          s_prev_astro_epoch_min = epoch_min_bucket;
        }
        if (banner_chg) {
          strncpy(s_prev_banner, g_gesture_banner, sizeof(s_prev_banner));
          s_prev_banner[sizeof(s_prev_banner) - 1] = '\0';
        }
        s_prev_wifi = wifi;
        if (pm_faces_current() == ClockFace::Spotify && pm_wifi_connected()) {
          if (!s_spotify_have_data || spotify_stale) {
            const bool refreshed = pm_spotify_refresh(&g_spotify_ui);
            pm_face_spotify_sync_hub(&g_spotify_ui, refreshed);
            s_last_spotify_poll_ms = now;
            s_spotify_have_data = true;
          }
        }
        if (pm_faces_current() == ClockFace::CalciferCountdown && pm_wifi_connected() && valid) {
          if (!s_calcifer_have_data || calcifer_stale) {
            if (ESP.getFreeHeap() >= MYNAH_FACE_FETCH_MIN_HEAP) {
              (void)pm_calcifer_fetch(&g_calcifer_ui, epoch);
            }
            s_last_calcifer_poll_ms = now;
            s_calcifer_have_data = true;
          }
        }
        if (pm_faces_current() == ClockFace::Weather) {
          if (!s_weather_have_data || weather_stale) {
            if (ESP.getFreeHeap() >= MYNAH_FACE_FETCH_MIN_HEAP) {
              (void)pm_weather_fetch(&g_weather_ui);
            } else {
              pm_weather_fill_demo(&g_weather_ui, valid ? tm_now.tm_hour : 12);
            }
            s_last_weather_poll_ms = now;
            s_weather_have_data = true;
          }
        }
        if (quotes_preload_due &&
            pm_faces_current() != ClockFace::Radar && ESP.getFreeHeap() >= MYNAH_FACE_FETCH_MIN_HEAP) {
          (void)pm_quotes_fetch(&g_quotes_ui);
          s_last_quotes_poll_ms = now;
          s_quotes_have_data = true;
        }
        if (g_state == AppState::kClock &&
            (pm_faces_current() == ClockFace::Quotes || pm_faces_current() == ClockFace::Faculty)) {
          if (s_quotes_have_data && g_quotes_ui.ok && g_quotes_ui.faculty_slug[0]) {
            (void)pm_faculty_preload_busts(g_quotes_ui.faculty_slug);
          } else {
            (void)pm_faculty_preload_busts(nullptr);
          }
        }

        if (pm_faces_current() == ClockFace::Quotes) {
          if (!s_quotes_have_data || quotes_stale) {
            if (ESP.getFreeHeap() >= MYNAH_FACE_FETCH_MIN_HEAP) {
              (void)pm_quotes_fetch(&g_quotes_ui);
            } else {
              pm_quotes_fill_demo(&g_quotes_ui);
            }
            if (g_quotes_ui.ok && g_quotes_ui.faculty_slug[0]) {
              (void)pm_faculty_request_bust(g_quotes_ui.faculty_slug);
            }
            s_last_quotes_poll_ms = now;
            s_quotes_have_data = true;
          }
          if (g_quotes_ui.ok && g_quotes_ui.faculty_slug[0] && pm_faculty_bust_status() != PmFacultyBustStatus::Working &&
              (pm_faculty_bust_size() == 0 || strcmp(pm_faculty_bust_slug(), g_quotes_ui.faculty_slug) != 0)) {
            (void)pm_faculty_request_bust(g_quotes_ui.faculty_slug);
          }
        }
        if (pm_faces_current() == ClockFace::Rocket && pm_wifi_connected() && valid) {
          if (!s_rocket_have_data || rocket_stale) {
            pm_faculty_release_bust_cache();
            (void)pm_speaker_release_idle_task();
            (void)pm_voice_release_idle_task();
            pm_face_rocket_reset_selection();
            (void)pm_rocket_request_fetch();
            s_last_rocket_poll_ms = now;
            s_rocket_have_data = true;
          }
        }
        if (pm_gfx) {
          if (gem_only_paint) {
            pm_faces_draw_home_gem_pulse();
          } else {
            pm_faces_draw();
          }
          face_tour_draw_guide_overlay();
          if (s_face_tour_active) {
            gfx->flush();
          }
        }
        if (!valid) {
          s_last_no_time_redraw = now;
        }
      }

      if (kAutoDailyBriefingEnabled && wifi && valid && !s_face_tour_active && s_clock_paint_inited && !s_daily_brief_auto_armed &&
          pm_daily_briefing_should_auto_play(&tm_now)) {
        s_daily_brief_auto_armed = true;
        if (home_begin_daily_briefing()) {
          pm_daily_briefing_mark_played(&tm_now);
        }
      }

      if (pm_faces_castalia_active() && wifi && !full_paint && pm_castalia_tick_poll()) {
        g_clock_repaint_pending = true;
      }

      if (ptt_armed && g_pcm && pm_faces_voice_input_enabled()) {
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
          g_commonplace_note_face = false;
          g_astro_voice_active = true;
          g_astro_voice_pcm = true;
          g_alethiometer_voice_active = false;
          g_alethiometer_voice_pcm = false;
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
          g_commonplace_note_face = false;
          g_astro_voice_active = false;
          g_astro_voice_pcm = false;
          g_synastry_voice_active = true;
          g_synastry_voice_pcm = true;
          g_alethiometer_voice_active = false;
          g_alethiometer_voice_pcm = false;
          g_moon_voice_pcm = false;
          s_synastry_play_armed = false;
        } else if (pm_faces_current() == ClockFace::Moon) {
          if (!pm_wifi_connected() || !pm_time_valid()) {
            g_clock_repaint_pending = true;
            break;
          }
          g_commonplace_journal = false;
          g_commonplace_note_face = false;
          g_astro_voice_active = true;
          g_astro_voice_pcm = true;
          g_synastry_voice_active = false;
          g_synastry_voice_pcm = false;
          g_alethiometer_voice_active = false;
          g_alethiometer_voice_pcm = false;
          g_moon_voice_pcm = true;
          s_astro_voice_armed = false;
          s_astro_play_armed = false;
        } else if (pm_faces_current() == ClockFace::Alethiometer) {
          if (!pm_wifi_connected()) {
            snprintf(g_gesture_banner, sizeof(g_gesture_banner), "aleth: need WiFi");
            g_clock_repaint_pending = true;
            break;
          }
          g_commonplace_journal = false;
          g_commonplace_note_face = false;
          g_astro_voice_active = false;
          g_astro_voice_pcm = false;
          g_synastry_voice_active = false;
          g_synastry_voice_pcm = false;
          g_alethiometer_voice_active = true;
          g_alethiometer_voice_pcm = true;
          g_moon_voice_pcm = false;
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
          g_commonplace_note_face = false;
          g_astro_voice_active = false;
          g_astro_voice_pcm = false;
          g_synastry_voice_active = false;
          g_synastry_voice_pcm = false;
          g_alethiometer_voice_active = false;
          g_alethiometer_voice_pcm = false;
          g_moon_voice_pcm = false;
        } else if (pm_faces_current() == ClockFace::Notes) {
          g_commonplace_journal = true;
          g_commonplace_note_face = true;
          g_astro_voice_active = false;
          g_astro_voice_pcm = false;
          g_synastry_voice_active = false;
          g_synastry_voice_pcm = false;
          g_alethiometer_voice_active = false;
          g_alethiometer_voice_pcm = false;
          g_moon_voice_pcm = false;
        } else {
          g_commonplace_journal = false;
          g_commonplace_note_face = false;
          g_astro_voice_active = false;
          g_astro_voice_pcm = false;
          g_synastry_voice_active = false;
          g_synastry_voice_pcm = false;
          g_alethiometer_voice_active = false;
          g_alethiometer_voice_pcm = false;
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
      const size_t ns = pm_mic_frame_samples();
      const size_t frame_bytes = ns * sizeof(int16_t);
      int16_t raw[512 * 4];
      int16_t frame[512];
      if (ns > sizeof(frame) / sizeof(frame[0]) ||
          ns * static_cast<size_t>(pm_mic_i2s_channels()) > sizeof(raw) / sizeof(raw[0]) ||
          frame_bytes == 0) {
        pm_mic_stop();
        s_rec_mic_on = false;
        recording_progress_end();
        g_astro_voice_active = false;
        g_synastry_voice_active = false;
        g_alethiometer_voice_active = false;
        g_alethiometer_voice_pcm = false;
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
        if (pm_mic_read_frame(raw, ns, &br) && br > 0 &&
            g_pcm_len + frame_bytes <= MYNAH_VOICE_MAX_PCM_BYTES) {
          const int nch = pm_mic_i2s_channels();
          const int capture_ch = pm_mic_capture_channels();
          int best_ch = 0;
          uint32_t best_peak = 0;
          for (int ch = 0; ch < capture_ch; ++ch) {
            const int slot = pm_mic_capture_slot(ch);
            if (slot < 0 || slot >= nch) {
              continue;
            }
            uint32_t peak = 0;
            for (size_t i = 0; i < ns; ++i) {
              const int32_t sample = raw[i * static_cast<size_t>(nch) + static_cast<size_t>(slot)];
              const uint32_t a = static_cast<uint32_t>(sample < 0 ? -sample : sample);
              if (a > peak) {
                peak = a;
              }
            }
            if (peak > best_peak) {
              best_peak = peak;
              best_ch = ch;
            }
          }
          const int analyzer_channels =
              capture_ch < PM_AUDIO_ANALYZER_IN_CHANNELS ? capture_ch : PM_AUDIO_ANALYZER_IN_CHANNELS;
          for (int ch = 0; ch < analyzer_channels; ++ch) {
            pm_mic_pick_capture_channel(raw, ns, ch, frame);
            pm_audio_analyzer_feed_in_channel(ch, frame, ns);
          }
          if (!pm_mic_mix_capture_channels(raw, ns, frame)) {
            pm_mic_pick_capture_channel(raw, ns, best_ch, frame);
          }
          (void)pm_mic_repair_sparse_mono(frame, ns);
          pm_audio_analyzer_cancel_echo_channel(0, frame, ns);
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
      } else if (g_alethiometer_voice_active) {
        pm_face_alethiometer_draw_voice_screen("listening");
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
        if (g_commonplace_note_face) {
          const float progress =
              static_cast<float>(g_pcm_len) / static_cast<float>(MYNAH_VOICE_MAX_PCM_BYTES);
          const uint32_t elapsed = s_thinking_t0_ms == 0 ? 0u : (now - s_thinking_t0_ms);
          pm_face_notes_draw_recording(progress, elapsed);
          pm_gfx->flush();
        } else {
          pm_face_draw_voice_wave_screen(false, now, "journal");
        }
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
        g_alethiometer_voice_active = false;
        g_commonplace_journal = false;
        g_commonplace_note_face = false;
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

      if (g_voice_play_reset) {
        g_voice_play_reset = false;
        s_voice_job_armed = false;
        s_commonplace_armed = false;
        s_voice_wait_t0 = 0;
      }

      if (g_commonplace_journal) {
        if (!s_commonplace_armed) {
          s_voice_wait_t0 = now;
          if (g_commonplace_note_face && (!pm_wifi_connected() || !pm_castalia_has_session())) {
            if (!pm_commonplace_save_offline_note(g_pcm, g_pcm_len)) {
              snprintf(g_gesture_banner, sizeof(g_gesture_banner), "note: %s",
                       pm_commonplace_last_error());
              g_commonplace_journal = false;
              g_commonplace_note_face = false;
              g_state = AppState::kClock;
              g_clock_repaint_pending = true;
              break;
            }
            snprintf(g_gesture_banner, sizeof(g_gesture_banner), "note queued: %u",
                     static_cast<unsigned>(pm_commonplace_offline_note_count()));
            g_commonplace_journal = false;
            g_commonplace_note_face = false;
            g_state = AppState::kClock;
            g_clock_repaint_pending = true;
            break;
          }
          if (!pm_commonplace_begin_pcm_journal(g_pcm, g_pcm_len)) {
            snprintf(g_gesture_banner, sizeof(g_gesture_banner), "journal: busy");
            g_commonplace_journal = false;
            g_commonplace_note_face = false;
            g_state = AppState::kClock;
            g_clock_repaint_pending = true;
            break;
          }
          thinking_progress_begin(90000u);
          s_commonplace_armed = true;
        }
        pm_faces_draw(thinking_progress_now());
        pm_face_draw_centered_line(g_commonplace_note_face ? "saving note" : "saving journal", 12,
                                   gfx->color565(200, 210, 240), 1, 1);
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
        g_commonplace_note_face = false;
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
        if (g_daily_briefing) {
          pm_speaker_set_max_play_seconds(600);
          started = pm_voice_begin_daily_briefing(&g_voice_result);
        } else if (g_calcifer_briefing) {
          started = pm_voice_begin_clock_agenda(&g_voice_result);
        } else if (g_text_voice_route == k_tv_moon) {
          started = pm_voice_begin_message(g_moon_voice_msg, g_moon_sys_prompt, &g_voice_result);
        } else if (g_text_voice_route == k_tv_runes) {
          started = pm_voice_begin_message(g_runes_voice_msg, g_runes_sys_prompt, &g_voice_result);
        } else if (g_text_voice_route == k_tv_synastry) {
          started = pm_voice_begin_message(kSynastryBootUserMsg, g_synastry_sys_prompt, &g_voice_result);
        } else if (g_text_voice_route == k_tv_face) {
          started = pm_voice_begin_message_ex(s_face_tour_voice_msg, s_face_tour_sys_prompt, s_face_voice_face,
                                              s_face_voice_faculty_slug, s_face_voice_faculty_name,
                                              &g_voice_result);
        } else if (g_astro_voice_active && !g_astro_voice_pcm) {
          started = pm_voice_begin_message(kAstroBootUserMsg, g_astrology_sys_prompt, &g_voice_result);
        } else {
          const char *sys = nullptr;
          if (g_moon_voice_pcm) {
            if (!pm_face_moon_build_system_prompt(g_moon_sys_prompt, kMoonSysPromptCap)) {
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
            if (!pm_face_astrology_build_system_prompt(g_astrology_voice_msg, kAstrologyVoiceMsgCap, g_astrology_sys_prompt, kAstrologySysPromptCap)) {
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
            if (!pm_face_synastry_build_system_prompt(g_synastry_voice_msg, kSynastryVoiceMsgCap,
                                                      g_synastry_sys_prompt,
                                                      kSynastrySysPromptCap)) {
              pm_face_synastry_draw_voice_screen("chart data fail");
              delay(1200);
              g_synastry_voice_active = false;
              g_synastry_voice_pcm = false;
              g_state = AppState::kClock;
              g_clock_repaint_pending = true;
              break;
            }
            sys = g_synastry_sys_prompt;
          } else if (g_alethiometer_voice_active) {
            if (!pm_face_alethiometer_build_system_prompt(s_face_tour_sys_prompt, kFaceTourSysPromptCap)) {
              pm_face_alethiometer_draw_voice_screen("contract fail");
              delay(1200);
              g_alethiometer_voice_active = false;
              g_alethiometer_voice_pcm = false;
              g_state = AppState::kClock;
              g_clock_repaint_pending = true;
              break;
            }
            sys = s_face_tour_sys_prompt;
          }
          started = pm_voice_begin_pcm(g_pcm, g_pcm_len, sys, &g_voice_result);
        }
        if (!started) {
          if (g_daily_briefing) {
            gfx->fillScreen(RGB565_BLACK);
            pm_face_draw_centered_line("briefing busy", 220, RGB565_RED, 2, 2);
            gfx->flush();
            delay(1200);
          } else if (g_synastry_voice_active) {
            pm_face_synastry_draw_voice_screen("voice start fail");
          } else if (g_alethiometer_voice_active) {
            pm_face_alethiometer_draw_voice_screen("voice start fail");
          } else if (g_runes_fortune_active) {
            pm_face_runes_draw_voice_screen("voice start fail", -1.f);
          } else if (g_astro_voice_active) {
            pm_face_astrology_draw_voice_screen("voice start fail", -1, -1, false);
          }
          g_daily_briefing = false;
          pm_speaker_set_max_play_seconds(180);
          g_astro_voice_active = false;
          g_astro_voice_pcm = false;
          g_synastry_voice_active = false;
          g_synastry_voice_pcm = false;
          g_alethiometer_voice_active = false;
          g_alethiometer_voice_pcm = false;
          g_runes_fortune_active = false;
          g_moon_voice_pcm = false;
          g_state = AppState::kClock;
          g_clock_repaint_pending = true;
          break;
        }
        thinking_progress_begin(g_daily_briefing ? 680000u
                                                : ((g_astro_voice_active || g_synastry_voice_active ||
                                                    g_alethiometer_voice_active || g_runes_fortune_active)
                                                       ? 180000u
                                                       : 45000u));
        s_voice_job_armed = true;
      }
      if (g_daily_briefing) {
        if (pm_voice_daily_briefing_streaming_play() && pm_speaker_http_stream_active()) {
          pm_face_home_briefing_draw_speaking(now);
        } else {
          pm_face_home_briefing_draw_thinking(thinking_progress_now());
        }
      } else if (g_synastry_voice_active) {
        pm_face_synastry_draw_voice_screen(nullptr, thinking_progress_now());
      } else if (g_alethiometer_voice_active) {
        pm_face_alethiometer_draw_voice_screen(nullptr, thinking_progress_now());
      } else if (g_astro_voice_active) {
        pm_face_astrology_draw_voice_screen(nullptr, -1, -1, false, thinking_progress_now());
      } else if (g_moon_fortune_active) {
        pm_face_moon_draw_voice_screen(nullptr, thinking_progress_now());
      } else if (g_runes_fortune_active) {
        pm_face_runes_draw_voice_screen(nullptr, thinking_progress_now());
      } else {
        pm_faces_draw(thinking_progress_now());
      }
      const PmVoiceStatus vs = pm_voice_poll();
      if (vs == PmVoiceStatus::Working) {
        const uint32_t voice_wait_ms =
            g_daily_briefing ? 680000u
                             : ((g_moon_fortune_active || g_astro_voice_active || g_synastry_voice_active ||
                                 g_alethiometer_voice_active || g_runes_fortune_active)
                                    ? 620000u
                                    : 100000u);
        if (s_voice_wait_t0 != 0 && (now - s_voice_wait_t0) > voice_wait_ms) {
          Serial.println("astro: voice wait timeout — abort");
          pm_voice_abort();
        } else {
          break;
        }
      }
      if (g_astro_voice_active) {
        Serial.printf("astro: voice done status=%d mp3=%u err=%s\n", static_cast<int>(vs),
                      static_cast<unsigned>(g_voice_result.mp3_len),
                      vs == PmVoiceStatus::DoneOk ? "-" : pm_voice_last_error());
      } else if (g_synastry_voice_active) {
        Serial.printf("synastry: voice done status=%d mp3=%u err=%s\n", static_cast<int>(vs),
                      static_cast<unsigned>(g_voice_result.mp3_len),
                      vs == PmVoiceStatus::DoneOk ? "-" : pm_voice_last_error());
      } else if (g_alethiometer_voice_active) {
        Serial.printf("alethiometer: voice done status=%d mp3=%u err=%s\n", static_cast<int>(vs),
                      static_cast<unsigned>(g_voice_result.mp3_len),
                      vs == PmVoiceStatus::DoneOk ? "-" : pm_voice_last_error());
      }
      s_voice_job_armed = false;
      thinking_progress_end();
      g_voice_use_message = false;
      g_text_voice_route = k_tv_none;
      g_calcifer_briefing = false;
      if (vs != PmVoiceStatus::DoneOk) {
        if (g_daily_briefing) {
          gfx->fillScreen(RGB565_BLACK);
          pm_face_draw_centered_line("briefing failed", 200, RGB565_RED, 2, 2);
          pm_face_draw_centered_line(pm_voice_last_error(), 232, gfx->color565(180, 120, 120), 1, 1);
          gfx->flush();
          delay(1800);
        } else if (g_synastry_voice_active) {
          pm_face_synastry_draw_voice_screen(pm_voice_last_error());
        } else if (g_alethiometer_voice_active) {
          pm_face_alethiometer_draw_voice_screen(pm_voice_last_error());
        } else if (g_astro_voice_active) {
          pm_face_astrology_draw_voice_screen(pm_voice_last_error(), -1, -1, false);
        } else if (g_moon_fortune_active) {
          pm_face_moon_draw_voice_screen(pm_voice_last_error(), -1.f);
          delay(1500);
        } else if (g_runes_fortune_active) {
          pm_face_runes_draw_voice_screen(pm_voice_last_error(), -1.f);
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
        g_alethiometer_voice_active = false;
        g_alethiometer_voice_pcm = false;
        g_moon_fortune_active = false;
        g_runes_fortune_active = false;
        g_daily_briefing = false;
        pm_speaker_set_max_play_seconds(180);
        g_state = AppState::kClock;
        g_clock_repaint_pending = true;
        break;
      }
      const bool voice_audio_buffered = g_voice_result.mp3 && g_voice_result.mp3_len >= 64;
      const bool voice_audio_streamed = g_voice_result.audio_streamed;
      if (!voice_audio_buffered && !voice_audio_streamed) {
        if (g_daily_briefing && pm_voice_daily_briefing_streamed()) {
          /* audio already played during HTTP download */
        } else if (g_daily_briefing) {
          gfx->fillScreen(RGB565_BLACK);
          pm_face_draw_centered_line("briefing: no audio", 220, RGB565_RED, 2, 2);
          gfx->flush();
          delay(1500);
          pm_voice_result_free(&g_voice_result);
          g_daily_briefing = false;
          pm_speaker_set_max_play_seconds(180);
          g_state = AppState::kClock;
          g_clock_repaint_pending = true;
          break;
        }
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
        if (g_alethiometer_voice_active) {
          pm_face_alethiometer_draw_voice_screen("no audio reply");
          delay(1500);
          pm_voice_result_free(&g_voice_result);
          g_alethiometer_voice_active = false;
          g_alethiometer_voice_pcm = false;
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
        if (g_runes_fortune_active) {
          pm_face_runes_draw_voice_screen("no audio reply", -1.f);
          delay(1500);
          pm_voice_result_free(&g_voice_result);
          g_runes_fortune_active = false;
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
      if (g_alethiometer_voice_active) {
        const char *question = g_voice_result.transcript[0] ? g_voice_result.transcript : "";
        const char *reply = g_voice_result.reply[0] ? g_voice_result.reply : question;
        pm_face_alethiometer_seed_from_text(question, reply);
      }
      if (g_voice_result.faculty_slug[0] != '\0') {
        pm_faculty_note_turn(g_voice_result.faculty_slug, g_voice_result.faculty_name,
                             g_voice_result.transcript, g_voice_result.reply);
        if (pm_faces_current() == ClockFace::Faculty) {
          (void)pm_faculty_tick_bust_fetch();
        }
      }
      if (voice_audio_buffered) {
        voice_last_play_save(g_voice_result.mp3, g_voice_result.mp3_len);
      }
      g_astro_voice_pcm = false;
      g_synastry_voice_pcm = false;
      g_alethiometer_voice_pcm = false;
      g_moon_voice_pcm = false;
      g_voice_play_reset = true;
      if (g_daily_briefing && pm_voice_daily_briefing_streamed()) {
        pm_voice_result_free(&g_voice_result);
        g_daily_briefing = false;
        pm_speaker_set_max_play_seconds(180);
        g_state = AppState::kClock;
        g_clock_repaint_pending = true;
        break;
      }
      if (voice_audio_streamed) {
        pm_voice_result_free(&g_voice_result);
        g_astro_voice_active = false;
        g_synastry_voice_active = false;
        g_alethiometer_voice_active = false;
        g_moon_fortune_active = false;
        g_runes_fortune_active = false;
        g_daily_briefing = false;
        pm_speaker_set_max_play_seconds(180);
        g_state = AppState::kClock;
        g_clock_repaint_pending = true;
        break;
      }
      if (g_daily_briefing) {
        pm_speaker_set_max_play_seconds(600);
      }
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
      if (g_alethiometer_voice_active) {
        if (!s_play_armed) {
          if (!g_voice_result.mp3 || g_voice_result.mp3_len < 64) {
            pm_face_alethiometer_draw_voice_screen("no audio");
            delay(1200);
            pm_voice_result_free(&g_voice_result);
            g_alethiometer_voice_active = false;
            g_state = AppState::kClock;
            g_clock_repaint_pending = true;
            break;
          }
          if (!pm_speaker_play_begin(g_voice_result.mp3, g_voice_result.mp3_len)) {
            pm_face_alethiometer_draw_voice_screen("speaker busy");
            delay(1200);
            pm_voice_result_free(&g_voice_result);
            g_alethiometer_voice_active = false;
            g_state = AppState::kClock;
            g_clock_repaint_pending = true;
            break;
          }
          s_play_armed = true;
          s_play_wait_t0 = now;
        }
        pm_face_alethiometer_draw_voice_screen(nullptr);
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
          pm_face_alethiometer_draw_voice_screen("playback failed");
          delay(1200);
        }
        pm_voice_result_free(&g_voice_result);
        s_play_armed = false;
        s_play_wait_t0 = 0;
        g_alethiometer_voice_active = false;
        g_state = AppState::kClock;
        g_clock_repaint_pending = true;
        break;
      }
      if (g_daily_briefing) {
        if (!s_play_armed) {
          if (!g_voice_result.mp3 || g_voice_result.mp3_len < 64) {
            pm_face_home_briefing_draw_speaking(now);
            delay(1200);
            pm_voice_result_free(&g_voice_result);
            g_daily_briefing = false;
            pm_speaker_set_max_play_seconds(180);
            g_state = AppState::kClock;
            g_clock_repaint_pending = true;
            break;
          }
          if (!pm_speaker_play_begin(g_voice_result.mp3, g_voice_result.mp3_len)) {
            pm_face_home_briefing_draw_speaking(now);
            delay(1200);
            pm_voice_result_free(&g_voice_result);
            g_daily_briefing = false;
            pm_speaker_set_max_play_seconds(180);
            g_state = AppState::kClock;
            g_clock_repaint_pending = true;
            break;
          }
          s_play_armed = true;
          s_play_wait_t0 = now;
        }
        pm_face_home_briefing_draw_speaking(now);
        gfx->flush();
        const PmSpeakerStatus spk = pm_speaker_poll();
        if (spk == PmSpeakerStatus::Playing) {
          const uint32_t est_ms =
              static_cast<uint32_t>((g_voice_result.mp3_len * 8u * 1000u) / 96000u) + 120000u;
          if (s_play_wait_t0 != 0 && (now - s_play_wait_t0) > est_ms) {
            pm_speaker_abort();
          } else {
            break;
          }
        }
        if (spk == PmSpeakerStatus::DoneFail) {
          delay(800);
        }
        pm_voice_result_free(&g_voice_result);
        s_play_armed = false;
        s_play_wait_t0 = 0;
        g_daily_briefing = false;
        pm_speaker_set_max_play_seconds(180);
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
      if (g_runes_fortune_active) {
        if (!s_play_armed) {
          if (!g_voice_result.mp3 || g_voice_result.mp3_len < 64) {
            pm_face_runes_draw_voice_screen("no audio", -1.f);
            delay(1200);
            pm_voice_result_free(&g_voice_result);
            g_runes_fortune_active = false;
            g_state = AppState::kClock;
            g_clock_repaint_pending = true;
            break;
          }
          if (!pm_speaker_play_begin(g_voice_result.mp3, g_voice_result.mp3_len)) {
            pm_face_runes_draw_voice_screen("speaker busy", -1.f);
            delay(1200);
            pm_voice_result_free(&g_voice_result);
            g_runes_fortune_active = false;
            g_state = AppState::kClock;
            g_clock_repaint_pending = true;
            break;
          }
          s_play_armed = true;
          s_play_wait_t0 = now;
        }
        pm_face_runes_draw_voice_screen(nullptr, -1.f);
        pm_face_draw_voice_waves_overlay(true, now);
        pm_gfx->fillRect(0, LCD_HEIGHT - 40, LCD_WIDTH, 40, pm_gfx->color565(7, 8, 13));
        pm_face_draw_centered_line("speaking", LCD_HEIGHT - 28, pm_gfx->color565(230, 210, 156), 1, 1);
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
          pm_face_runes_draw_voice_screen("playback failed", -1.f);
          delay(1200);
        }
        pm_voice_result_free(&g_voice_result);
        s_play_armed = false;
        s_play_wait_t0 = 0;
        g_runes_fortune_active = false;
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
