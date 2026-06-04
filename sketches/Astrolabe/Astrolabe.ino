// Mynah Astrolabe: WiFi + NTP hue clock + hold-to-talk (Supabase voice-pipeline: STT / LLM / TTS).

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <WiFi.h>
#include <Wire.h>
#include <cstdio>
#include <cmath>
#include <ctime>
#include <cstring>
#include <strings.h>

#include "esp_heap_caps.h"
#include <esp_system.h>
#include "esp32-hal-tinyusb.h"

#include "astrolabe_baseline.h"
#include "pin_config.h"
#include "pm_config.h"
#include "pm_gesture.h"
#include "pm_geo_tz.h"
#include "pm_mic.h"
#include "pm_side_buttons.h"
#include "pm_speaker.h"
#include "pm_audio_route.h"
#include "pm_usb_uac.h"
#include "pm_usb_hid.h"
#include "pm_usb_midi.h"
#include "pm_rtp_midi.h"
#include "pm_touch.h"
#include "pm_spotify.h"
#include "pm_voice.h"
#include "pm_wifi_creds.h"
#include "pm_wifi_ntp.h"
#include "pm_screen_http.h"
#include "pm_remote_control.h"
#include "pm_birth_nvs.h"
#include "pm_chart_profiles.h"
#include "pm_transit.h"
#include "pm_castalia_auth.h"
#include "pm_calcifer.h"
#include "pm_rocket.h"
#include "pm_commonplace.h"
#include "pm_astro_highlight.h"
#include "faces/pm_faces.h"
#include "faces/shared/pm_face_draw.h"
#include "faces/alethiometer/pm_face_alethiometer.h"
#include "faces/astrology/pm_face_astrology.h"
#include "faces/biometrics/pm_face_biometrics.h"
#include "faces/bongo/pm_face_bongo.h"
#include "faces/chakra/pm_face_chakra.h"
#include "faces/chord/pm_face_chord.h"
#include "faces/drone/pm_face_drone.h"
#include "faces/kalimba/pm_face_kalimba.h"
#include "faces/ocarina/pm_face_ocarina.h"
#include "faces/pandrum/pm_face_pandrum.h"
#include "faces/piano/pm_face_piano.h"
#include "faces/pitch_pipe/pm_face_pitch_pipe.h"
#include "faces/tibetan_bowl/pm_face_tibetan_bowl.h"
#include "faces/moon/pm_face_moon.h"
#include "faces/runes/pm_face_runes.h"
#include "faces/tarot/pm_face_tarot.h"
#include "faces/notes/pm_face_notes.h"
#include "faces/question_day/pm_face_question_day.h"
#include "faces/focus/pm_face_focus.h"
#include "faces/geomancy/pm_face_geomancy.h"
#include "faces/globe/pm_face_globe.h"
#include "faces/hid/pm_face_hid.h"
#include "faces/inq_card/pm_face_inq_card.h"
#include "faces/spotify/pm_face_spotify.h"
#include "faces/calcifer/pm_face_calcifer.h"
#include "faces/level/pm_face_level.h"
#include "faces/lenormand/pm_face_lenormand.h"
#include "faces/luopan/pm_face_luopan.h"
#include "faces/pythia/pm_face_pythia.h"
#include "faces/babel_fish/pm_face_babel_fish.h"
#include "faces/enochian_angel/pm_face_enochian_angel.h"
#include "faces/weather/pm_face_weather.h"
#include "faces/watcher/pm_face_watcher.h"
#include "faces/settings/pm_face_settings_wifi.h"
#include "pm_weather.h"
#include "faces/quotes/pm_face_quotes.h"
#include "pm_quotes.h"
#include "faces/spectrum/pm_face_spectrum.h"
#include "faces/sky/pm_face_sky.h"
#include "faces/synastry/pm_face_synastry.h"
#include "faces/radar/pm_face_radar.h"
#include "faces/orientation/pm_face_orientation.h"
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
#include "pm_power.h"
#include "pm_settings.h"
#include "pm_daily_briefing.h"
#include "pm_daily_briefing_nvs.h"
#include "pm_user_nvs.h"
#include "pm_variant.h"
#include "faces/home/pm_face_home_briefing.h"
#include "pm_speaker.h"

Arduino_DataBus *bus = new Arduino_ESP32QSPI(
    LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);

Arduino_CO5300 *tft = new Arduino_CO5300(
    bus, LCD_RESET, 0, false, LCD_WIDTH, LCD_HEIGHT, 6, 0, 0, 0);
/** Portable framebuffer facade; flush() pushes pixels to the CO5300 (enables WiFi BMP grab). */
PmDisplayCanvas *gfx = new PmDisplayCanvas(LCD_WIDTH, LCD_HEIGHT, tft);

static void astrolabe_set_brightness(uint8_t brightness) {
#ifndef ASTROLABE_QEMU
  if (tft) {
    tft->setBrightness(brightness);
  }
#else
  (void)brightness;
#endif
}

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
static constexpr uint8_t k_tv_question = 6;
static uint8_t g_text_voice_route = k_tv_none;
static bool g_calcifer_briefing = false;
/** Home / first-run: full daily LLM+TTS briefing (schedule + sky). */
static bool g_daily_briefing = false;
static uint32_t s_daily_brief_next_try_ms = 0;
static bool s_face_tour_active = false;
static int s_face_tour_idx = 0;
static uint32_t s_face_tour_last_ms = 0;
static uint32_t s_face_tour_dwell_ms = 2800;
static bool s_face_tour_narrate = false;
static bool s_face_tour_button_test = false;
static uint8_t s_face_tour_voice_phase = 0;
static uint32_t s_face_tour_voice_started_ms = 0;
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
static constexpr size_t kQuestionVoiceMsgCap = 9500;
static constexpr size_t kQuestionSysPromptCap = 900;
static char *s_face_tour_voice_msg = nullptr;
static char *s_face_tour_sys_prompt = nullptr;
static char s_face_voice_face[24] = "";
static char s_face_voice_faculty_slug[64] = "";
static char s_face_voice_faculty_name[96] = "";
static char s_face_voice_tts_voice[64] = "";
static char *g_moon_voice_msg = nullptr;
static char *g_moon_sys_prompt = nullptr;
static char *g_runes_voice_msg = nullptr;
static char *g_runes_sys_prompt = nullptr;
static char *g_question_voice_msg = nullptr;
static char *g_question_sys_prompt = nullptr;
static bool g_moon_voice_pcm = false;
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
  if (!g_question_voice_msg) g_question_voice_msg = voice_psram_buffer(kQuestionVoiceMsgCap);
  if (!g_question_sys_prompt) g_question_sys_prompt = voice_psram_buffer(kQuestionSysPromptCap);
  if (!g_astrology_voice_msg) g_astrology_voice_msg = voice_psram_buffer(kAstrologyVoiceMsgCap);
  if (!g_astrology_sys_prompt) g_astrology_sys_prompt = voice_psram_buffer(kAstrologySysPromptCap);
  if (!g_synastry_voice_msg) g_synastry_voice_msg = voice_psram_buffer(kSynastryVoiceMsgCap);
  if (!g_synastry_sys_prompt) g_synastry_sys_prompt = voice_psram_buffer(kSynastrySysPromptCap);
  return s_face_tour_voice_msg && s_face_tour_sys_prompt && g_moon_voice_msg && g_moon_sys_prompt &&
         g_runes_voice_msg && g_runes_sys_prompt && g_astrology_voice_msg && g_astrology_sys_prompt &&
         g_synastry_voice_msg && g_synastry_sys_prompt && g_question_voice_msg && g_question_sys_prompt;
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
/** Pythia voice: stay on the Delphi bust during record/think/speak. */
static bool g_pythia_voice_active = false;
static bool g_pythia_voice_pcm = false;
/** Babel Fish voice: stay on the fish during record/think/speak translation. */
static bool g_babel_fish_voice_active = false;
static bool g_babel_fish_voice_pcm = false;
/** Question of the Day: text fetch or recorded answer. */
static bool g_question_voice_active = false;
static bool g_question_answer_pcm = false;
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
static uint32_t s_remote_touch_clear_at = 0;
static uint32_t s_remote_pwr_hold_clear_at = 0;
static uint8_t *g_pcm = nullptr;
static size_t g_pcm_len = 0;
static PmVoiceResult g_voice_result = {};
char g_gesture_banner[44] = "";
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

/** Stop voice/PTT and return to clock (swipe away from Moon fortune, astro, etc.). */
static void gesture_end_voice_ui(void) {
  pm_faces_set_navigation_mode(false);
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
  g_alethiometer_voice_active = false;
  g_alethiometer_voice_pcm = false;
  g_pythia_voice_active = false;
  g_pythia_voice_pcm = false;
  g_babel_fish_voice_active = false;
  g_babel_fish_voice_pcm = false;
  g_moon_voice_pcm = false;
  g_question_voice_active = false;
  g_question_answer_pcm = false;
  g_commonplace_journal = false;
  g_commonplace_note_face = false;
  s_rec_mic_on = false;
  g_voice_play_reset = true;
  g_state = AppState::kClock;
}

static bool home_begin_daily_briefing(void) {
#if defined(ASTROLABE_NO_ONBOARD_AUDIO) && ASTROLABE_NO_ONBOARD_AUDIO
  snprintf(g_gesture_banner, sizeof(g_gesture_banner), "brief: no speaker");
  return false;
#endif
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
  pm_faculty_release_bust_cache();
  if (!pm_heap_briefing_ready("briefing")) {
    snprintf(g_gesture_banner, sizeof(g_gesture_banner), "brief: low heap");
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
  g_pythia_voice_active = false;
  g_pythia_voice_pcm = false;
  g_babel_fish_voice_active = false;
  g_babel_fish_voice_pcm = false;
  g_moon_fortune_active = false;
  g_moon_voice_pcm = false;
  g_question_voice_active = false;
  g_question_answer_pcm = false;
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

static void daily_briefing_mark_success(void) {
  if (!pm_time_valid()) {
    return;
  }
  struct tm tm_now = {};
  pm_time_local(&tm_now);
  pm_daily_briefing_mark_played(&tm_now);
}

static bool gesture_cycle_face(int delta) {
  if (g_state != AppState::kClock) {
    gesture_end_voice_ui();
  }
  pm_faces_cycle(delta);
  g_gesture_banner[0] = '\0';
  g_clock_repaint_pending = true;
  Serial.printf("[gesture] face -> %d\n", static_cast<int>(pm_faces_current()));
  return true;
}

static bool gesture_is_navigation_swipe(PmGestureKind kind) {
  return kind == PmGestureKind::SwipeLeft || kind == PmGestureKind::SwipeRight ||
         kind == PmGestureKind::SwipeUp || kind == PmGestureKind::SwipeDown;
}

static int gesture_navigation_delta(PmGestureKind kind) {
  return (kind == PmGestureKind::SwipeLeft || kind == PmGestureKind::SwipeUp) ? 1 : -1;
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
  g_alethiometer_voice_active = false;
  g_alethiometer_voice_pcm = false;
  g_pythia_voice_active = false;
  g_pythia_voice_pcm = false;
  g_babel_fish_voice_active = false;
  g_babel_fish_voice_pcm = false;
  g_moon_voice_pcm = false;
  g_question_voice_active = false;
  g_question_answer_pcm = false;
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

static bool question_day_begin_fetch() {
  if (!voice_prompt_buffers_ensure()) {
    snprintf(g_gesture_banner, sizeof(g_gesture_banner), "question: PSRAM OOM");
    return false;
  }
  if (!pm_wifi_connected()) {
    snprintf(g_gesture_banner, sizeof(g_gesture_banner), "question: need WiFi");
    return false;
  }
  if (!pm_face_question_day_build_prompt(g_question_voice_msg, kQuestionVoiceMsgCap,
                                         g_question_sys_prompt, kQuestionSysPromptCap)) {
    snprintf(g_gesture_banner, sizeof(g_gesture_banner), "question: build fail");
    return false;
  }
  pm_voice_result_free(&g_voice_result);
  g_voice_use_message = true;
  g_text_voice_route = k_tv_question;
  s_face_voice_faculty_slug[0] = '\0';
  s_face_voice_faculty_name[0] = '\0';
  g_question_voice_active = true;
  g_question_answer_pcm = false;
  g_moon_fortune_active = false;
  g_runes_fortune_active = false;
  g_astro_voice_active = false;
  g_astro_voice_pcm = false;
  g_synastry_voice_active = false;
  g_synastry_voice_pcm = false;
  g_alethiometer_voice_active = false;
  g_alethiometer_voice_pcm = false;
  g_moon_voice_pcm = false;
  g_calcifer_briefing = false;
  g_daily_briefing = false;
  g_voice_play_reset = true;
  g_state = AppState::kThinking;
  return true;
}

static void faculty_remember_voice_result(const PmVoiceResult &result) {
  if (strcasecmp(result.route, "ask-faculty") != 0 || result.faculty_slug[0] == '\0') {
    return;
  }
  char name[sizeof(result.faculty_name)];
  if (result.faculty_name[0] != '\0') {
    snprintf(name, sizeof(name), "%s", result.faculty_name);
  } else {
    pm_faculty_label_from_slug(result.faculty_slug, name, sizeof(name));
  }
  pm_faculty_note_turn(result.faculty_slug, name, result.transcript, result.reply);
  (void)pm_faculty_request_bust(result.faculty_slug);
  snprintf(g_gesture_banner, sizeof(g_gesture_banner), "faculty: %.25s", name);
}

static bool face_index_from_name(const char *name, int *out) {
  if (!name || !out) {
    return false;
  }
  struct {
    const char *n;
    ClockFace face;
  } k[] = {
      {"classic", ClockFace::ClassicAnalog}, {"hue", ClockFace::ClassicAnalog},
      {"analog", ClockFace::ClassicAnalog}, {"apocalypso", ClockFace::Apocalypso},
      {"digital", ClockFace::DigitalLocal}, {"spotify", ClockFace::Spotify},
      {"astro", ClockFace::Astrology}, {"astrology", ClockFace::Astrology},
      {"moon", ClockFace::Moon}, {"calcifer", ClockFace::CalciferCountdown},
      {"schedule", ClockFace::CalciferCountdown}, {"castalia", ClockFace::Castalia},
      {"settings", ClockFace::Settings}, {"wifi", ClockFace::Settings},
      {"synastry", ClockFace::Synastry}, {"syn", ClockFace::Synastry},
      {"spectrum", ClockFace::Spectrum}, {"fft", ClockFace::Spectrum},
      {"audio", ClockFace::Spectrum}, {"sound", ClockFace::Spectrum},
      {"chakra", ClockFace::Chakra}, {"bowl", ClockFace::TibetanBowl},
      {"tibetan", ClockFace::TibetanBowl}, {"tibetan_bowl", ClockFace::TibetanBowl},
      {"rocket", ClockFace::Rocket}, {"launch", ClockFace::Rocket},
      {"launchclock", ClockFace::Rocket}, {"radar", ClockFace::Radar},
      {"presence", ClockFace::Radar}, {"peers", ClockFace::Radar},
      {"locator", ClockFace::Radar}, {"locations", ClockFace::Radar},
      {"faculty", ClockFace::Faculty}, {"fac", ClockFace::Faculty},
      {"weather", ClockFace::Weather}, {"globe", ClockFace::Globe},
      {"earth", ClockFace::Globe}, {"sky", ClockFace::Sky}, {"stars", ClockFace::Sky},
      {"quotes", ClockFace::Quotes}, {"quote", ClockFace::Quotes},
      {"transits", ClockFace::LiveTransits}, {"live_transits", ClockFace::LiveTransits},
      {"live-transits", ClockFace::LiveTransits}, {"live", ClockFace::LiveTransits},
      {"tarot", ClockFace::Tarot}, {"cards", ClockFace::Tarot}, {"card", ClockFace::Tarot},
      {"arcana", ClockFace::Tarot}, {"inq", ClockFace::InqCard}, {"inq_card", ClockFace::InqCard},
      {"inq-card", ClockFace::InqCard}, {"card_of_day", ClockFace::InqCard},
      {"card-of-day", ClockFace::InqCard}, {"cotd", ClockFace::InqCard},
      {"notes", ClockFace::Notes}, {"note", ClockFace::Notes},
      {"commonplace", ClockFace::Notes}, {"notebook", ClockFace::Notes},
      {"ocarina", ClockFace::Ocarina}, {"ocarina_face", ClockFace::Ocarina},
      {"pitch_pipe", ClockFace::PitchPipe}, {"pitch-pipe", ClockFace::PitchPipe},
      {"pitchpipe", ClockFace::PitchPipe}, {"pipe", ClockFace::PitchPipe},
      {"flute", ClockFace::Ocarina}, {"bongo", ClockFace::Bongo},
      {"drum", ClockFace::Bongo}, {"drums", ClockFace::Bongo}, {"conga", ClockFace::Bongo},
      {"piano", ClockFace::Piano}, {"keys", ClockFace::Piano},
      {"keyboard", ClockFace::Piano}, {"kalimba", ClockFace::Kalimba},
      {"mbira", ClockFace::Kalimba}, {"thumb_piano", ClockFace::Kalimba},
      {"drone", ClockFace::Drone}, {"shruti", ClockFace::Drone},
      {"chord", ClockFace::Chord}, {"chords", ClockFace::Chord},
      {"autoharp", ClockFace::Chord}, {"level", ClockFace::Level},
      {"bubble", ClockFace::Level}, {"bubble_level", ClockFace::Level}, {"imu", ClockFace::Level},
      {"tuning", ClockFace::Tuning}, {"tuner", ClockFace::Tuning},
      {"staff", ClockFace::Tuning}, {"pitch", ClockFace::Tuning},
      {"pandrum", ClockFace::PanDrum}, {"pan_drum", ClockFace::PanDrum},
      {"pan-drum", ClockFace::PanDrum}, {"pandrom", ClockFace::PanDrum},
      {"pandrom_face", ClockFace::PanDrum}, {"handpan", ClockFace::PanDrum},
      {"hang", ClockFace::PanDrum}, {"alethiometer", ClockFace::Alethiometer},
      {"aleth", ClockFace::Alethiometer}, {"compass", ClockFace::Alethiometer},
      {"golden_compass", ClockFace::Alethiometer}, {"runes", ClockFace::Runes},
      {"rune", ClockFace::Runes}, {"futhark", ClockFace::Runes}, {"fortune", ClockFace::Runes},
      {"orientation", ClockFace::Orientation}, {"orient", ClockFace::Orientation},
      {"heading", ClockFace::Orientation}, {"relative_heading", ClockFace::Orientation},
      {"luopan", ClockFace::Luopan}, {"fengshui", ClockFace::Luopan},
      {"feng_shui", ClockFace::Luopan}, {"feng-shui", ClockFace::Luopan},
      {"qotd", ClockFace::QuestionOfDay}, {"question", ClockFace::QuestionOfDay},
      {"question_day", ClockFace::QuestionOfDay}, {"question-of-day", ClockFace::QuestionOfDay},
      {"question_of_the_day", ClockFace::QuestionOfDay}, {"focus", ClockFace::FocusTimer},
      {"timer", ClockFace::FocusTimer}, {"pomodoro", ClockFace::FocusTimer},
      {"productivity", ClockFace::FocusTimer}, {"biometrics", ClockFace::Biometrics},
      {"bio", ClockFace::Biometrics}, {"signals", ClockFace::Biometrics},
      {"enso", ClockFace::Biometrics}, {"readiness", ClockFace::Biometrics},
      {"attention", ClockFace::Biometrics},
      {"watcher", ClockFace::Watcher}, {"sensecap", ClockFace::Watcher},
      {"camera", ClockFace::Watcher}, {"facecam", ClockFace::Watcher},
      {"lenormand", ClockFace::Lenormand}, {"len", ClockFace::Lenormand},
      {"oracle", ClockFace::Lenormand}, {"petit_lenormand", ClockFace::Lenormand},
      {"pythia", ClockFace::Pythia}, {"delphi", ClockFace::Pythia},
      {"babel", ClockFace::BabelFish}, {"babel_fish", ClockFace::BabelFish},
      {"babel-fish", ClockFace::BabelFish}, {"fish", ClockFace::BabelFish},
      {"translate", ClockFace::BabelFish}, {"translator", ClockFace::BabelFish},
      {"geomancy", ClockFace::Geomancy}, {"geomantic", ClockFace::Geomancy},
      {"geo", ClockFace::Geomancy}, {"figures", ClockFace::Geomancy},
      {"hid", ClockFace::HidTouchpad}, {"touchpad", ClockFace::HidTouchpad},
      {"mouse", ClockFace::HidTouchpad}, {"controller", ClockFace::HidTouchpad},
      {"enochian", ClockFace::EnochianAngel}, {"angel", ClockFace::EnochianAngel},
      {"enochian_angel", ClockFace::EnochianAngel}, {"enochian-angel", ClockFace::EnochianAngel}};
  for (const auto &e : k) {
    if (strcasecmp(name, e.n) == 0) {
      *out = static_cast<int>(e.face);
      return true;
    }
  }
  return false;
}

struct FaceTourInfo {
  ClockFace face;
  const char *name;
  const char *summary;
  const char *tts_focus;
  const char *ok;
  const char *warn;
  bool needs_wifi;
  bool needs_time;
};

static const FaceTourInfo k_face_tour[] = {
    {ClockFace::ClassicAnalog, "classic", "hue home clock with breathing gem pulse", "a short daily orientation from the home clock",
     "drawing locally", "heap is low", false, false},
    {ClockFace::Apocalypso, "apocalypso", "watch-style day wheel and local time", "a brief reading of the day wheel and risk-radar mood",
     "drawing local time", "time is not synced", false, true},
    {ClockFace::DigitalLocal, "digital", "large local digital clock", "a concise spoken local-time check-in", "drawing local time",
     "time is not synced", false, true},
    {ClockFace::Spotify, "spotify", "Spotify transport and now-playing surface", "a musical listening prompt for the current moment",
     "WiFi is available for refresh", "offline, transport is display-only", true, false},
    {ClockFace::Astrology, "astro", "live sky wheel and astrology voice hooks", "the current astrology transits and sky wheel",
     "time and WiFi are ready", "needs WiFi and time for live reading", true, true},
    {ClockFace::Moon, "moon", "lunar phase, fortune tap, and Moon voice", "today's lunar phase and fortune",
     "time and WiFi are ready", "needs WiFi and time for fortune voice", true, true},
    {ClockFace::CalciferCountdown, "calcifer", "rolling agenda daywheel from calendar", "the next calendar moment and schedule rhythm",
     "calendar refresh can run", "needs WiFi and time for calendar", true, true},
    {ClockFace::Castalia, "castalia", "Castalia pairing QR and auth status", "Castalia sign-in status and what pairing unlocks",
     "WiFi is available for pairing", "offline, pairing QR only", true, false},
    {ClockFace::Settings, "settings", "WiFi and Castalia settings hub", "a settings health check for WiFi, auth, heap, and time",
     "settings UI is drawing", "settings UI is drawing", false, false},
    {ClockFace::Synastry, "synastry", "dual natal chart and relationship aspects", "the active synastry relationship highlight",
     "time and WiFi are ready", "needs WiFi and time for voice", true, true},
    {ClockFace::Spectrum, "spectrum", "microphone spectrum visualizer modes", "a sound-check prompt for the audio spectrum face",
     "local audio analyzer is drawing", "audio analyzer is local only", false, false},
    {ClockFace::Chakra, "chakra", "chakra symbols with solfeggio tones", "the current chakra tone and embodied attention",
     "local tone controls are available", "local tone controls are available", false, false},
    {ClockFace::TibetanBowl, "bowl", "Tibetan bowl rim instrument", "a short singing-bowl meditation prompt",
     "local rim instrument is available", "local rim instrument is available", false, false},
    {ClockFace::Rocket, "rocket", "upcoming orbital launch clock", "the next launch window and mission context",
     "launch refresh can run", "needs WiFi and time for launches", true, true},
    {ClockFace::Radar, "radar", "BLE locator and nearby peer radar", "nearby BLE peers and spatial presence",
     "BLE radar can start", "heap is tight after BLE", false, false},
    {ClockFace::HidTouchpad, "hid", "USB HID mouse and touchpad controller", "the HID touchpad readiness and host control state",
     "USB HID touchpad is available", "requires HID firmware target", false, false},
    {ClockFace::Faculty, "faculty", "recent ask-faculty conversation portraits", "the active faculty persona and recent conversation",
     "WiFi is available for portraits", "offline, cached portraits only", true, false},
    {ClockFace::Weather, "weather", "24-hour radial forecast rings", "the local 24-hour weather ring",
     "weather refresh can run", "needs WiFi and time for forecast", true, true},
    {ClockFace::Globe, "globe", "spinning Earth disk with live day-night terminator",
     "the current Earth daylight pattern and local time context",
     "time is available for the terminator", "needs time for daylight line", false, true},
    {ClockFace::Sky, "sky", "draggable night-sky planisphere with stars and constellation lines",
     "the visible sky orientation and constellation field",
     "time is available for sky motion", "needs time for sky motion", false, true},
    {ClockFace::Quotes, "quotes", "Castalia quote of the day with faculty bust", "the quote of the day and its faculty context",
     "quote refresh can run", "offline demo quote only", true, false},
    {ClockFace::LiveTransits, "transits", "live planetary spheres and next Moon ingress", "live transits and the next Moon ingress",
     "time and ephemeris are ready", "needs time for live transits", false, true},
    {ClockFace::Tarot, "tarot", "daily Major Arcana card and deck browser", "the active Major Arcana card",
     "drawing local Major Arcana", "drawing local Major Arcana", false, false},
    {ClockFace::InqCard, "inq-card", "iNQ Card of the Day image from cards.castalia.institute",
     "today's iNQ Card of the Day", "card image refresh can run", "needs WiFi and time for card image", true, true},
    {ClockFace::Notes, "notes", "offline voice notes queued for Commonplace", "the offline note capture queue",
     "flash note queue is available", "flash note queue is available", false, false},
    {ClockFace::Ocarina, "ocarina", "breath-played clay ocarina", "the active ocarina key and breath note",
     "local ocarina tones are available", "local ocarina tones are available", false, false},
    {ClockFace::PitchPipe, "pitch-pipe", "tap-or-breath reference pitch pipe", "the selected reference pitch",
     "local pitch pipe tones are available", "local pitch pipe tones are available", false, false},
    {ClockFace::Bongo, "bongo", "touch-playable bongo with center-to-rim pitch", "the last bongo tap pitch and drum feel",
     "local bongo hits are available", "local bongo hits are available", false, false},
    {ClockFace::Piano, "piano", "one-octave circular piano", "the active piano key and note",
     "local piano tones are available", "local piano tones are available", false, false},
    {ClockFace::Kalimba, "kalimba", "pentatonic thumb-piano tines", "the active kalimba tine",
     "local kalimba tones are available", "local kalimba tones are available", false, false},
    {ClockFace::Drone, "drone", "sustained root/fifth/octave drone", "the active drone root",
     "local drone tones are available", "local drone tones are available", false, false},
    {ClockFace::Chord, "chord", "autoharp-style harmony pads", "the active chord pad",
     "local chord tones are available", "local chord tones are available", false, false},
    {ClockFace::Level, "level", "IMU rolling-sphere level with the top of the display as forward",
     "the current level nudge", "IMU level is drawing", "IMU unavailable", false, false},
    {ClockFace::Tuning, "tuning", "live microphone tuning staff with detected notes", "the currently detected pitch and cents",
     "local pitch detector is listening", "local pitch detector is listening", false, false},
    {ClockFace::PanDrum, "pandrum", "14-note touch-playable handpan", "the active pan drum note and resonance",
     "local pan drum tones are available", "local pan drum tones are available", false, false},
    {ClockFace::Alethiometer, "alethiometer", "36-symbol compass with three question needles and one answer needle",
     "the active alethiometer symbols and narrative interpretation",
     "WiFi is available for LLM interpretation", "offline, compass animation only", true, false},
    {ClockFace::Runes, "runes", "three-rune past, present, future fortune spread", "the selected rune spread and spoken fortune",
     "WiFi is available for TTS fortune", "offline, visual spread only", true, false},
    {ClockFace::Orientation, "orientation", "relative heading and pitch/roll orientation dial", "the current relative orientation",
     "6DOF orientation is drawing", "IMU unavailable", false, false},
    {ClockFace::Luopan, "luopan", "feng-shui luopan dial with 24 mountains", "the active relative luopan alignment",
     "relative luopan is drawing", "IMU unavailable", false, false},
    {ClockFace::QuestionOfDay, "question", "context-aware Question of the Day with Commonplace answers",
     "the current Question of the Day and answer capture state",
     "WiFi is available for fresh questions", "offline, last question only", true, false},
    {ClockFace::FocusTimer, "focus", "Pomodoro productivity timer with focus and break presets",
     "the active focus timer and session state",
     "local timer is available", "local timer is available", false, false},
    {ClockFace::Biometrics, "enso", "simulated EEG/HRV attention and readiness face",
     "the inferred attention, readiness, simulated EEG focus, HRV balance, and coherence parameters",
     "sensor model is sampling", "some sensor inputs are unavailable", false, false},
    {ClockFace::Watcher, "watcher", "SenseCAP camera presence face with SSCMA, face metrics, and Castalia greeting",
     "the Watcher camera/presence pipeline, face metrics, and greeting readiness",
     "Watcher pipeline is visible", "needs Watcher firmware for live camera frames", true, false},
    {ClockFace::Lenormand, "lenormand", "daily 36-card Lenormand oracle using Noto Emoji symbols",
     "the active Lenormand card and its practical keyword",
     "drawing local Lenormand deck", "drawing local Lenormand deck", false, false},
    {ClockFace::Pythia, "pythia", "Delphi oracle bust for obtuse spoken answers",
     "a question for Pythia, answered as an ambiguous oracle",
     "WiFi is available for oracle voice", "offline, Pythia bust only", true, false},
    {ClockFace::BabelFish, "babel_fish", "Babel Fish spoken translator with STT, LLM, and TTS",
     "a spoken phrase translated into or back out of the device native language",
     "WiFi is available for translation voice", "offline, fish face only", true, false},
    {ClockFace::Geomancy, "geomancy", "daily geomantic figure from the 16 traditional figures",
     "the active geomantic figure and its practical keyword",
     "drawing local geomancy figures", "drawing local geomancy figures", false, false},
    {ClockFace::EnochianAngel, "enochian", "luminous Enochian Angel visage and tablet geometry",
     "an angelic oracle reflection from the tablet face",
     "WiFi is available for angelic oracle voice", "offline, angel face only", true, false},
};

static constexpr int face_tour_count(void) {
  return static_cast<int>(sizeof(k_face_tour) / sizeof(k_face_tour[0]));
}

static const FaceTourInfo *face_tour_info(int idx) {
  if (idx < 0 || idx >= face_tour_count()) {
    return nullptr;
  }
  return &k_face_tour[idx];
}

static const FaceTourInfo *face_tour_info_for_face(ClockFace face) {
  for (int i = 0; i < face_tour_count(); ++i) {
    if (k_face_tour[i].face == face) {
      return &k_face_tour[i];
    }
  }
  return nullptr;
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

static const ClockFace k_instrument_stack[] = {
    ClockFace::Chakra,      ClockFace::TibetanBowl, ClockFace::Ocarina, ClockFace::PitchPipe,
    ClockFace::Bongo,       ClockFace::Kalimba,     ClockFace::Drone,   ClockFace::Chord,
    ClockFace::Piano,       ClockFace::PanDrum,     ClockFace::Tuning,
};

static const char *instrument_stack_label(ClockFace face) {
  switch (face) {
    case ClockFace::Chakra:
      return "chakra";
    case ClockFace::TibetanBowl:
      return "bowl";
    case ClockFace::Ocarina:
      return "ocarina";
    case ClockFace::PitchPipe:
      return "pitch";
    case ClockFace::Bongo:
      return "bongo";
    case ClockFace::Piano:
      return "piano";
    case ClockFace::Kalimba:
      return "kalimba";
    case ClockFace::Drone:
      return "drone";
    case ClockFace::Chord:
      return "chord";
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

static void release_noninstrument_speaker_task(uint32_t now) {
  static uint32_t s_last_release_try_ms = 0;
  if (g_state != AppState::kClock || instrument_stack_contains(pm_faces_current())) {
    return;
  }
  if (now - s_last_release_try_ms < 1000u) {
    return;
  }
  s_last_release_try_ms = now;
  (void)pm_speaker_release_idle_task();
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

static bool face_voice_build_prompt(const FaceTourInfo *info, char *msg, size_t msg_cap,
                                    char *sys, size_t sys_cap, bool tour_test) {
  if (!info || !msg || msg_cap == 0 || !sys || sys_cap == 0) {
    return false;
  }
  msg[0] = '\0';
  sys[0] = '\0';
  s_face_voice_face[0] = '\0';
  s_face_voice_faculty_slug[0] = '\0';
  s_face_voice_faculty_name[0] = '\0';
  const ClockFace face = info->face;
  const char *health = face_tour_health_text(info);
  char when[40];
  face_tour_format_clock(when, sizeof(when));

  if (face == ClockFace::Astrology) {
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
    if (!pm_time_valid() || !pm_face_moon_build_fortune_message(msg, msg_cap) ||
        !pm_face_moon_build_fortune_system_prompt(sys, sys_cap)) {
      snprintf(g_gesture_banner, sizeof(g_gesture_banner), "moon: build fail");
      return false;
    }
    return true;
  }
  if (face == ClockFace::Synastry) {
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
           "You are the Mynah Astrolabe face-specific TTS button. Speak like a gentle fortune teller reading "
           "omens from a brass astrolabe: warm, a little mysterious, but grounded. Use only the supplied face "
           "state. Offer one omen, one counsel, and one vivid image. Keep it under 35 seconds. Do not say this "
           "is a test unless something is unavailable, and never claim certainty or fixed fate.");

  switch (face) {
    case ClockFace::ClassicAnalog:
      snprintf(msg, msg_cap,
               "Face: classic home clock. Current state: %s; %s. Give a short daily orientation grounded in "
               "the breathing hue clock.",
               when, health);
      break;
    case ClockFace::Apocalypso:
      snprintf(msg, msg_cap,
               "Face: Apocalypso day wheel. Current state: %s; %s. Give a brief spoken read of the day's "
               "risk-radar mood and what to notice next.",
               when, health);
      break;
    case ClockFace::DigitalLocal:
      snprintf(msg, msg_cap,
               "Face: digital local clock. Current state: %s; %s. Speak a concise time check-in with one "
               "useful nudge for the next hour.",
               when, health);
      break;
    case ClockFace::Spotify:
      snprintf(msg, msg_cap,
               "Face: Spotify. Current state: %s. Give a listening prompt for the current moment; if playback "
               "metadata is unavailable, say so gracefully.",
               health);
      break;
    case ClockFace::CalciferCountdown:
      if (!g_calcifer_ui.ok && pm_wifi_connected() && pm_time_valid() &&
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
                 "Face: Calcifer agenda daywheel. Now: %s. Calendar state: %s. Give a concise schedule "
                 "status and what is missing.",
                 when, g_calcifer_ui.error[0] ? g_calcifer_ui.error : health);
      }
      break;
    case ClockFace::Castalia:
      snprintf(msg, msg_cap,
               "Face: Castalia pairing. Status: WiFi %s, time %s, heap largest %u bytes. Explain what signing "
               "in unlocks on the watch.",
               pm_wifi_connected() ? "connected" : "offline", pm_time_valid() ? "synced" : "unsynced",
               static_cast<unsigned>(pm_heap_internal_largest()));
      break;
    case ClockFace::Settings:
      snprintf(msg, msg_cap,
               "Face: settings. WiFi %s, time %s, local clock %s, heap largest %u bytes. Give a short health "
               "check.",
               pm_wifi_connected() ? "connected" : "offline", pm_time_valid() ? "synced" : "unsynced", when,
               static_cast<unsigned>(pm_heap_internal_largest()));
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
      if ((!g_rocket_ui.ok || g_rocket_ui.count <= 0) && pm_wifi_connected() && pm_time_valid() &&
          ESP.getFreeHeap() >= MYNAH_ROCKET_MIN_FETCH_HEAP) {
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
                 "Face: rocket launch clock. Launch data state: %s. Give a concise launch-clock status.",
                 g_rocket_ui.error[0] ? g_rocket_ui.error : health);
      }
      break;
    }
    case ClockFace::Radar:
      snprintf(msg, msg_cap,
               "Face: BLE radar. Nearby peer count: %u. Heap largest: %u bytes. Give a short spatial-presence "
               "readout.",
               static_cast<unsigned>(pm_presence_peer_count()), static_cast<unsigned>(pm_heap_internal_largest()));
      break;
    case ClockFace::Biometrics: {
      char state[360];
      pm_face_biometrics_format_prompt_state(state, sizeof(state));
      pm_face_biometrics_pause_ble_for_voice();
      snprintf(sys, sys_cap,
               "You are the Mynah Astrolabe Enso readiness guide. The watch supplies simulated attention and "
               "readiness parameters from provisional EEG and HRV channels plus WiFi RSSI, BLE presence, IMU "
               "motion, and microphone audio features. " ASTROLABE_MINDFULNESS_POLICY_TEXT " "
               "Treat these channels as playful, non-medical signals and never claim clinical accuracy. "
               ASTROLABE_MINDFULNESS_RESPONSE_STYLE_TEXT " Offer one readiness readout, one counsel, and one vivid image "
               "under 30 seconds.");
      snprintf(msg, msg_cap,
               "Face: Enso readiness. Inferred sensor state: %s. Give a concise attention/readiness reading "
               "from these parameters.",
               state);
      break;
    }
    case ClockFace::Watcher: {
      char state[420];
      pm_face_watcher_format_prompt_state(state, sizeof(state));
      snprintf(sys, sys_cap,
               "You are the Mynah Astrolabe Watcher guide. The Watcher face represents a SenseCAP camera "
               "pipeline that uses local SSCMA person/face detections, waits for the subject to settle near "
               "center, sends an image to face.castalia.institute for facial metrics, then asks Castalia for "
               "a brief spoken greeting. " ASTROLABE_MINDFULNESS_POLICY_TEXT " Treat all face observations as "
               "self-reflection cues, never as facts about identity, personality, diagnosis, emotion, or fate. "
               ASTROLABE_MINDFULNESS_RESPONSE_STYLE_TEXT " Keep the response under 25 seconds.");
      snprintf(msg, msg_cap,
               "Face: Watcher camera presence. Pipeline state: %s. Give a concise readiness check and one "
               "mindful greeting cue for using the Watcher.",
               state);
      break;
    }
    case ClockFace::Faculty:
    case ClockFace::Wand: {
      PmFacultyProfile faculty = {};
      if (pm_faculty_active(&faculty)) {
        snprintf(s_face_voice_face, sizeof(s_face_voice_face), "%s",
                 pm_faces_current() == ClockFace::Wand ? "wand" : "faculty");
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
      if (!g_weather_ui.ok && pm_wifi_connected() && ESP.getFreeHeap() >= MYNAH_FACE_FETCH_MIN_HEAP) {
        (void)pm_weather_fetch(&g_weather_ui);
      }
      if (g_weather_ui.ok) {
        snprintf(msg, msg_cap,
                 "Face: weather. Location: %s. Current: %d Celsius, %s. High %d, low %d. Give a concise "
                 "24-hour weather ring briefing.",
                 g_weather_ui.location, static_cast<int>(g_weather_ui.current_temp_c), g_weather_ui.condition,
                 static_cast<int>(g_weather_ui.hi_c), static_cast<int>(g_weather_ui.lo_c));
      } else {
        snprintf(msg, msg_cap, "Face: weather. Forecast state: %s. Give a short weather-status note.",
                 g_weather_ui.error[0] ? g_weather_ui.error : health);
      }
      break;
    case ClockFace::Quotes:
      if (!g_quotes_ui.ok && pm_wifi_connected() && ESP.getFreeHeap() >= MYNAH_FACE_FETCH_MIN_HEAP) {
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
               when, health);
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
    case ClockFace::InqCard:
      snprintf(msg, msg_cap,
               "Face: iNQ Card of the Day. Date: %s. Active card: %s. Give a concise observation prompt "
               "for the displayed card image.",
               pm_face_inq_card_date(), pm_face_inq_card_title());
      break;
    case ClockFace::Lenormand: {
      struct tm local = {};
      const bool valid = pm_time_valid();
      if (valid) {
        pm_time_local(&local);
      }
      const int lenormand_idx = pm_face_lenormand_index(&local, valid);
      snprintf(msg, msg_cap,
               "Face: lenormand. Active card: %02d %s. Keyword: %s. Give a concise Lenormand reading "
               "for the watch face: practical signal, near-term counsel, and one plain image. Keep it "
               "reflective, not deterministic.",
               lenormand_idx + 1, pm_face_lenormand_title(lenormand_idx),
               pm_face_lenormand_keyword(lenormand_idx));
      break;
    }
    case ClockFace::Geomancy: {
      struct tm local = {};
      const bool valid = pm_time_valid();
      if (valid) {
        pm_time_local(&local);
      }
      const int geomancy_idx = pm_face_geomancy_index(&local, valid);
      snprintf(msg, msg_cap,
               "Face: geomancy. Active figure: %s. Keyword: %s. Give a concise geomantic reading for the "
               "watch face: practical signal, present tension, and one grounded image. Keep it reflective, "
               "not deterministic.",
               pm_face_geomancy_title(geomancy_idx), pm_face_geomancy_keyword(geomancy_idx));
      break;
    }
    case ClockFace::Pythia:
      snprintf(s_face_voice_face, sizeof(s_face_voice_face), "pythia");
      if (!pm_face_pythia_build_system_prompt(sys, sys_cap)) {
        snprintf(g_gesture_banner, sizeof(g_gesture_banner), "pythia: prompt fail");
        return false;
      }
      snprintf(msg, msg_cap,
               "%sThe asker stands before the Pythia face but has not spoken a specific question. Give a "
               "brief Delphic omen inviting a better question.",
               tour_test ? "Tour-test the Pythia TTS button. " : "");
      break;
    case ClockFace::BabelFish:
      snprintf(s_face_voice_face, sizeof(s_face_voice_face), "babel_fish");
      if (!pm_face_babel_fish_build_system_prompt(sys, sys_cap)) {
        snprintf(g_gesture_banner, sizeof(g_gesture_banner), "babel: prompt fail");
        return false;
      }
      snprintf(msg, msg_cap,
               "%sThe Babel Fish face is ready, but no spoken phrase was captured. Say a short phrase in "
               "another language, or say a native-language phrase and name the target language.",
               tour_test ? "Tour-test the Babel Fish TTS button. " : "");
      break;
    case ClockFace::EnochianAngel:
      snprintf(s_face_voice_face, sizeof(s_face_voice_face), "enochian");
      if (!pm_face_enochian_angel_build_system_prompt(sys, sys_cap)) {
        snprintf(g_gesture_banner, sizeof(g_gesture_banner), "enochian: prompt fail");
        return false;
      }
      snprintf(msg, msg_cap,
               "%sFace: Enochian Angel. The display shows a luminous angelic visage, ordered stars, and "
               "tablet geometry. Give a concise symbolic reflection for the current threshold.",
               tour_test ? "Tour-test the Enochian Angel TTS button. " : "");
      break;
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
    case ClockFace::PitchPipe:
      snprintf(msg, msg_cap,
               "Face: pitch pipe. Selected pitch: %s. Current state: tap or breath starts a local reference tone. "
               "Give a short tuning cue.",
               pm_face_pitch_pipe_note_label());
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
    case ClockFace::Kalimba:
      snprintf(msg, msg_cap,
               "Face: kalimba. Active tine: %s. Current state: local pentatonic thumb-piano and MIDI "
               "instrument. Give a short plucked melodic cue.",
               pm_face_kalimba_note_label()[0] ? pm_face_kalimba_note_label() : "none");
      break;
    case ClockFace::Drone:
      snprintf(msg, msg_cap,
               "Face: drone. Active root: %s. Current state: tap toggles a sustained root, fifth, and octave "
               "MIDI drone. Give a short grounding cue.",
               pm_face_drone_label());
      break;
    case ClockFace::Chord:
      snprintf(msg, msg_cap,
               "Face: chord. Active chord: %s. Current state: local autoharp-style harmony pads and MIDI "
               "chord output. Give a short harmony cue.",
               pm_face_chord_label()[0] ? pm_face_chord_label() : "none");
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
      snprintf(sys, sys_cap,
               "You are the Mynah Astrolabe level face TTS button. Say exactly the supplied leveling nudge, "
               "with no preamble and no extra words. The top of the display is forward.");
      snprintf(msg, msg_cap, "Leveling nudge to speak exactly: %s.", guidance);
      break;
    }
    case ClockFace::Alethiometer:
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
      break;
    case ClockFace::Runes:
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
      break;
    case ClockFace::QuestionOfDay:
      snprintf(s_face_voice_face, sizeof(s_face_voice_face), "question_of_day");
      snprintf(msg, msg_cap,
               "Face: Question of the Day. Current question: %.180s. Castalia session: %s. Explain that "
               "tap or BOOT fetches a fresh non-repeating question, and PWR hold records the answer to "
               "Commonplace.",
               pm_face_question_day_current()[0] ? pm_face_question_day_current() : "none yet",
               pm_castalia_has_session() ? "signed in" : "not signed in");
      break;
    default:
      snprintf(msg, msg_cap, "Face: %s. Purpose: %s. Current state: %s. Speak one concise useful note.",
               info->name, info->summary, health);
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
  if (!pm_wifi_connected()) {
    Serial.printf("tour: %s skipped %d %s reason=no wifi\n", s_face_tour_button_test ? "tts" : "narrate", idx,
                  info->name);
    ++s_face_tour_tts_skip;
    return;
  }
  if (info->face == ClockFace::Radar) {
    pm_presence_ble_set_suppressed(true);
    for (uint8_t i = 0; i < 8; ++i) {
      pm_presence_tick(millis());
      pm_presence_ble_end();
      delay(50);
    }
    Serial.printf("tour: radar BLE paused for TTS heap=%u largest=%u\n",
                  static_cast<unsigned>(pm_heap_internal_free()),
                  static_cast<unsigned>(pm_heap_internal_largest()));
  }
  if (pm_speaker_is_playing()) {
    return;
  }
  face_tour_voice_reset();
  bool started = false;
  if (s_face_tour_button_test) {
    if (!face_voice_build_prompt(info, s_face_tour_voice_msg, kFaceTourVoiceMsgCap,
                                 s_face_tour_sys_prompt, kFaceTourSysPromptCap, true)) {
      Serial.printf("tour: tts skipped %d %s reason=%s\n", idx, info->name, g_gesture_banner);
      ++s_face_tour_tts_skip;
      return;
    }
    started = pm_voice_begin_message_ex(s_face_tour_voice_msg, s_face_tour_sys_prompt, s_face_voice_face,
                                        s_face_voice_faculty_slug, s_face_voice_faculty_name,
                                        &s_face_tour_voice_result);
  } else {
    const char *health = face_tour_health_text(info);
    snprintf(s_face_tour_voice_msg, kFaceTourVoiceMsgCap,
             "Astrolabe tour face %d of %d: %s. It is %s. Say this aloud in one concise sentence, no preamble.",
             idx + 1, face_tour_count(), info->summary, health);
    started = pm_voice_begin_message(s_face_tour_voice_msg,
                                     "You narrate a tiny smartwatch face tour. Be warm, concrete, and brief. "
                                     "Do not mention implementation details unless the face has a warning.",
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

static void face_tour_select(int idx) {
  const FaceTourInfo *info = face_tour_info(idx);
  if (!info) {
    return;
  }
  if (info->face == ClockFace::Settings) {
    pm_settings_set_page(SettingsPage::WiFi);
  }
  pm_presence_ble_set_suppressed((s_face_tour_narrate || s_face_tour_button_test) &&
                                 info->face == ClockFace::Radar);
  pm_faces_set(info->face);
  snprintf(g_gesture_banner, sizeof(g_gesture_banner), "tour: %.28s", info->name);
  g_clock_repaint_pending = true;
  const char *health = face_tour_health_text(info);
  Serial.printf("tour: loading %d %s heap=%u largest=%u psram=%u health=%s - %s\n", idx, info->name,
                static_cast<unsigned>(pm_heap_internal_free()), static_cast<unsigned>(pm_heap_internal_largest()),
                static_cast<unsigned>(pm_heap_psram_free()), health, info->summary);
  face_tour_voice_start(info, idx);
}

static void face_tour_start(uint32_t dwell_ms, bool narrate = false, bool button_test = false) {
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
  s_face_tour_narrate = narrate;
  s_face_tour_button_test = button_test;
  s_face_tour_tts_ok = 0;
  s_face_tour_tts_fail = 0;
  s_face_tour_tts_skip = 0;
  s_face_tour_idx = 0;
  s_face_tour_dwell_ms = dwell_ms;
  s_face_tour_last_ms = 0;
  Serial.printf("tour: start faces=%d dwell_ms=%u narrate=%d tts=%d wifi=%d time=%d\n",
                face_tour_count(), static_cast<unsigned>(s_face_tour_dwell_ms),
                s_face_tour_narrate ? 1 : 0, s_face_tour_button_test ? 1 : 0, pm_wifi_connected() ? 1 : 0,
                pm_time_valid() ? 1 : 0);
  face_tour_select(s_face_tour_idx);
}

static void face_tour_stop(void) {
  if (!s_face_tour_active) {
    Serial.println("tour: stopped");
    return;
  }
  s_face_tour_active = false;
  s_face_tour_narrate = false;
  s_face_tour_button_test = false;
  s_face_tour_idx = 0;
  s_face_tour_last_ms = 0;
  pm_presence_ble_set_suppressed(false);
  face_tour_voice_reset();
  g_gesture_banner[0] = '\0';
  g_clock_repaint_pending = true;
  Serial.println("tour: stopped");
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
  const FaceTourInfo *info = face_tour_info_for_face(pm_faces_current());
  if (!info) {
    snprintf(g_gesture_banner, sizeof(g_gesture_banner), "voice: no face prompt");
    return false;
  }
  if (!face_voice_build_prompt(info, s_face_tour_voice_msg, kFaceTourVoiceMsgCap,
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
  g_question_voice_active = false;
  g_question_answer_pcm = false;
  g_voice_play_reset = true;
  g_state = AppState::kThinking;
  return true;
}

static bool remote_tts_begin(const char *text, const char *face, const char *faculty_slug,
                             const char *faculty_name, const char *tts_voice) {
  if (!text || text[0] == '\0') {
    snprintf(g_gesture_banner, sizeof(g_gesture_banner), "remote: no text");
    return false;
  }
  if (!voice_prompt_buffers_ensure()) {
    snprintf(g_gesture_banner, sizeof(g_gesture_banner), "remote: PSRAM OOM");
    return false;
  }
  if (!pm_wifi_connected()) {
    snprintf(g_gesture_banner, sizeof(g_gesture_banner), "remote: need WiFi");
    return false;
  }
  if (g_state != AppState::kClock) {
    gesture_end_voice_ui();
  }
  const bool babel_face = face && (strcasecmp(face, "babel_fish") == 0 ||
                                   strcasecmp(face, "babel-fish") == 0 ||
                                   strcasecmp(face, "babel") == 0);
  strlcpy(s_face_tour_voice_msg, text, kFaceTourVoiceMsgCap);
  if (babel_face) {
    if (!pm_face_babel_fish_build_system_prompt(s_face_tour_sys_prompt, kFaceTourSysPromptCap)) {
      snprintf(g_gesture_banner, sizeof(g_gesture_banner), "remote: babel prompt fail");
      return false;
    }
  } else {
    snprintf(s_face_tour_sys_prompt, kFaceTourSysPromptCap,
             "You are speaking as an Astrolabe device in a synchronized presentation tour. "
             "Say exactly the user's supplied message unless a tiny verbal cleanup is needed for speech. "
             "Do not add preamble, extra commentary, or implementation details.");
  }
  strlcpy(s_face_voice_face, babel_face ? "babel_fish" : (face && face[0] ? face : "remote_tour"),
          sizeof(s_face_voice_face));
  strlcpy(s_face_voice_faculty_slug, faculty_slug && faculty_slug[0] ? faculty_slug : "",
          sizeof(s_face_voice_faculty_slug));
  strlcpy(s_face_voice_faculty_name, faculty_name && faculty_name[0] ? faculty_name : "",
          sizeof(s_face_voice_faculty_name));
  strlcpy(s_face_voice_tts_voice, tts_voice && tts_voice[0] ? tts_voice : "", sizeof(s_face_voice_tts_voice));
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
  g_question_voice_active = false;
  g_question_answer_pcm = false;
  g_voice_play_reset = true;
  g_state = AppState::kThinking;
  snprintf(g_gesture_banner, sizeof(g_gesture_banner), "remote: speaking");
  return true;
}

static void remote_control_process(uint32_t now) {
  if (s_remote_touch_clear_at != 0 && now - s_remote_touch_clear_at < 0x80000000u) {
    pm_touch_inject_clear();
    s_remote_touch_clear_at = 0;
  }
  if (s_remote_pwr_hold_clear_at != 0 && now - s_remote_pwr_hold_clear_at < 0x80000000u) {
    pm_side_buttons_inject_pek_hold(false);
    s_remote_pwr_hold_clear_at = 0;
  }

  PmRemoteCommand cmd;
  while (pm_remote_control_take(&cmd)) {
    switch (cmd.type) {
      case PmRemoteCommandType::Face: {
        s_face_tour_active = false;
        int idx = -1;
        char *end = nullptr;
        const long n = strtol(cmd.face, &end, 10);
        if (end != cmd.face && end && *end == '\0') {
          idx = static_cast<int>(n);
        } else if (face_index_from_name(cmd.face, &idx)) {
          /* ok */
        }
        if (idx >= 0 && idx < static_cast<int>(ClockFace::kNumFaces)) {
          if (g_state != AppState::kClock) {
            gesture_end_voice_ui();
          }
          pm_faces_set(static_cast<ClockFace>(idx));
          snprintf(g_gesture_banner, sizeof(g_gesture_banner), "remote face: %d", idx);
          g_clock_repaint_pending = true;
          Serial.printf("remote: face seq=%lu face=%d\n", static_cast<unsigned long>(cmd.seq), idx);
        } else {
          Serial.printf("remote: bad face seq=%lu value=%s\n", static_cast<unsigned long>(cmd.seq), cmd.face);
        }
        break;
      }
      case PmRemoteCommandType::Button:
        if (strcasecmp(cmd.button, "boot") == 0) {
          pm_side_buttons_inject(PM_SIDE_BTN_BOOT);
        } else if (strcasecmp(cmd.button, "pwr") == 0 || strcasecmp(cmd.button, "power") == 0) {
          pm_side_buttons_inject(PM_SIDE_BTN_PWR);
        } else if (strcasecmp(cmd.button, "pwr_hold") == 0 || strcasecmp(cmd.button, "ptt") == 0) {
          pm_side_buttons_inject_pek_hold(true);
          s_remote_pwr_hold_clear_at = now + (cmd.duration_ms ? cmd.duration_ms : 900u);
        }
        snprintf(g_gesture_banner, sizeof(g_gesture_banner), "remote button: %.20s", cmd.button);
        g_clock_repaint_pending = true;
        break;
      case PmRemoteCommandType::Tap:
        pm_touch_inject_set(cmd.x, cmd.y);
        s_remote_touch_clear_at = now + (cmd.duration_ms ? cmd.duration_ms : 120u);
        snprintf(g_gesture_banner, sizeof(g_gesture_banner), "remote tap %d,%d", cmd.x, cmd.y);
        break;
      case PmRemoteCommandType::TouchDown:
        pm_touch_inject_set(cmd.x, cmd.y);
        s_remote_touch_clear_at = 0;
        snprintf(g_gesture_banner, sizeof(g_gesture_banner), "remote touch");
        break;
      case PmRemoteCommandType::TouchUp:
        pm_touch_inject_clear();
        s_remote_touch_clear_at = 0;
        snprintf(g_gesture_banner, sizeof(g_gesture_banner), "remote touch up");
        break;
      case PmRemoteCommandType::Tts: {
        if (cmd.face[0]) {
          int idx = -1;
          if (face_index_from_name(cmd.face, &idx) && idx >= 0 && idx < static_cast<int>(ClockFace::kNumFaces)) {
            pm_faces_set(static_cast<ClockFace>(idx));
            g_clock_repaint_pending = true;
          }
        }
        (void)remote_tts_begin(cmd.text, cmd.face, cmd.faculty_slug, cmd.faculty_name, cmd.tts_voice);
        break;
      }
      case PmRemoteCommandType::Tour:
        if (strcasecmp(cmd.mode, "stop") == 0) {
          face_tour_stop();
        } else {
          const bool tts = strcasecmp(cmd.mode, "tts") == 0 || strcasecmp(cmd.mode, "button") == 0;
          const bool narrate = tts || strcasecmp(cmd.mode, "narrate") == 0 || strcasecmp(cmd.mode, "voice") == 0;
          face_tour_start(cmd.dwell_ms ? cmd.dwell_ms : (narrate ? 1200u : 2800u), narrate, tts);
        }
        break;
      case PmRemoteCommandType::Stop:
        face_tour_stop();
        gesture_end_voice_ui();
        pm_touch_inject_clear();
        pm_side_buttons_inject_pek_hold(false);
        s_remote_touch_clear_at = 0;
        s_remote_pwr_hold_clear_at = 0;
        snprintf(g_gesture_banner, sizeof(g_gesture_banner), "remote: stopped");
        g_clock_repaint_pending = true;
        break;
      case PmRemoteCommandType::None:
        break;
    }
  }
}

static void face_tour_tick(uint32_t now) {
  if (!s_face_tour_active || g_state != AppState::kClock) {
    return;
  }
  if (s_face_tour_voice_phase == 1) {
    const PmVoiceStatus vs = pm_voice_poll();
    if (vs == PmVoiceStatus::Working) {
      if (s_face_tour_voice_started_ms != 0 && static_cast<int32_t>(now - s_face_tour_voice_started_ms) < 0) {
        return;
      }
      if (s_face_tour_voice_started_ms != 0 && now - s_face_tour_voice_started_ms > 120000u) {
        Serial.printf("tour: narrate timeout %d err=%s\n", s_face_tour_idx, pm_voice_last_error());
        ++s_face_tour_tts_fail;
        face_tour_voice_reset();
      } else {
        return;
      }
    } else if (vs == PmVoiceStatus::DoneOk && s_face_tour_voice_result.mp3 &&
               s_face_tour_voice_result.mp3_len >= 64) {
      if (pm_speaker_play_begin(s_face_tour_voice_result.mp3, s_face_tour_voice_result.mp3_len)) {
        s_face_tour_voice_phase = 2;
        return;
      }
      Serial.printf("tour: narrate speaker busy %d\n", s_face_tour_idx);
      ++s_face_tour_tts_fail;
      face_tour_voice_reset();
    } else if (vs == PmVoiceStatus::DoneOk) {
      Serial.printf("tour: narrate no audio %d\n", s_face_tour_idx);
      ++s_face_tour_tts_fail;
      face_tour_voice_reset();
    } else if (vs == PmVoiceStatus::DoneFail) {
      Serial.printf("tour: narrate failed %d err=%s\n", s_face_tour_idx, pm_voice_last_error());
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
      ++s_face_tour_tts_fail;
      Serial.printf("tour: tts speaker failed %d\n", s_face_tour_idx);
    }
    face_tour_voice_reset();
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
  if (s_face_tour_idx >= face_tour_count()) {
    const bool report_tts_tour = s_face_tour_narrate || s_face_tour_button_test;
    s_face_tour_active = false;
    s_face_tour_narrate = false;
    s_face_tour_button_test = false;
    s_face_tour_idx = 0;
    pm_presence_ble_set_suppressed(false);
    face_tour_voice_reset();
    g_gesture_banner[0] = '\0';
    g_clock_repaint_pending = true;
    if (report_tts_tour) {
      Serial.printf("tour: summary tts_ok=%u tts_fail=%u tts_skip=%u\n",
                    static_cast<unsigned>(s_face_tour_tts_ok), static_cast<unsigned>(s_face_tour_tts_fail),
                    static_cast<unsigned>(s_face_tour_tts_skip));
      if (s_face_tour_tts_ok == static_cast<uint8_t>(face_tour_count()) && s_face_tour_tts_fail == 0 &&
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
#if defined(RESTART_BOOTLOADER_DFU) && defined(RESTART_BOOTLOADER)
  usb_persist_restart(dfu ? RESTART_BOOTLOADER_DFU : RESTART_BOOTLOADER);
#else
  (void)dfu;
  esp_restart();
#endif
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
                  static_cast<int>(WiFi.status()), pm_wifi_mdns_name(), pm_wifi_mac_string(), have ? ssid : "",
                  WiFi.localIP().toString().c_str(), pm_wifi_connected() ? static_cast<int>(WiFi.RSSI()) : 0,
                  have ? 1 : 0);
    return true;
  }
  if (strcmp(cmd, "scan") == 0) {
    WiFi.mode(WIFI_STA);
    const int n = WiFi.scanNetworks(false, true);
    Serial.printf("wifi: scan count=%d\n", n);
    for (int i = 0; i < n && i < 12; ++i) {
      Serial.printf("wifi: ap %d ssid=%s rssi=%d channel=%d enc=%d\n", i, WiFi.SSID(i).c_str(),
                    static_cast<int>(WiFi.RSSI(i)), static_cast<int>(WiFi.channel(i)),
                    static_cast<int>(WiFi.encryptionType(i)));
    }
    WiFi.scanDelete();
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
                  WiFi.localIP().toString().c_str());
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
                WiFi.localIP().toString().c_str());
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
                WiFi.localIP().toString().c_str());
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
      } else if (pm_remote_control_serial_command(line)) {
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
          Serial.printf("qa: face=%d state=%d heap=%u iheap=%u largest=%u psram=%u voice_stack_hw=%u spk_stack_hw=%u rocket_stack_hw=%u wifi=%d time=%d ip=%s name=%s banner=\"%s\"\n",
                        static_cast<int>(pm_faces_current()), static_cast<int>(g_state),
                        static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(pm_heap_internal_free()),
                        static_cast<unsigned>(pm_heap_internal_largest()), static_cast<unsigned>(pm_heap_psram_free()),
                        static_cast<unsigned>(pm_voice_stack_high_water()),
                        static_cast<unsigned>(pm_speaker_stack_high_water()),
                        static_cast<unsigned>(pm_rocket_fetch_stack_high_water()), pm_wifi_connected() ? 1 : 0,
                        pm_time_valid() ? 1 : 0, WiFi.localIP().toString().c_str(), pm_user_display_name(),
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
        } else if (strcmp(args, "time") == 0) {
          print_time_status("qa time");
        } else if (strcmp(args, "briefing") == 0 || strcmp(args, "brief") == 0) {
          Serial.printf("qa: briefing %s\n", home_begin_daily_briefing() ? "started" : "blocked");
        } else if (strcmp(args, "tone") == 0) {
          Serial.printf("qa: tone %s\n", pm_speaker_play_tone_begin(528.f, 1200u) ? "started" : "failed");
        } else if (strcmp(args, "bowl") == 0) {
          Serial.printf("qa: bowl %s\n", pm_speaker_bowl_voice_test(320.f, 1800u) ? "done" : "failed");
        } else if (strcmp(args, "faces") == 0) {
          Serial.printf("qa: faces=%d tour=%d\n", static_cast<int>(ClockFace::kNumFaces), face_tour_count());
          for (int i = 0; i < face_tour_count(); ++i) {
            const FaceTourInfo &info = k_face_tour[i];
            Serial.printf("qa: %d %s enum=%d\n", i, info.name, static_cast<int>(info.face));
          }
        } else if (strncmp(args, "tour", 4) == 0 && (args[4] == '\0' || args[4] == ' ')) {
          handle_tour_command(args + 4);
        } else if (!pm_qa_inject_command(args)) {
          Serial.println("qa: usage: status | heap | audio | time | briefing | tone | bowl | faces | tour [narrate|tts] [dwell_ms] | tour stop | inject …");
        }
      } else if (strncmp(line, "face ", 5) == 0) {
        s_face_tour_active = false;
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
      } else if (strncmp(line, "tour", 4) == 0 && (line[4] == '\0' || line[4] == ' ')) {
        handle_tour_command(line + 4);
      } else if ((strncmp(line, "say ", 4) == 0 || strncmp(line, "tts ", 4) == 0) && line[4] != '\0') {
        const char *text = line + 4;
        while (*text == ' ') {
          ++text;
        }
        const char *face = pm_faces_current() == ClockFace::BabelFish ? "babel_fish" : "serial";
        const bool ok = remote_tts_begin(text, face, nullptr, nullptr, nullptr);
        Serial.printf("say: %s face=%s\n", ok ? "started" : g_gesture_banner, face);
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

static void boot_variant_palette(PmDeviceVariant variant, uint8_t *r, uint8_t *g, uint8_t *b) {
  switch (variant) {
    case PmDeviceVariant::Lunasay:
      *r = 112;
      *g = 162;
      *b = 238;
      break;
    case PmDeviceVariant::Ocarina:
      *r = 96;
      *g = 214;
      *b = 174;
      break;
    case PmDeviceVariant::Cameo:
      *r = 224;
      *g = 154;
      *b = 184;
      break;
    case PmDeviceVariant::Enso:
      *r = 238;
      *g = 214;
      *b = 138;
      break;
    case PmDeviceVariant::Luopan:
      *r = 224;
      *g = 92;
      *b = 76;
      break;
    case PmDeviceVariant::SmartSpeaker:
      *r = 29;
      *g = 185;
      *b = 84;
      break;
    case PmDeviceVariant::Astrolabe:
    case PmDeviceVariant::Pocket:
    default:
      *r = 202;
      *g = 168;
      *b = 76;
      break;
  }
}

static float boot_variant_chime_hz(PmDeviceVariant variant) {
  switch (variant) {
    case PmDeviceVariant::Lunasay:
      return 432.f;
    case PmDeviceVariant::Ocarina:
      return 528.f;
    case PmDeviceVariant::Cameo:
      return 396.f;
    case PmDeviceVariant::Enso:
      return 639.f;
    case PmDeviceVariant::Luopan:
      return 288.f;
    case PmDeviceVariant::SmartSpeaker:
      return 440.f;
    case PmDeviceVariant::Astrolabe:
    case PmDeviceVariant::Pocket:
    default:
      return 480.f;
  }
}

static void draw_boot_variant_mark(PmDeviceVariant variant, int frame, uint16_t accent, uint16_t dim) {
  const int cx = LCD_WIDTH / 2;
  const int cy = LCD_HEIGHT / 2;
  const float t = static_cast<float>(frame) / 5.f;
  const int pulse = static_cast<int>(10.f * sinf(t * pm_face_k_two_pi));

  switch (variant) {
    case PmDeviceVariant::Lunasay:
      gfx->fillCircle(cx + 18, cy - 20, 78 + pulse, accent);
      gfx->fillCircle(cx + 44, cy - 32, 82 + pulse, RGB565_BLACK);
      gfx->drawCircle(cx + 18, cy - 20, 104 + frame * 3, dim);
      break;
    case PmDeviceVariant::Ocarina:
      gfx->drawRoundRect(cx - 104, cy - 42, 208, 84, 42, accent);
      gfx->fillCircle(cx - 54, cy, 15 + frame, dim);
      gfx->fillCircle(cx - 12, cy - 18, 12 + frame / 2, accent);
      gfx->fillCircle(cx + 34, cy + 10, 11 + frame / 2, accent);
      gfx->drawLine(cx + 84, cy - 18, cx + 128, cy - 42 - frame * 2, accent);
      gfx->drawLine(cx + 84, cy + 18, cx + 128, cy + 42 + frame * 2, accent);
      break;
    case PmDeviceVariant::Cameo:
      gfx->fillEllipse(cx, cy - 10, 58 + pulse / 2, 82 + pulse, dim);
      gfx->drawEllipse(cx, cy - 10, 78, 104, accent);
      gfx->fillCircle(cx - 18, cy - 28, 10, accent);
      gfx->drawLine(cx - 28, cy + 30, cx + 32, cy + 30, accent);
      break;
    case PmDeviceVariant::Enso:
      gfx->drawCircle(cx, cy - 8, 82 + pulse, accent);
      gfx->drawCircle(cx, cy - 8, 83 + pulse, accent);
      gfx->drawCircle(cx + 22, cy - 30, 12 + frame, dim);
      break;
    case PmDeviceVariant::Luopan:
      gfx->drawCircle(cx, cy - 6, 102, dim);
      gfx->drawCircle(cx, cy - 6, 74 + pulse, accent);
      for (int i = 0; i < 8; ++i) {
        const float a = (static_cast<float>(i) * 45.f + frame * 6.f) * (pm_face_k_pi / 180.f);
        gfx->drawLine(cx + static_cast<int>(sinf(a) * 42), cy - 6 - static_cast<int>(cosf(a) * 42),
                      cx + static_cast<int>(sinf(a) * 102), cy - 6 - static_cast<int>(cosf(a) * 102), dim);
      }
      gfx->fillTriangle(cx, cy - 92, cx - 12, cy - 8, cx + 12, cy - 8, accent);
      break;
    case PmDeviceVariant::SmartSpeaker:
      gfx->fillCircle(cx, cy - 8, 82 + pulse / 2, dim);
      gfx->fillCircle(cx, cy - 8, 42 + pulse / 3, RGB565_BLACK);
      gfx->drawCircle(cx, cy - 8, 116 + frame * 2, accent);
      gfx->drawCircle(cx, cy - 8, 92 + frame, accent);
      gfx->fillCircle(cx, cy - 8, 10 + frame, accent);
      for (int i = 0; i < 3; ++i) {
        const int y = cy - 58 + i * 56;
        gfx->drawLine(cx + 88, y, cx + 126 + frame * 4, y - 14 + i * 14, accent);
      }
      break;
    case PmDeviceVariant::Astrolabe:
    case PmDeviceVariant::Pocket:
    default:
      gfx->drawCircle(cx, cy - 8, 94 + pulse / 2, accent);
      gfx->drawCircle(cx, cy - 8, 56, dim);
      for (int i = 0; i < 12; ++i) {
        const float a = (static_cast<float>(i) * 30.f + frame * 4.f) * (pm_face_k_pi / 180.f);
        gfx->drawLine(cx + static_cast<int>(sinf(a) * 74), cy - 8 - static_cast<int>(cosf(a) * 74),
                      cx + static_cast<int>(sinf(a) * 94), cy - 8 - static_cast<int>(cosf(a) * 94), accent);
      }
      gfx->fillCircle(cx, cy - 8, 8 + frame, accent);
      break;
  }
}

static void boot_variant_splash(void) {
#ifdef ASTROLABE_QEMU
  return;
#else
  if (!gfx) {
    return;
  }
  pm_display_bind(gfx);
  const PmDeviceVariant variant = pm_variant_get();
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
  boot_variant_palette(variant, &r, &g, &b);
  const uint16_t accent = gfx->color565(r, g, b);
  const uint16_t dim = gfx->color565(r / 3, g / 3, b / 3);
  const char *label = pm_variant_label(variant);
  char platform[32];
  snprintf(platform, sizeof(platform), "Astrolabe %s", pm_variant_device_platform());

  for (int frame = 0; frame < 6; ++frame) {
    gfx->fillScreen(RGB565_BLACK);
    for (int ring = 0; ring < 4; ++ring) {
      gfx->drawCircle(LCD_WIDTH / 2, LCD_HEIGHT / 2, 120 + ring * 24 + frame * 2, dim);
    }
    draw_boot_variant_mark(variant, frame, accent, dim);
    gfx->setTextColor(accent);
    gfx->setTextSize(2);
    int16_t x1 = 0;
    int16_t y1 = 0;
    uint16_t w = 0;
    uint16_t h = 0;
    gfx->getTextBounds(label, 0, 0, &x1, &y1, &w, &h);
    gfx->setCursor((LCD_WIDTH - static_cast<int>(w)) / 2, 330);
    gfx->print(label);
    gfx->setTextColor(gfx->color565(154, 148, 134));
    gfx->setTextSize(1);
    gfx->getTextBounds(platform, 0, 0, &x1, &y1, &w, &h);
    gfx->setCursor((LCD_WIDTH - static_cast<int>(w)) / 2, 362);
    gfx->print(platform);
    gfx->flush();
    delay(72);
  }
#ifndef ASTROLABE_NO_ONBOARD_AUDIO
  (void)pm_speaker_play_tone_begin(boot_variant_chime_hz(variant), 260);
#endif
#endif
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

#if defined(ASTROLABE_USB_MIDI_ENABLED)
  if (pm_usb_midi_begin()) {
    pm_log_printf(false, "usb-midi: ocarina ready");
    Serial.println("USB MIDI ready (Astrolabe Ocarina MIDI)");
  } else {
    pm_log_printf(false, "usb-midi: unavailable");
    Serial.println("USB MIDI unavailable");
  }
#endif

#if defined(ASTROLABE_USB_HID_ENABLED)
  if (pm_usb_hid_begin()) {
    pm_log_printf(false, "usb-hid: touchpad ready");
    Serial.println("USB HID ready (Astrolabe HID Touchpad)");
  } else {
    pm_log_printf(false, "usb-hid: unavailable");
    Serial.println("USB HID unavailable");
  }
#endif

#ifdef ASTROLABE_QEMU
  pm_gesture_reset();
  pm_display_bind(nullptr);
  pm_power_begin(astrolabe_set_brightness);
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
  pm_power_begin(astrolabe_set_brightness);
  gfx->fillScreen(RGB565_BLACK);
  gfx->flush();

  (void)pm_touch_begin();
  pm_gesture_reset();
  (void)pm_side_buttons_begin();
  pm_birth_ensure_demo();
  pm_chart_profiles_ensure_demo_seed();
  pm_faculty_ensure_demo_seed();
  pm_home_gem_pulse_begin();
  pm_user_begin();
  pm_variant_begin();
  boot_variant_splash();
#ifndef ASTROLABE_NO_ONBOARD_AUDIO
  {
    const uint32_t deadline = millis() + 900u;
    while (pm_speaker_is_playing() && static_cast<int32_t>(millis() - deadline) < 0) {
      delay(10);
    }
    (void)pm_speaker_release_idle_task();
  }
#endif
  pm_faces_set(pm_variant_home_face());

  if (pm_wifi_begin()) {
#if defined(ASTROLABE_RTP_MIDI_ENABLED)
    if (pm_rtp_midi_begin()) {
      pm_log_printf(false, "rtpmidi: ready");
      Serial.println("RTP-MIDI ready (AppleMIDI network session)");
    } else {
      pm_log_printf(false, "rtpmidi: unavailable");
      Serial.println("RTP-MIDI unavailable");
    }
#endif
#if defined(ASTROLABE_USB_MIDI_ENABLED)
    pm_ntp_retry_if_stale();
#else
    pm_ntp_sync_blocking();
#endif
    pm_castalia_warmup_after_wifi();
  }
  pm_screen_http_begin(gfx);
  pm_faces_draw();

  ensure_pcm_buffer();

  pm_audio_route_begin();

  (void)pm_motion_begin();
  (void)pm_presence_begin();

  pm_log_printf(false, "boot: Mynah Astrolabe ready host=%s mac=%s ip=%s heap=%u largest=%u psram=%u",
                pm_wifi_mdns_name(), pm_wifi_mac_string(), WiFi.localIP().toString().c_str(), static_cast<unsigned>(pm_heap_internal_free()),
                static_cast<unsigned>(pm_heap_internal_largest()), static_cast<unsigned>(pm_heap_psram_free()));
  Serial.println("Mynah Astrolabe ready");
#endif
}

void loop() {
#ifndef ASTROLABE_QEMU
  pm_screen_http_loop();
  pm_wifi_poll();
#if defined(ASTROLABE_RTP_MIDI_ENABLED)
  pm_rtp_midi_tick();
#endif
#endif
  const uint32_t now = millis();
  remote_control_process(now);
  pm_gesture_poll(now);
  if (g_state == AppState::kClock && pm_faces_current() == ClockFace::Ocarina) {
    int16_t tx = 0;
    int16_t ty = 0;
    const bool down = pm_gesture_touch_point(&tx, &ty);
    if (pm_face_ocarina_touch_tick(tx, ty, down, now)) {
      snprintf(g_gesture_banner, sizeof(g_gesture_banner), "ocarina: fingering %d",
               pm_face_ocarina_selected_index() + 1);
      g_clock_repaint_pending = true;
    }
    if (pm_face_ocarina_breath_tick(now)) {
      snprintf(g_gesture_banner, sizeof(g_gesture_banner), "ocarina: breath note %d",
               pm_face_ocarina_note_index() + 1);
      g_clock_repaint_pending = true;
    }
  }
  if (g_state == AppState::kClock && pm_faces_current() == ClockFace::PitchPipe) {
    if (pm_face_pitch_pipe_breath_tick(now)) {
      snprintf(g_gesture_banner, sizeof(g_gesture_banner), "pitch: %s", pm_face_pitch_pipe_note_label());
      g_clock_repaint_pending = true;
    }
  }
  pm_presence_tick(now);
  poll_serial_birth_commands();
  face_tour_tick(now);
  handle_usb_audio_stream_event();
  const uint8_t side_ev = pm_side_buttons_poll(now);
  const bool qa_boot = (side_ev & PM_SIDE_BTN_BOOT) && pm_qa_consume_injected_boot();
  if (side_ev != 0 || pm_gesture_touch_down()) {
    pm_power_note_activity(now);
  }
  if (pm_power_tick(now)) {
    g_clock_repaint_pending = true;
  }
  release_noninstrument_speaker_task(now);

  if (g_state == AppState::kClock && pm_faces_current() == ClockFace::TibetanBowl) {
    if (pm_face_tibetan_bowl_touch_tick(now)) {
      g_clock_repaint_pending = true;
    }
  }
  if (g_state == AppState::kClock && pm_faces_current() == ClockFace::Sky) {
    if (pm_face_sky_touch_tick(now)) {
      pm_power_note_activity(now);
      g_clock_repaint_pending = true;
    }
  }
  if (g_state == AppState::kClock && pm_faces_current() == ClockFace::HidTouchpad) {
    if (pm_face_hid_touch_tick(now)) {
      pm_power_note_activity(now);
      g_clock_repaint_pending = true;
    }
  }

  PmGestureEvent ge;
  while (pm_gesture_consume(&ge)) {
    const bool qa_gesture = pm_qa_consume_injected_gesture();
    if (g_state == AppState::kClock && ge.kind == PmGestureKind::DoubleTap) {
      if (pm_faces_navigation_mode()) {
        pm_faces_set_navigation_mode(false);
        g_clock_repaint_pending = true;
        ge.kind = PmGestureKind::Tap;
      } else {
        pm_faces_set_navigation_mode(true);
        snprintf(g_gesture_banner, sizeof(g_gesture_banner), "navigation");
        g_clock_repaint_pending = true;
        continue;
      }
    }
    if (g_state == AppState::kClock) {
      pm_power_note_activity(now);
    }
    if (g_state == AppState::kClock && pm_faces_navigation_mode() &&
        gesture_is_navigation_swipe(ge.kind)) {
      if (pm_faces_current() == ClockFace::Settings && ge.kind == PmGestureKind::SwipeUp) {
        pm_faces_set_navigation_mode(false);
        pm_faces_set(pm_variant_home_face());
        g_gesture_banner[0] = '\0';
        g_clock_repaint_pending = false;
        if (pm_gfx) {
          pm_faces_draw();
        }
        continue;
      }
      const int delta = gesture_navigation_delta(ge.kind);
      (void)gesture_cycle_face(delta);
      pm_faces_set_navigation_mode(true);
      snprintf(g_gesture_banner, sizeof(g_gesture_banner), "navigation");
      continue;
    }
    if (g_state == AppState::kClock && pm_faces_is_commonplace_home() &&
        ge.kind == PmGestureKind::Tap) {
      if (qa_gesture) {
        snprintf(g_gesture_banner, sizeof(g_gesture_banner), "qa tap");
        g_clock_repaint_pending = true;
        continue;
      }
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
      if (ge.kind == PmGestureKind::Tap && pm_settings_page() == SettingsPage::Variant) {
        const PmDeviceVariant variant = pm_variant_cycle(1);
        snprintf(g_gesture_banner, sizeof(g_gesture_banner), "variant: %s", pm_variant_label(variant));
        g_clock_repaint_pending = false;
        if (pm_gfx) {
          pm_faces_draw();
        }
        continue;
      }
      if (ge.kind == PmGestureKind::Tap && pm_settings_page() == SettingsPage::Ota) {
        pm_screen_http_ota_arm(5u * 60u * 1000u);
        snprintf(g_gesture_banner, sizeof(g_gesture_banner), "ota: armed");
        g_clock_repaint_pending = false;
        if (pm_gfx) {
          pm_faces_draw();
        }
        continue;
      }
      if (ge.kind == PmGestureKind::Tap && pm_settings_page() == SettingsPage::Sleep) {
        pm_power_cycle_sleep_timeout(1);
        snprintf(g_gesture_banner, sizeof(g_gesture_banner), "sleep: timeout");
        g_clock_repaint_pending = false;
        if (pm_gfx) {
          pm_faces_draw();
        }
        continue;
      }
      if (ge.kind == PmGestureKind::LongPress && pm_settings_page() == SettingsPage::Sleep) {
        pm_power_toggle_enabled();
        snprintf(g_gesture_banner, sizeof(g_gesture_banner), "sleep: toggled");
        g_clock_repaint_pending = false;
        if (pm_gfx) {
          pm_faces_draw();
        }
        continue;
      }
      if (ge.kind == PmGestureKind::MultiFingerTap2 && pm_settings_page() == SettingsPage::Sleep) {
        pm_power_cycle_dim_timeout(1);
        snprintf(g_gesture_banner, sizeof(g_gesture_banner), "sleep: dim timeout");
        g_clock_repaint_pending = false;
        if (pm_gfx) {
          pm_faces_draw();
        }
        continue;
      }
      if (ge.kind == PmGestureKind::SwipeUp) {
        pm_faces_set(pm_variant_home_face());
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
                 pm_settings_page_label(pm_settings_page()));
        g_clock_repaint_pending = false;
        if (pm_gfx) {
          pm_faces_draw();
        }
        continue;
      }
    }
    if (g_state == AppState::kClock &&
        pm_audio_route_handle_gesture(ge.kind, pm_faces_current(), g_gesture_banner,
                                      sizeof(g_gesture_banner))) {
      if (pm_faces_current() == ClockFace::Spectrum) {
        pm_audio_analyzer_mic_end();
        (void)pm_audio_analyzer_mic_begin();
      }
      g_clock_repaint_pending = true;
      continue;
    } else if (g_state == AppState::kClock && pm_faces_current() == ClockFace::HidTouchpad) {
      (void)pm_face_hid_on_gesture(ge.kind, ge.x, ge.y, g_gesture_banner,
                                   sizeof(g_gesture_banner));
      g_clock_repaint_pending = true;
      continue;
    } else if (g_state == AppState::kClock && pm_faces_current() == ClockFace::FocusTimer &&
               ge.kind == PmGestureKind::Tap) {
      const bool running = pm_face_focus_toggle();
      snprintf(g_gesture_banner, sizeof(g_gesture_banner), "focus: %s", running ? "started" : "paused");
      g_clock_repaint_pending = true;
      continue;
    } else if (g_state == AppState::kClock && pm_faces_current() == ClockFace::FocusTimer &&
               ge.kind == PmGestureKind::LongPress) {
      pm_face_focus_reset();
      snprintf(g_gesture_banner, sizeof(g_gesture_banner), "focus: reset");
      g_clock_repaint_pending = true;
      continue;
    } else if (g_state == AppState::kClock && pm_faces_current() == ClockFace::FocusTimer &&
               (ge.kind == PmGestureKind::SwipeUp || ge.kind == PmGestureKind::SwipeDown)) {
      (void)pm_face_focus_cycle_mode(ge.kind == PmGestureKind::SwipeUp ? 1 : -1);
      snprintf(g_gesture_banner, sizeof(g_gesture_banner), "focus: %s", pm_face_focus_mode_label());
      g_clock_repaint_pending = true;
      continue;
    } else if (g_state == AppState::kClock && pm_faces_current() == ClockFace::Biometrics &&
               ge.kind == PmGestureKind::Tap) {
      (void)pm_face_biometrics_cycle_lens();
      g_gesture_banner[0] = '\0';
      g_clock_repaint_pending = true;
      continue;
    } else if (g_state == AppState::kClock && pm_faces_current() == ClockFace::Watcher &&
               ge.kind == PmGestureKind::Tap) {
      snprintf(g_gesture_banner, sizeof(g_gesture_banner), "watcher: %s", pm_face_watcher_cycle_mode());
      g_clock_repaint_pending = true;
      continue;
    } else if (g_state == AppState::kClock && pm_faces_current() == ClockFace::Luopan &&
               ge.kind == PmGestureKind::Tap) {
      pm_motion_zero_yaw();
      snprintf(g_gesture_banner, sizeof(g_gesture_banner), "luopan: north set");
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
      if (qa_gesture) {
        snprintf(g_gesture_banner, sizeof(g_gesture_banner), "qa tap");
        g_clock_repaint_pending = true;
        continue;
      }
      if (synastry_begin_boot_reading()) {
        g_gesture_banner[0] = '\0';
      }
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
    } else if (g_state == AppState::kClock && pm_faces_current() == ClockFace::Drone &&
               (ge.kind == PmGestureKind::SwipeUp || ge.kind == PmGestureKind::SwipeDown)) {
      pm_face_drone_cycle_root(ge.kind == PmGestureKind::SwipeUp ? 1 : -1);
      snprintf(g_gesture_banner, sizeof(g_gesture_banner), "drone: %s", pm_face_drone_label());
      g_clock_repaint_pending = true;
      continue;
    } else if (g_state == AppState::kClock && pm_faces_current() == ClockFace::PitchPipe &&
               (ge.kind == PmGestureKind::SwipeUp || ge.kind == PmGestureKind::SwipeDown)) {
      pm_face_pitch_pipe_cycle(ge.kind == PmGestureKind::SwipeUp ? 1 : -1);
      snprintf(g_gesture_banner, sizeof(g_gesture_banner), "pitch: %s", pm_face_pitch_pipe_note_label());
      g_clock_repaint_pending = true;
      continue;
    } else if (g_state == AppState::kClock && instrument_stack_swipe(ge.kind)) {
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
    } else if (g_state == AppState::kClock && pm_faces_current() == ClockFace::PitchPipe &&
               ge.kind == PmGestureKind::Tap) {
      if (pm_face_pitch_pipe_tap_at(ge.x, ge.y)) {
        snprintf(g_gesture_banner, sizeof(g_gesture_banner), "pitch: %s", pm_face_pitch_pipe_note_label());
      } else {
        snprintf(g_gesture_banner, sizeof(g_gesture_banner), "pitch: busy");
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
    } else if (g_state == AppState::kClock && pm_faces_current() == ClockFace::Kalimba &&
               ge.kind == PmGestureKind::Tap) {
      if (pm_face_kalimba_play_at(ge.x, ge.y)) {
        snprintf(g_gesture_banner, sizeof(g_gesture_banner), "kalimba: %s", pm_face_kalimba_note_label());
      } else {
        snprintf(g_gesture_banner, sizeof(g_gesture_banner), "kalimba: tine?");
      }
      g_clock_repaint_pending = true;
      continue;
    } else if (g_state == AppState::kClock && pm_faces_current() == ClockFace::Drone &&
               ge.kind == PmGestureKind::Tap) {
      if (pm_face_drone_toggle_at(ge.x, ge.y)) {
        snprintf(g_gesture_banner, sizeof(g_gesture_banner), "drone: %s", pm_face_drone_label());
      } else {
        snprintf(g_gesture_banner, sizeof(g_gesture_banner), "drone: busy");
      }
      g_clock_repaint_pending = true;
      continue;
    } else if (g_state == AppState::kClock && pm_faces_current() == ClockFace::Chord &&
               ge.kind == PmGestureKind::Tap) {
      if (pm_face_chord_play_at(ge.x, ge.y)) {
        snprintf(g_gesture_banner, sizeof(g_gesture_banner), "chord: %s", pm_face_chord_label());
      } else {
        snprintf(g_gesture_banner, sizeof(g_gesture_banner), "chord: pad?");
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
      if (pm_faculty_cycle_active(ge.kind == PmGestureKind::SwipeUp ? 1 : -1, &faculty)) {
        snprintf(g_gesture_banner, sizeof(g_gesture_banner), "faculty: %.25s", faculty.name);
      } else {
        snprintf(g_gesture_banner, sizeof(g_gesture_banner), "faculty: no recents");
      }
      g_clock_repaint_pending = true;
      continue;
    } else if (g_state == AppState::kClock && pm_faces_current() == ClockFace::Moon &&
               ge.kind == PmGestureKind::Tap) {
      if (qa_gesture) {
        snprintf(g_gesture_banner, sizeof(g_gesture_banner), "qa tap");
        g_clock_repaint_pending = true;
        continue;
      }
      if (moon_begin_fortune()) {
        g_gesture_banner[0] = '\0';
      }
      g_clock_repaint_pending = true;
    } else if (g_state == AppState::kClock && pm_faces_current() == ClockFace::Runes &&
               ge.kind == PmGestureKind::Tap) {
      if (qa_gesture) {
        snprintf(g_gesture_banner, sizeof(g_gesture_banner), "qa tap");
        g_clock_repaint_pending = true;
        continue;
      }
      if (runes_begin_fortune()) {
        g_gesture_banner[0] = '\0';
      }
      g_clock_repaint_pending = true;
    } else if (g_state == AppState::kClock && pm_faces_current() == ClockFace::QuestionOfDay &&
               ge.kind == PmGestureKind::Tap) {
      if (qa_gesture) {
        snprintf(g_gesture_banner, sizeof(g_gesture_banner), "qa tap");
        g_clock_repaint_pending = true;
        continue;
      }
      if (question_day_begin_fetch()) {
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
    } else if (g_state == AppState::kClock && pm_faces_current() == ClockFace::Lenormand &&
               (ge.kind == PmGestureKind::SwipeUp || ge.kind == PmGestureKind::SwipeDown)) {
      pm_face_lenormand_cycle(ge.kind == PmGestureKind::SwipeUp ? 1 : -1);
      struct tm local = {};
      const bool valid = pm_time_valid();
      if (valid) {
        pm_time_local(&local);
      }
      const int lenormand_idx = pm_face_lenormand_index(&local, valid);
      snprintf(g_gesture_banner, sizeof(g_gesture_banner), "len %02d %.24s", lenormand_idx + 1,
               pm_face_lenormand_title(lenormand_idx));
      g_clock_repaint_pending = true;
      continue;
    } else if (g_state == AppState::kClock && pm_faces_current() == ClockFace::Lenormand &&
               ge.kind == PmGestureKind::Tap) {
      pm_face_lenormand_reset_daily();
      snprintf(g_gesture_banner, sizeof(g_gesture_banner), "lenormand: daily");
      g_clock_repaint_pending = true;
      continue;
    } else if (g_state == AppState::kClock && pm_faces_current() == ClockFace::Geomancy &&
               (ge.kind == PmGestureKind::SwipeUp || ge.kind == PmGestureKind::SwipeDown)) {
      pm_face_geomancy_cycle(ge.kind == PmGestureKind::SwipeUp ? 1 : -1);
      struct tm local = {};
      const bool valid = pm_time_valid();
      if (valid) {
        pm_time_local(&local);
      }
      const int geomancy_idx = pm_face_geomancy_index(&local, valid);
      snprintf(g_gesture_banner, sizeof(g_gesture_banner), "geo %.24s",
               pm_face_geomancy_title(geomancy_idx));
      g_clock_repaint_pending = true;
      continue;
    } else if (g_state == AppState::kClock && pm_faces_current() == ClockFace::Geomancy &&
               ge.kind == PmGestureKind::Tap) {
      pm_face_geomancy_cast_entropy(ge.x, ge.y);
      struct tm local = {};
      const bool valid = pm_time_valid();
      if (valid) {
        pm_time_local(&local);
      }
      const int geomancy_idx = pm_face_geomancy_index(&local, valid);
      snprintf(g_gesture_banner, sizeof(g_gesture_banner), "cast %.22s",
               pm_face_geomancy_title(geomancy_idx));
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
      if (pm_face_rocket_has_stream()) {
        pm_face_rocket_toggle_stream_qr();
        snprintf(g_gesture_banner, sizeof(g_gesture_banner),
                 pm_face_rocket_stream_qr_visible() ? "launch: stream QR" : "launch: clock");
        g_gesture_banner[sizeof(g_gesture_banner) - 1] = '\0';
      } else {
        snprintf(g_gesture_banner, sizeof(g_gesture_banner), "launch: no stream");
      }
      g_clock_repaint_pending = true;
    } else if (ge.kind != PmGestureKind::SwipeUp && ge.kind != PmGestureKind::SwipeDown) {
      snprintf(g_gesture_banner, sizeof(g_gesture_banner), "%s", gesture_label(ge.kind));
      Serial.printf("[gesture] %s @ %d,%d\n", g_gesture_banner, static_cast<int>(ge.x), static_cast<int>(ge.y));
    }
  }

  if (g_state == AppState::kClock && pm_faces_current() == ClockFace::Astrology &&
      (side_ev & PM_SIDE_BTN_BOOT) != 0) {
    if (qa_boot) {
      snprintf(g_gesture_banner, sizeof(g_gesture_banner), "qa boot");
      g_clock_repaint_pending = true;
    } else if (!voice_last_play_begin()) {
      if (astrology_begin_boot_reading()) {
        g_gesture_banner[0] = '\0';
      }
      g_clock_repaint_pending = true;
    }
  }

  if (g_state == AppState::kClock && pm_faces_current() == ClockFace::Synastry &&
      (side_ev & PM_SIDE_BTN_BOOT) != 0) {
    if (qa_boot) {
      snprintf(g_gesture_banner, sizeof(g_gesture_banner), "qa boot");
      g_clock_repaint_pending = true;
    } else if (!voice_last_play_begin()) {
      if (synastry_begin_boot_reading()) {
        g_gesture_banner[0] = '\0';
      }
      g_clock_repaint_pending = true;
    }
  }

  if (g_state == AppState::kClock && pm_faces_current() == ClockFace::QuestionOfDay &&
      (side_ev & PM_SIDE_BTN_BOOT) != 0) {
    if (qa_boot) {
      snprintf(g_gesture_banner, sizeof(g_gesture_banner), "qa boot");
    } else if (question_day_begin_fetch()) {
      g_gesture_banner[0] = '\0';
    }
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
  if (g_state == AppState::kClock && (side_ev & PM_SIDE_BTN_BOOT) && pm_faces_voice_input_enabled() &&
      pm_faces_current() != ClockFace::Astrology && pm_faces_current() != ClockFace::Synastry &&
      pm_faces_current() != ClockFace::QuestionOfDay) {
    if (pm_faces_current() == ClockFace::Biometrics) {
      pm_face_biometrics_reveal();
      g_clock_repaint_pending = true;
    }
    if (qa_boot) {
      snprintf(g_gesture_banner, sizeof(g_gesture_banner), "qa boot");
      g_clock_repaint_pending = true;
    } else if (voice_last_play_begin()) {
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
        if (pm_faces_current() == ClockFace::Biometrics) {
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
        if (pm_faces_current() == ClockFace::QuestionOfDay) {
          PmFacultyProfile faculty = {};
          if (pm_face_question_day_faculty(&faculty)) {
            (void)pm_faculty_request_bust(faculty.slug);
          }
          g_clock_repaint_pending = true;
        }
        s_prev_dial_face = pm_faces_current();
      }

      static uint32_t s_last_spectrum_ms = 0;
      const bool spectrum_anim =
          pm_faces_current() == ClockFace::Spectrum && g_state == AppState::kClock &&
          (now - s_last_spectrum_ms >= 50u);
      if (spectrum_anim) {
        s_last_spectrum_ms = now;
        pm_face_spectrum_tick();
      }

      static uint32_t s_last_radar_ms = 0;
      const bool radar_anim =
          pm_faces_current() == ClockFace::Radar && g_state == AppState::kClock &&
          (now - s_last_radar_ms >= 80u);
      if (radar_anim) {
        s_last_radar_ms = now;
        pm_face_radar_tick(now);
      }

      static uint32_t s_last_biometrics_ms = 0;
      bool biometrics_anim = false;
      if (pm_faces_current() == ClockFace::Biometrics && g_state == AppState::kClock &&
          (now - s_last_biometrics_ms >= 80u)) {
        s_last_biometrics_ms = now;
        biometrics_anim = pm_face_biometrics_anim_tick(now);
      }

      static uint32_t s_last_level_ms = 0;
      const bool level_anim =
          pm_faces_current() == ClockFace::Level && g_state == AppState::kClock &&
          (now - s_last_level_ms >= 50u);
      if (level_anim) {
        s_last_level_ms = now;
        (void)pm_face_level_anim_tick(now);
      }

      static uint32_t s_last_orientation_ms = 0;
      const bool orientation_anim =
          (pm_faces_current() == ClockFace::Orientation || pm_faces_current() == ClockFace::Luopan) &&
          g_state == AppState::kClock && (now - s_last_orientation_ms >= 80u);
      if (orientation_anim) {
        s_last_orientation_ms = now;
        if (pm_faces_current() == ClockFace::Luopan) {
          pm_face_luopan_tick(now);
        } else {
          pm_face_orientation_tick(now);
        }
        g_clock_repaint_pending = true;
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
        pm_face_spotify_tick(now);
        if (pm_face_spotify_needs_repaint(now)) {
          g_clock_repaint_pending = true;
        }
      }
      if (pm_faces_current() != ClockFace::CalciferCountdown) {
        s_calcifer_have_data = false;
      }
      if (pm_faces_current() != ClockFace::Weather) {
        s_weather_have_data = false;
      }
      if (pm_faces_current() != ClockFace::Rocket) {
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
          pm_faces_current() != ClockFace::Rocket && pm_faces_current() != ClockFace::Radar &&
          pm_faces_current() != ClockFace::Biometrics;
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

      const bool chakra_anim =
          pm_faces_current() == ClockFace::Chakra && pm_face_chakra_anim_tick(now);
      const bool bowl_anim =
          pm_faces_current() == ClockFace::TibetanBowl && pm_face_tibetan_bowl_anim_tick(now);
      const bool ocarina_anim =
          pm_faces_current() == ClockFace::Ocarina && pm_face_ocarina_anim_tick(now);
      const bool pitch_pipe_anim =
          pm_faces_current() == ClockFace::PitchPipe && pm_face_pitch_pipe_anim_tick(now);
      const bool bongo_anim =
          pm_faces_current() == ClockFace::Bongo &&
          (pm_face_bongo_motion_tick(now) || pm_face_bongo_anim_tick(now));
      const bool piano_anim =
          pm_faces_current() == ClockFace::Piano && pm_face_piano_anim_tick(now);
      const bool kalimba_anim =
          pm_faces_current() == ClockFace::Kalimba && pm_face_kalimba_anim_tick(now);
      const bool drone_anim =
          pm_faces_current() == ClockFace::Drone && pm_face_drone_anim_tick(now);
      const bool chord_anim =
          pm_faces_current() == ClockFace::Chord && pm_face_chord_anim_tick(now);
      const bool pandrum_anim =
          pm_faces_current() == ClockFace::PanDrum &&
          (pm_face_pandrum_motion_tick(now) || pm_face_pandrum_anim_tick(now));
      const bool alethiometer_anim =
          pm_faces_current() == ClockFace::Alethiometer && pm_face_alethiometer_anim_tick(now);
      const bool faculty_anim =
          (pm_faces_current() == ClockFace::Faculty || pm_faces_current() == ClockFace::Quotes ||
           pm_faces_current() == ClockFace::QuestionOfDay) &&
          pm_faculty_tick(now);
      static uint32_t s_last_focus_paint_ms = 0;
      bool focus_anim = false;
      if (pm_faces_current() == ClockFace::FocusTimer && now - s_last_focus_paint_ms >= 250u) {
        s_last_focus_paint_ms = now;
        focus_anim = true;
      }
      const bool home_gem_breath =
          pm_faces_current() == ClockFace::ClassicAnalog && pm_home_gem_pulse_enabled();
      const bool sec_tick_paint =
          sec_tick && !pm_faces_castalia_active() && pm_faces_current() != ClockFace::Settings &&
          pm_faces_current() != ClockFace::CalciferCountdown &&
          pm_faces_current() != ClockFace::Synastry && pm_faces_current() != ClockFace::Spectrum &&
          pm_faces_current() != ClockFace::Chakra && pm_faces_current() != ClockFace::TibetanBowl &&
          pm_faces_current() != ClockFace::Rocket && pm_faces_current() != ClockFace::Radar &&
          pm_faces_current() != ClockFace::Biometrics &&
          pm_faces_current() != ClockFace::Faculty && pm_faces_current() != ClockFace::Quotes &&
          pm_faces_current() != ClockFace::Globe && pm_faces_current() != ClockFace::Sky &&
          pm_faces_current() != ClockFace::LiveTransits && pm_faces_current() != ClockFace::Tarot &&
          pm_faces_current() != ClockFace::Lenormand &&
          pm_faces_current() != ClockFace::Ocarina && pm_faces_current() != ClockFace::PitchPipe &&
          pm_faces_current() != ClockFace::Bongo &&
          pm_faces_current() != ClockFace::Piano && pm_faces_current() != ClockFace::Kalimba &&
          pm_faces_current() != ClockFace::Drone && pm_faces_current() != ClockFace::Chord &&
          pm_faces_current() != ClockFace::Level &&
          pm_faces_current() != ClockFace::PanDrum && pm_faces_current() != ClockFace::Alethiometer &&
          pm_faces_current() != ClockFace::Runes && pm_faces_current() != ClockFace::QuestionOfDay &&
          !home_gem_breath;
      const bool calcifer_sec =
          pm_faces_current() == ClockFace::CalciferCountdown && valid && sec_tick;
      const bool rocket_sec = pm_faces_current() == ClockFace::Rocket && valid && sec_tick;
      const bool globe_anim = pm_faces_current() == ClockFace::Globe && pm_face_globe_anim_tick(now);
      const bool sky_anim = pm_faces_current() == ClockFace::Sky && pm_face_sky_anim_tick(now);
      static uint32_t s_last_settings_status_ms = 0;
      bool settings_status_paint = false;
      if (pm_faces_current() == ClockFace::Settings &&
          (pm_settings_page() == SettingsPage::WiFi || pm_settings_page() == SettingsPage::Battery ||
           pm_settings_page() == SettingsPage::Sleep || pm_settings_page() == SettingsPage::Ota) &&
          now - s_last_settings_status_ms >= 1000u) {
        s_last_settings_status_ms = now;
        settings_status_paint = true;
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
                                 weather_stale || quotes_face_stale || quotes_preload_due || rocket_stale || sec_tick_paint || calcifer_sec || rocket_sec ||
                                 astro_repaint || spectrum_anim || chakra_anim || bowl_anim || ocarina_anim ||
                                 pitch_pipe_anim || bongo_anim ||
                                 piano_anim || kalimba_anim || drone_anim || chord_anim || pandrum_anim ||
                                 alethiometer_anim || radar_anim || biometrics_anim ||
                                 level_anim || faculty_anim || focus_anim || globe_anim || sky_anim || settings_status_paint;
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
            pm_faces_current() != ClockFace::Radar && pm_faces_current() != ClockFace::Biometrics &&
            ESP.getFreeHeap() >= MYNAH_FACE_FETCH_MIN_HEAP) {
          (void)pm_quotes_fetch(&g_quotes_ui);
          s_last_quotes_poll_ms = now;
          s_quotes_have_data = true;
        }
        if (pm_faces_current() == ClockFace::Faculty || pm_faces_current() == ClockFace::Quotes ||
            pm_faces_current() == ClockFace::QuestionOfDay) {
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
            if (ESP.getFreeHeap() < MYNAH_ROCKET_MIN_FETCH_HEAP) {
              memset(&g_rocket_ui, 0, sizeof(g_rocket_ui));
              snprintf(g_rocket_ui.error, sizeof(g_rocket_ui.error), "low memory");
              pm_rocket_pad_image_release();
            } else {
              pm_face_rocket_reset_selection();
              (void)pm_rocket_request_fetch();
            }
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
        }
        if (!valid) {
          s_last_no_time_redraw = now;
        }
      }

      if (wifi && valid && !s_face_tour_active && s_clock_paint_inited && g_state == AppState::kClock &&
          now >= MYNAH_DAILY_BRIEFING_BOOT_GRACE_MS && now >= s_daily_brief_next_try_ms &&
          pm_daily_briefing_should_auto_play(&tm_now)) {
#if defined(ASTROLABE_FORCE_FACULTY_HOME) && ASTROLABE_FORCE_FACULTY_HOME
        PmFacultyProfile faculty_home = {};
        if (pm_faculty_active(&faculty_home) && !pm_faculty_bust_ready_for(faculty_home.slug)) {
          s_daily_brief_next_try_ms = now + 12000u;
        } else
#endif
        if (home_begin_daily_briefing()) {
          s_daily_brief_next_try_ms = now + 120000u;
        } else {
          s_daily_brief_next_try_ms = now + 120000u;
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
          g_question_voice_active = false;
          g_question_answer_pcm = false;
        } else if (pm_faces_current() == ClockFace::Pythia) {
          if (!pm_wifi_connected()) {
            snprintf(g_gesture_banner, sizeof(g_gesture_banner), "pythia: need WiFi");
            g_clock_repaint_pending = true;
            break;
          }
          g_commonplace_journal = false;
          g_commonplace_note_face = false;
          g_astro_voice_active = false;
          g_astro_voice_pcm = false;
          g_synastry_voice_active = false;
          g_synastry_voice_pcm = false;
          g_alethiometer_voice_active = false;
          g_alethiometer_voice_pcm = false;
          g_pythia_voice_active = true;
          g_pythia_voice_pcm = true;
          g_moon_voice_pcm = false;
          g_question_voice_active = false;
          g_question_answer_pcm = false;
        } else if (pm_faces_current() == ClockFace::BabelFish) {
          if (!pm_wifi_connected()) {
            snprintf(g_gesture_banner, sizeof(g_gesture_banner), "babel: need WiFi");
            g_clock_repaint_pending = true;
            break;
          }
          g_commonplace_journal = false;
          g_commonplace_note_face = false;
          g_astro_voice_active = false;
          g_astro_voice_pcm = false;
          g_synastry_voice_active = false;
          g_synastry_voice_pcm = false;
          g_alethiometer_voice_active = false;
          g_alethiometer_voice_pcm = false;
          g_pythia_voice_active = false;
          g_pythia_voice_pcm = false;
          g_babel_fish_voice_active = true;
          g_babel_fish_voice_pcm = true;
          g_moon_voice_pcm = false;
          g_question_voice_active = false;
          g_question_answer_pcm = false;
        } else if (pm_faces_current() == ClockFace::QuestionOfDay) {
          if (!pm_wifi_connected()) {
            snprintf(g_gesture_banner, sizeof(g_gesture_banner), "question: need WiFi");
            g_clock_repaint_pending = true;
            break;
          }
          if (!pm_castalia_has_session()) {
            snprintf(g_gesture_banner, sizeof(g_gesture_banner), "question: sign in");
            g_clock_repaint_pending = true;
            break;
          }
          if (!pm_face_question_day_current()[0]) {
            snprintf(g_gesture_banner, sizeof(g_gesture_banner), "question: tap first");
            g_clock_repaint_pending = true;
            break;
          }
          g_commonplace_journal = false;
          g_commonplace_note_face = false;
          g_astro_voice_active = false;
          g_astro_voice_pcm = false;
          g_synastry_voice_active = false;
          g_synastry_voice_pcm = false;
          g_alethiometer_voice_active = false;
          g_alethiometer_voice_pcm = false;
          g_moon_voice_pcm = false;
          g_question_voice_active = true;
          g_question_answer_pcm = true;
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
          g_pythia_voice_active = false;
          g_pythia_voice_pcm = false;
          g_babel_fish_voice_active = false;
          g_babel_fish_voice_pcm = false;
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
          g_pythia_voice_active = false;
          g_pythia_voice_pcm = false;
          g_babel_fish_voice_active = false;
          g_babel_fish_voice_pcm = false;
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
          g_pythia_voice_active = false;
          g_pythia_voice_pcm = false;
          g_babel_fish_voice_active = false;
          g_babel_fish_voice_pcm = false;
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
      int16_t raw[512 * 2];
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
        g_babel_fish_voice_active = false;
        g_babel_fish_voice_pcm = false;
        g_question_voice_active = false;
        g_question_answer_pcm = false;
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
          int analyzer_channels = pm_mic_i2s_channels();
          if (analyzer_channels > PM_AUDIO_ANALYZER_IN_CHANNELS) {
            analyzer_channels = PM_AUDIO_ANALYZER_IN_CHANNELS;
          }
          for (int ch = 0; ch < analyzer_channels; ++ch) {
            pm_mic_pick_channel(raw, ns, ch, frame);
            pm_audio_analyzer_feed_in_channel(ch, frame, ns);
          }
          pm_mic_pick_channel(raw, ns, 0, frame);
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
      } else if (g_pythia_voice_active) {
        pm_face_pythia_draw_voice_screen("listening");
      } else if (g_babel_fish_voice_active) {
        pm_face_babel_fish_draw_voice_screen("listening");
      } else if (g_question_voice_active) {
        const float progress =
            static_cast<float>(g_pcm_len) / static_cast<float>(MYNAH_VOICE_MAX_PCM_BYTES);
        const uint32_t elapsed = s_thinking_t0_ms == 0 ? 0u : (now - s_thinking_t0_ms);
        pm_face_question_day_draw_recording(progress, elapsed);
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
        g_question_voice_active = false;
        g_babel_fish_voice_active = false;
        g_babel_fish_voice_pcm = false;
        g_question_answer_pcm = false;
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
        } else if (g_text_voice_route == k_tv_question) {
          started = pm_voice_begin_message_ex(g_question_voice_msg, g_question_sys_prompt, "question_of_day",
                                              s_face_voice_faculty_slug, s_face_voice_faculty_name,
                                              &g_voice_result);
        } else if (g_text_voice_route == k_tv_synastry) {
          started = pm_voice_begin_message(kSynastryBootUserMsg, g_synastry_sys_prompt, &g_voice_result);
        } else if (g_text_voice_route == k_tv_face) {
          started = pm_voice_begin_message_voice(s_face_tour_voice_msg, s_face_tour_sys_prompt, s_face_voice_face,
                                                 s_face_voice_faculty_slug, s_face_voice_faculty_name,
                                                 s_face_voice_tts_voice, &g_voice_result);
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
          } else if (g_pythia_voice_active) {
            if (!pm_face_pythia_build_system_prompt(s_face_tour_sys_prompt, kFaceTourSysPromptCap)) {
              pm_face_pythia_draw_voice_screen("prompt fail");
              delay(1200);
              g_pythia_voice_active = false;
              g_pythia_voice_pcm = false;
              g_state = AppState::kClock;
              g_clock_repaint_pending = true;
              break;
            }
            sys = s_face_tour_sys_prompt;
          } else if (g_babel_fish_voice_active) {
            if (!pm_face_babel_fish_build_system_prompt(s_face_tour_sys_prompt, kFaceTourSysPromptCap)) {
              pm_face_babel_fish_draw_voice_screen("prompt fail");
              delay(1200);
              g_babel_fish_voice_active = false;
              g_babel_fish_voice_pcm = false;
              g_state = AppState::kClock;
              g_clock_repaint_pending = true;
              break;
            }
            sys = s_face_tour_sys_prompt;
          } else if (g_question_voice_active && g_question_answer_pcm) {
            if (!pm_face_question_day_build_answer_system_prompt(g_question_sys_prompt,
                                                                 kQuestionSysPromptCap)) {
              pm_face_question_day_draw_voice_screen("question fail");
              delay(1200);
              g_question_voice_active = false;
              g_question_answer_pcm = false;
              g_state = AppState::kClock;
              g_clock_repaint_pending = true;
              break;
            }
            sys = g_question_sys_prompt;
          }
          if (g_question_voice_active && g_question_answer_pcm) {
            started = pm_voice_begin_pcm_ex(g_pcm, g_pcm_len, sys, "question_of_day", &g_voice_result);
          } else if (g_pythia_voice_active) {
            started = pm_voice_begin_pcm_ex(g_pcm, g_pcm_len, sys, "pythia", &g_voice_result);
          } else if (g_babel_fish_voice_active) {
            started = pm_voice_begin_pcm_ex(g_pcm, g_pcm_len, sys, "babel_fish", &g_voice_result);
          } else if (pm_faces_current() == ClockFace::Faculty) {
            started = pm_voice_begin_pcm_ex(g_pcm, g_pcm_len, sys, "faculty", &g_voice_result);
          } else {
            started = pm_voice_begin_pcm(g_pcm, g_pcm_len, sys, &g_voice_result);
          }
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
          } else if (g_pythia_voice_active) {
            pm_face_pythia_draw_voice_screen("voice start fail");
          } else if (g_babel_fish_voice_active) {
            pm_face_babel_fish_draw_voice_screen("voice start fail");
          } else if (g_runes_fortune_active) {
            pm_face_runes_draw_voice_screen("voice start fail", -1.f);
          } else if (g_question_voice_active) {
            pm_face_question_day_draw_voice_screen("voice start fail");
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
          g_pythia_voice_active = false;
          g_pythia_voice_pcm = false;
          g_babel_fish_voice_active = false;
          g_babel_fish_voice_pcm = false;
          g_runes_fortune_active = false;
          g_question_voice_active = false;
          g_question_answer_pcm = false;
          g_moon_voice_pcm = false;
          g_state = AppState::kClock;
          g_clock_repaint_pending = true;
          break;
        }
        thinking_progress_begin(g_daily_briefing ? 680000u
                                                : ((g_astro_voice_active || g_synastry_voice_active ||
                                                    g_alethiometer_voice_active || g_runes_fortune_active ||
                                                    g_pythia_voice_active || g_babel_fish_voice_active ||
                                                    g_question_voice_active)
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
      } else if (g_pythia_voice_active) {
        pm_face_pythia_draw_voice_screen(nullptr, thinking_progress_now());
      } else if (g_babel_fish_voice_active) {
        pm_face_babel_fish_draw_voice_screen(nullptr, thinking_progress_now());
      } else if (g_astro_voice_active) {
        pm_face_astrology_draw_voice_screen(nullptr, -1, -1, false, thinking_progress_now());
      } else if (g_moon_fortune_active) {
        pm_face_moon_draw_voice_screen(nullptr, thinking_progress_now());
      } else if (g_runes_fortune_active) {
        pm_face_runes_draw_voice_screen(nullptr, thinking_progress_now());
      } else if (g_question_voice_active) {
        pm_face_question_day_draw_voice_screen(g_question_answer_pcm ? "saving answer" : "choosing question",
                                               thinking_progress_now());
      } else {
        pm_faces_draw(thinking_progress_now());
      }
      const PmVoiceStatus vs = pm_voice_poll();
      if (vs == PmVoiceStatus::Working) {
        const uint32_t voice_wait_ms =
            g_daily_briefing ? 680000u
                             : ((g_moon_fortune_active || g_astro_voice_active || g_synastry_voice_active ||
                                 g_alethiometer_voice_active || g_runes_fortune_active ||
                                 g_pythia_voice_active || g_babel_fish_voice_active ||
                                 g_question_voice_active)
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
      } else if (g_pythia_voice_active) {
        Serial.printf("pythia: voice done status=%d mp3=%u err=%s\n", static_cast<int>(vs),
                      static_cast<unsigned>(g_voice_result.mp3_len),
                      vs == PmVoiceStatus::DoneOk ? "-" : pm_voice_last_error());
      } else if (g_babel_fish_voice_active) {
        Serial.printf("babel: voice done status=%d mp3=%u err=%s\n", static_cast<int>(vs),
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
        } else if (g_pythia_voice_active) {
          pm_face_pythia_draw_voice_screen(pm_voice_last_error());
        } else if (g_babel_fish_voice_active) {
          pm_face_babel_fish_draw_voice_screen(pm_voice_last_error());
        } else if (g_astro_voice_active) {
          pm_face_astrology_draw_voice_screen(pm_voice_last_error(), -1, -1, false);
        } else if (g_moon_fortune_active) {
          pm_face_moon_draw_voice_screen(pm_voice_last_error(), -1.f);
          delay(1500);
        } else if (g_runes_fortune_active) {
          pm_face_runes_draw_voice_screen(pm_voice_last_error(), -1.f);
          delay(1500);
        } else if (g_question_voice_active) {
          pm_face_question_day_draw_voice_screen(pm_voice_last_error());
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
        g_pythia_voice_active = false;
        g_pythia_voice_pcm = false;
        g_babel_fish_voice_active = false;
        g_babel_fish_voice_pcm = false;
        g_moon_fortune_active = false;
        g_runes_fortune_active = false;
        g_question_voice_active = false;
        g_question_answer_pcm = false;
        if (g_daily_briefing) {
          s_daily_brief_next_try_ms = now + 120000u;
        }
        g_daily_briefing = false;
        pm_speaker_set_max_play_seconds(180);
        g_state = AppState::kClock;
        g_clock_repaint_pending = true;
        break;
      }
      if (!g_voice_result.mp3 || g_voice_result.mp3_len < 64) {
        if (g_daily_briefing && pm_voice_daily_briefing_streamed()) {
          /* audio already played during HTTP download */
        } else if (g_daily_briefing) {
          gfx->fillScreen(RGB565_BLACK);
          pm_face_draw_centered_line("briefing: no audio", 220, RGB565_RED, 2, 2);
          gfx->flush();
          delay(1500);
          pm_voice_result_free(&g_voice_result);
          s_daily_brief_next_try_ms = now + 120000u;
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
        if (g_pythia_voice_active) {
          pm_face_pythia_draw_voice_screen("no audio reply");
          delay(1500);
          pm_voice_result_free(&g_voice_result);
          g_pythia_voice_active = false;
          g_pythia_voice_pcm = false;
          g_state = AppState::kClock;
          g_clock_repaint_pending = true;
          break;
        }
        if (g_babel_fish_voice_active) {
          pm_face_babel_fish_draw_voice_screen("no audio reply");
          delay(1500);
          pm_voice_result_free(&g_voice_result);
          g_babel_fish_voice_active = false;
          g_babel_fish_voice_pcm = false;
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
        if (g_question_voice_active && g_voice_result.reply[0] != '\0') {
          if (!g_question_answer_pcm) {
            (void)pm_face_question_day_set_current(g_voice_result.reply);
            PmFacultyProfile faculty = {};
            if (pm_face_question_day_faculty(&faculty)) {
              (void)pm_faculty_request_bust(faculty.slug);
              snprintf(s_face_voice_faculty_slug, sizeof(s_face_voice_faculty_slug), "%s", faculty.slug);
              snprintf(s_face_voice_faculty_name, sizeof(s_face_voice_faculty_name), "%s", faculty.name);
            }
          }
          snprintf(g_gesture_banner, sizeof(g_gesture_banner), "%s",
                   g_question_answer_pcm ? "answer saved" : "question ready");
          pm_voice_result_free(&g_voice_result);
          g_question_voice_active = false;
          g_question_answer_pcm = false;
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
      if (g_question_voice_active && !g_question_answer_pcm && g_voice_result.reply[0] != '\0') {
        (void)pm_face_question_day_set_current(g_voice_result.reply);
        PmFacultyProfile faculty = {};
        if (pm_face_question_day_faculty(&faculty)) {
          (void)pm_faculty_request_bust(faculty.slug);
          snprintf(s_face_voice_faculty_slug, sizeof(s_face_voice_faculty_slug), "%s", faculty.slug);
          snprintf(s_face_voice_faculty_name, sizeof(s_face_voice_faculty_name), "%s", faculty.name);
        }
      }
      faculty_remember_voice_result(g_voice_result);
      if (g_voice_result.mp3 && g_voice_result.mp3_len >= 64) {
        voice_last_play_save(g_voice_result.mp3, g_voice_result.mp3_len);
      }
      g_astro_voice_pcm = false;
      g_synastry_voice_pcm = false;
      g_alethiometer_voice_pcm = false;
      g_moon_voice_pcm = false;
      g_babel_fish_voice_pcm = false;
      g_voice_play_reset = true;
      if (g_daily_briefing && pm_voice_daily_briefing_streamed()) {
        pm_voice_result_free(&g_voice_result);
        g_daily_briefing = false;
        daily_briefing_mark_success();
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
      if (g_pythia_voice_active) {
        if (!s_play_armed) {
          if (!g_voice_result.mp3 || g_voice_result.mp3_len < 64) {
            pm_face_pythia_draw_voice_screen("no audio");
            delay(1200);
            pm_voice_result_free(&g_voice_result);
            g_pythia_voice_active = false;
            g_pythia_voice_pcm = false;
            g_state = AppState::kClock;
            g_clock_repaint_pending = true;
            break;
          }
          if (!pm_speaker_play_begin(g_voice_result.mp3, g_voice_result.mp3_len)) {
            pm_face_pythia_draw_voice_screen("speaker busy");
            delay(1200);
            pm_voice_result_free(&g_voice_result);
            g_pythia_voice_active = false;
            g_pythia_voice_pcm = false;
            g_state = AppState::kClock;
            g_clock_repaint_pending = true;
            break;
          }
          s_play_armed = true;
          s_play_wait_t0 = now;
        }
        pm_face_pythia_draw_voice_screen("speaking", -1.f, true);
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
          pm_face_pythia_draw_voice_screen("playback failed");
          delay(1200);
        }
        pm_voice_result_free(&g_voice_result);
        s_play_armed = false;
        s_play_wait_t0 = 0;
        g_pythia_voice_active = false;
        g_pythia_voice_pcm = false;
        g_state = AppState::kClock;
        g_clock_repaint_pending = true;
        break;
      }
      if (g_babel_fish_voice_active) {
        if (!s_play_armed) {
          if (!g_voice_result.mp3 || g_voice_result.mp3_len < 64) {
            pm_face_babel_fish_draw_voice_screen("no audio");
            delay(1200);
            pm_voice_result_free(&g_voice_result);
            g_babel_fish_voice_active = false;
            g_babel_fish_voice_pcm = false;
            g_state = AppState::kClock;
            g_clock_repaint_pending = true;
            break;
          }
          if (!pm_speaker_play_begin(g_voice_result.mp3, g_voice_result.mp3_len)) {
            pm_face_babel_fish_draw_voice_screen("speaker busy");
            delay(1200);
            pm_voice_result_free(&g_voice_result);
            g_babel_fish_voice_active = false;
            g_babel_fish_voice_pcm = false;
            g_state = AppState::kClock;
            g_clock_repaint_pending = true;
            break;
          }
          s_play_armed = true;
          s_play_wait_t0 = now;
        }
        pm_face_babel_fish_draw_voice_screen("speaking", -1.f, true);
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
          pm_face_babel_fish_draw_voice_screen("playback failed");
          delay(1200);
        }
        pm_voice_result_free(&g_voice_result);
        s_play_armed = false;
        s_play_wait_t0 = 0;
        g_babel_fish_voice_active = false;
        g_babel_fish_voice_pcm = false;
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
        const bool played_ok = spk != PmSpeakerStatus::DoneFail;
        if (!played_ok) {
          delay(800);
        }
        pm_voice_result_free(&g_voice_result);
        s_play_armed = false;
        s_play_wait_t0 = 0;
        g_daily_briefing = false;
        if (played_ok) {
          daily_briefing_mark_success();
        } else {
          s_daily_brief_next_try_ms = now + 120000u;
        }
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
      if (g_question_voice_active) {
        if (!s_play_armed) {
          if (!g_voice_result.mp3 || g_voice_result.mp3_len < 64) {
            pm_voice_result_free(&g_voice_result);
            g_question_voice_active = false;
            g_question_answer_pcm = false;
            g_state = AppState::kClock;
            g_clock_repaint_pending = true;
            break;
          }
          if (!pm_speaker_play_begin(g_voice_result.mp3, g_voice_result.mp3_len)) {
            pm_face_question_day_draw_voice_screen("speaker busy");
            delay(1200);
            pm_voice_result_free(&g_voice_result);
            g_question_voice_active = false;
            g_question_answer_pcm = false;
            g_state = AppState::kClock;
            g_clock_repaint_pending = true;
            break;
          }
          s_play_armed = true;
          s_play_wait_t0 = now;
        }
        pm_face_question_day_draw_voice_screen(g_question_answer_pcm ? "answer saved" : "question ready");
        PmSpeakerStatus spk = pm_speaker_poll();
        if (spk == PmSpeakerStatus::Playing) {
          const uint32_t est_ms =
              static_cast<uint32_t>((g_voice_result.mp3_len * 8u * 1000u) / 96000u) + 30000u;
          if (s_play_wait_t0 != 0 && (now - s_play_wait_t0) > est_ms) {
            pm_speaker_abort();
          } else {
            break;
          }
        }
        if (spk == PmSpeakerStatus::DoneFail) {
          pm_face_question_day_draw_voice_screen("playback failed");
          delay(1200);
        }
        pm_voice_result_free(&g_voice_result);
        s_play_armed = false;
        s_play_wait_t0 = 0;
        g_question_voice_active = false;
        g_question_answer_pcm = false;
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
