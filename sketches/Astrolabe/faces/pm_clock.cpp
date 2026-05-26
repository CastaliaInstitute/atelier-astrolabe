#include "faces/pm_faces.h"

#include <Arduino_GFX_Library.h>
#include <cmath>
#include <cstring>
#include <ctime>

#include "esp_heap_caps.h"
#include "faces/apocalypso/pm_face_apocalypso.h"
#include "faces/alethiometer/pm_face_alethiometer.h"
#include "faces/astrology/pm_face_astrology.h"
#include "faces/biometrics/pm_face_biometrics.h"
#include "faces/bongo/pm_face_bongo.h"
#include "faces/calcifer/pm_face_calcifer.h"
#include "faces/castalia/pm_face_castalia.h"
#include "faces/chakra/pm_face_chakra.h"
#include "faces/classic_analog/pm_face_classic_analog.h"
#include "faces/digital/pm_face_digital.h"
#include "faces/enochian_angel/pm_face_enochian_angel.h"
#include "faces/faculty/pm_face_faculty.h"
#include "faces/focus/pm_face_focus.h"
#include "faces/geomancy/pm_face_geomancy.h"
#include "faces/globe/pm_face_globe.h"
#include "faces/hid/pm_face_hid.h"
#include "faces/inq_card/pm_face_inq_card.h"
#include "faces/level/pm_face_level.h"
#include "faces/lenormand/pm_face_lenormand.h"
#include "faces/live_transits/pm_face_live_transits.h"
#include "faces/luopan/pm_face_luopan.h"
#include "faces/moon/pm_face_moon.h"
#include "faces/notes/pm_face_notes.h"
#include "faces/ocarina/pm_face_ocarina.h"
#include "faces/orientation/pm_face_orientation.h"
#include "faces/pandrum/pm_face_pandrum.h"
#include "faces/piano/pm_face_piano.h"
#include "faces/question_day/pm_face_question_day.h"
#include "faces/pythia/pm_face_pythia.h"
#include "faces/quotes/pm_face_quotes.h"
#include "faces/shared/pm_face_draw.h"
#include "faces/sky/pm_face_sky.h"
#include "faces/rocket/pm_face_rocket.h"
#include "faces/runes/pm_face_runes.h"
#include "faces/spotify/pm_face_spotify.h"
#include "faces/spectrum/pm_face_spectrum.h"
#include "faces/synastry/pm_face_synastry.h"
#include "faces/tarot/pm_face_tarot.h"
#include "faces/radar/pm_face_radar.h"
#include "faces/tibetan_bowl/pm_face_tibetan_bowl.h"
#include "faces/tuning/pm_face_tuning.h"
#include "faces/watcher/pm_face_watcher.h"
#include "faces/weather/pm_face_weather.h"
#include "pm_config.h"
#include "pm_display.h"
#include "pm_ephemeris.h"
#include "pm_faculty.h"
#include "pm_settings.h"
#include "pm_variant.h"
#include "pm_wifi_ntp.h"

extern char g_gesture_banner[44];

static bool pm_faces_skip_in_dial(ClockFace face) {
  return face == ClockFace::Castalia || face == ClockFace::Settings || !pm_variant_face_allowed(face);
}

static const ClockFace k_face_dial_order[] = {
    // Astrolabe / Pocket: orientation, time, presence, and daily rhythm.
    ClockFace::ClassicAnalog,
    ClockFace::DigitalLocal,
    ClockFace::Apocalypso,
    ClockFace::CalciferCountdown,
    ClockFace::Weather,
    ClockFace::Globe,
    ClockFace::Sky,
    ClockFace::Radar,
    ClockFace::HidTouchpad,
    ClockFace::Level,
    ClockFace::Rocket,

    // Lunasay: lunar, astrological, and divination surfaces.
    ClockFace::Moon,
    ClockFace::Astrology,
    ClockFace::LiveTransits,
    ClockFace::Synastry,
    ClockFace::Tarot,
    ClockFace::InqCard,
    ClockFace::Lenormand,
    ClockFace::Geomancy,
    ClockFace::Pythia,
    ClockFace::EnochianAngel,
    ClockFace::Alethiometer,

    // Enso: attention, readiness, and focus rituals.
    ClockFace::Biometrics,
    ClockFace::Watcher,
    ClockFace::FocusTimer,
    ClockFace::Chakra,
    ClockFace::TibetanBowl,
    ClockFace::Runes,

    // Ocarina: breath, music, and sound instruments.
    ClockFace::Ocarina,
    ClockFace::Tuning,
    ClockFace::Spectrum,
    ClockFace::Bongo,
    ClockFace::Piano,
    ClockFace::PanDrum,

    // Cameo: memory, identity, and companion context.
    ClockFace::Faculty,
    ClockFace::Quotes,
    ClockFace::Notes,
    ClockFace::QuestionOfDay,

    // Luopan: direction and feng-shui alignment.
    ClockFace::Orientation,
    ClockFace::Luopan,
};

#if defined(ASTROLABE_FORCE_VARIANT_ENSO)
static ClockFace s_clock_face = ClockFace::Biometrics;
#else
static ClockFace s_clock_face = ClockFace::ClassicAnalog;
#endif
static uint16_t s_clock_bg565 = 0;
static int s_analog_saved_local_h = -1;
static int s_analog_saved_local_m = -1;
static bool s_navigation_mode = false;

static void pm_faces_draw_navigation_zoom(void) {
  if (!pm_gfx) {
    return;
  }
  uint16_t *fb = pm_gfx->getFramebuffer();
  if (!fb) {
    return;
  }
  static uint16_t *s_zoom_src = nullptr;
  constexpr size_t kFbPixels = static_cast<size_t>(LCD_WIDTH) * LCD_HEIGHT;
  if (!s_zoom_src) {
    s_zoom_src = static_cast<uint16_t *>(
        heap_caps_malloc(kFbPixels * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  if (!s_zoom_src) {
    pm_gfx->drawCircle(pm_face_lcd_cx, pm_face_lcd_cy, 216, pm_gfx->color565(255, 220, 160));
    pm_face_draw_centered_line("nav", 28, pm_gfx->color565(255, 220, 160), 1, 1);
    return;
  }
  memcpy(s_zoom_src, fb, kFbPixels * sizeof(uint16_t));
  pm_gfx->fillScreen(0);

  constexpr int kDstW = 420;
  constexpr int kDstH = 420;
  constexpr int kOffX = (LCD_WIDTH - kDstW) / 2;
  constexpr int kOffY = (LCD_HEIGHT - kDstH) / 2;
  for (int y = 0; y < kDstH; ++y) {
    const int src_y = (y * LCD_HEIGHT) / kDstH;
    uint16_t *dst = fb + (kOffY + y) * LCD_WIDTH + kOffX;
    const uint16_t *src_row = s_zoom_src + src_y * LCD_WIDTH;
    for (int x = 0; x < kDstW; ++x) {
      const int src_x = (x * LCD_WIDTH) / kDstW;
      dst[x] = src_row[src_x];
    }
  }

  const uint16_t accent = pm_gfx->color565(255, 220, 160);
  const uint16_t dim = pm_gfx->color565(88, 78, 56);
  pm_gfx->drawCircle(pm_face_lcd_cx, pm_face_lcd_cy, 222, dim);
  pm_gfx->drawCircle(pm_face_lcd_cx, pm_face_lcd_cy, 218, accent);
  pm_face_draw_centered_line("navigation", 18, accent, 1, 1);
}

static float pm_faces_home_hue_deg(void) {
  struct tm tm = {};
  int sec_of_day_for_hue = 0;
  if (pm_time_valid()) {
    pm_time_local(&tm);
    sec_of_day_for_hue = tm.tm_hour * 3600 + tm.tm_min * 60 + tm.tm_sec;
    return static_cast<float>(sec_of_day_for_hue) * (360.0f / 86400.0f);
  }
  return fmodf(static_cast<float>(millis()) * 0.0015f, 360.0f);
}

static void pm_faces_on_leave(ClockFace from, ClockFace to) {
  (void)to;
  switch (from) {
    case ClockFace::Spotify:
      memset(&g_spotify_ui, 0, sizeof(g_spotify_ui));
      break;
    case ClockFace::Astrology:
    case ClockFace::LiveTransits:
    case ClockFace::Synastry:
      pm_ephemeris_release_cache();
      break;
    case ClockFace::CalciferCountdown:
      memset(&g_calcifer_ui, 0, sizeof(g_calcifer_ui));
      s_calcifer_have_data = false;
      break;
    case ClockFace::Spectrum:
      pm_face_spectrum_on_leave();
      break;
    case ClockFace::Tuning:
      pm_face_tuning_on_leave();
      break;
    case ClockFace::Chakra:
      pm_face_chakra_stop();
      break;
    case ClockFace::TibetanBowl:
      pm_face_tibetan_bowl_stop();
      break;
    case ClockFace::Ocarina:
      pm_face_ocarina_stop();
      break;
    case ClockFace::Bongo:
      pm_face_bongo_stop();
      break;
    case ClockFace::PanDrum:
      pm_face_pandrum_stop();
      break;
    case ClockFace::Piano:
      pm_face_piano_stop();
      break;
    case ClockFace::Level:
      break;
    case ClockFace::Rocket:
      memset(&g_rocket_ui, 0, sizeof(g_rocket_ui));
      s_rocket_have_data = false;
      pm_face_rocket_set_stream_qr_visible(false);
      pm_rocket_pad_image_release();
      break;
    case ClockFace::Radar:
      pm_face_radar_on_leave();
      break;
    case ClockFace::HidTouchpad:
      pm_face_hid_on_leave();
      break;
    case ClockFace::Biometrics:
      pm_face_biometrics_on_leave();
      break;
    case ClockFace::Watcher:
      pm_face_watcher_on_leave();
      break;
    case ClockFace::Faculty:
      pm_faculty_release_bust_cache();
      break;
    case ClockFace::Quotes:
      pm_faculty_release_bust_cache();
      break;
    case ClockFace::Weather:
      memset(&g_weather_ui, 0, sizeof(g_weather_ui));
      break;
    case ClockFace::Sky:
      pm_face_sky_on_leave();
      break;
    default:
      break;
  }
}

static void pm_faces_on_enter(ClockFace face, ClockFace from) {
  (void)from;
  switch (face) {
    case ClockFace::Spectrum:
      pm_face_spectrum_on_enter();
      break;
    case ClockFace::Tuning:
      pm_face_tuning_on_enter();
      break;
    case ClockFace::Radar:
      pm_face_radar_on_enter();
      break;
    case ClockFace::HidTouchpad:
      pm_face_hid_on_enter();
      break;
    case ClockFace::Biometrics:
      pm_face_biometrics_on_enter();
      break;
    case ClockFace::Watcher:
      pm_face_watcher_on_enter();
      break;
    case ClockFace::Faculty:
      break;
    default:
      break;
  }
}

static void pm_faces_transition_to(ClockFace face) {
  const ClockFace prev = s_clock_face;
  if (prev == face) {
    return;
  }
  pm_faces_on_leave(prev, face);
  s_clock_face = face;
  pm_faces_on_enter(face, prev);
}

ClockFace pm_faces_current(void) { return s_clock_face; }
void pm_faces_set(ClockFace face) {
  if (face == ClockFace::Castalia) {
    face = ClockFace::Settings;
    pm_settings_set_page(SettingsPage::Castalia);
  }
  pm_faces_transition_to(face);
}

void pm_faces_set_navigation_mode(bool active) { s_navigation_mode = active; }

bool pm_faces_navigation_mode(void) { return s_navigation_mode; }

void pm_faces_open_settings(void) {
  pm_settings_set_page(SettingsPage::WiFi);
  pm_faces_set(ClockFace::Settings);
}

bool pm_faces_castalia_active(void) {
  return s_clock_face == ClockFace::Settings && pm_settings_page() == SettingsPage::Castalia;
}

void pm_faces_cycle(int delta) {
  if (s_clock_face == ClockFace::Settings) {
    return;
  }
  constexpr int n = static_cast<int>(sizeof(k_face_dial_order) / sizeof(k_face_dial_order[0]));
  int idx = -1;
  for (int i = 0; i < n; ++i) {
    if (k_face_dial_order[i] == s_clock_face) {
      idx = i;
      break;
    }
  }
  if (idx < 0) {
    int v = static_cast<int>(s_clock_face);
    const int face_count = static_cast<int>(ClockFace::kNumFaces);
    do {
      v = (v + delta + face_count) % face_count;
    } while (pm_faces_skip_in_dial(static_cast<ClockFace>(v)));
    pm_faces_transition_to(static_cast<ClockFace>(v));
    return;
  }
  for (int step = 0; step < n; ++step) {
    idx = (idx + delta + n) % n;
    if (!pm_faces_skip_in_dial(k_face_dial_order[idx])) {
      pm_faces_transition_to(k_face_dial_order[idx]);
      return;
    }
  }
}



void pm_faces_draw(float thinking_progress) {
  struct tm tm = {};
  int sec_of_day_for_hue = 0;
  if (pm_time_valid()) {
    pm_time_local(&tm);
    sec_of_day_for_hue = tm.tm_hour * 3600 + tm.tm_min * 60 + tm.tm_sec;
  }
  const float hue =
      pm_time_valid() ? static_cast<float>(sec_of_day_for_hue) * (360.0f / 86400.0f)
                       : fmodf(static_cast<float>(millis()) * 0.0015f, 360.0f);
  const uint16_t bg_hsv = pm_face_color565_from_hsv(pm_gfx, hue, pm_face_hsv_s, pm_face_hsv_v);
  uint16_t bg = bg_hsv;
  if (s_clock_face != ClockFace::Apocalypso && s_clock_face != ClockFace::LiveTransits &&
      s_clock_face != ClockFace::CalciferCountdown &&
      s_clock_face != ClockFace::Spectrum && s_clock_face != ClockFace::Chakra &&
      s_clock_face != ClockFace::TibetanBowl && s_clock_face != ClockFace::Rocket &&
      s_clock_face != ClockFace::Radar && s_clock_face != ClockFace::HidTouchpad &&
      s_clock_face != ClockFace::Biometrics && s_clock_face != ClockFace::Faculty &&
      s_clock_face != ClockFace::Watcher &&
      s_clock_face != ClockFace::Weather && s_clock_face != ClockFace::Quotes &&
      s_clock_face != ClockFace::Globe && s_clock_face != ClockFace::Sky &&
      s_clock_face != ClockFace::Notes && s_clock_face != ClockFace::Ocarina &&
      s_clock_face != ClockFace::Bongo && s_clock_face != ClockFace::PanDrum &&
      s_clock_face != ClockFace::Piano && s_clock_face != ClockFace::Tuning &&
      s_clock_face != ClockFace::Level && s_clock_face != ClockFace::Orientation &&
      s_clock_face != ClockFace::Luopan && s_clock_face != ClockFace::Alethiometer &&
      s_clock_face != ClockFace::InqCard && s_clock_face != ClockFace::Lenormand &&
      s_clock_face != ClockFace::Geomancy &&
      s_clock_face != ClockFace::Pythia && s_clock_face != ClockFace::EnochianAngel &&
      s_clock_face != ClockFace::Runes && s_clock_face != ClockFace::QuestionOfDay &&
      s_clock_face != ClockFace::FocusTimer) {
#if MYNAH_HUE_HOME_ONLY
    if (s_clock_face == ClockFace::ClassicAnalog) {
      bg = pm_face_draw_home_gem_glow(hue);
    } else {
      pm_gfx->fillScreen(bg);
    }
#else
    pm_gfx->fillScreen(bg);
#endif
  }

  switch (s_clock_face) {
    case ClockFace::ClassicAnalog:
      pm_face_classic_analog_draw(bg, &tm, pm_time_valid());
      break;
    case ClockFace::Apocalypso:
      pm_face_apocalypso_draw(&tm, pm_time_valid());
      break;
    case ClockFace::DigitalLocal:
      pm_face_digital_draw(&tm, pm_time_valid());
      break;
    case ClockFace::Spotify:
      pm_face_spotify_draw();
      break;
    case ClockFace::Astrology:
      pm_face_astrology_draw(&tm, pm_time_valid(), -1, -1, false);
      break;
    case ClockFace::LiveTransits:
      pm_face_live_transits_draw(&tm, pm_time_valid());
      break;
    case ClockFace::Moon:
      pm_face_moon_draw(&tm, pm_time_valid());
      break;
    case ClockFace::CalciferCountdown:
      pm_face_calcifer_draw();
      break;
    case ClockFace::Castalia:
      pm_settings_set_page(SettingsPage::Castalia);
      pm_settings_draw();
      break;
    case ClockFace::Settings:
      pm_settings_draw();
      break;
    case ClockFace::Synastry:
      pm_face_synastry_draw(&tm, pm_time_valid());
      break;
    case ClockFace::Spectrum:
      pm_face_spectrum_draw(bg);
      break;
    case ClockFace::Chakra:
      pm_face_chakra_draw();
      break;
    case ClockFace::TibetanBowl:
      pm_face_tibetan_bowl_draw();
      break;
    case ClockFace::Rocket:
      pm_face_rocket_draw();
      break;
    case ClockFace::Radar:
      pm_face_radar_draw(&tm, pm_time_valid());
      break;
    case ClockFace::HidTouchpad:
      pm_face_hid_draw();
      break;
    case ClockFace::Biometrics:
      pm_face_biometrics_draw();
      break;
    case ClockFace::Watcher:
      pm_face_watcher_draw();
      break;
    case ClockFace::Faculty:
      pm_face_faculty_draw();
      break;
    case ClockFace::Weather: {
      int lh = 0;
      int lm = 0;
      if (pm_time_valid()) {
        lh = tm.tm_hour;
        lm = tm.tm_min;
      }
      pm_face_weather_draw(pm_time_valid(), lh, lm);
      break;
    }
    case ClockFace::Globe:
      pm_face_globe_draw(&tm, pm_time_valid());
      break;
    case ClockFace::Sky:
      pm_face_sky_draw(&tm, pm_time_valid());
      break;
    case ClockFace::Quotes:
      pm_face_quotes_draw();
      break;
    case ClockFace::Tarot:
      pm_face_tarot_draw(&tm, pm_time_valid());
      break;
    case ClockFace::InqCard:
      pm_face_inq_card_draw(&tm, pm_time_valid());
      break;
    case ClockFace::Lenormand:
      pm_face_lenormand_draw(&tm, pm_time_valid());
      break;
    case ClockFace::Geomancy:
      pm_face_geomancy_draw(&tm, pm_time_valid());
      break;
    case ClockFace::Pythia:
      pm_face_pythia_draw();
      break;
    case ClockFace::EnochianAngel:
      pm_face_enochian_angel_draw();
      break;
    case ClockFace::Notes:
      pm_face_notes_draw();
      break;
    case ClockFace::Ocarina:
      pm_face_ocarina_draw();
      break;
    case ClockFace::Bongo:
      pm_face_bongo_draw();
      break;
    case ClockFace::PanDrum:
      pm_face_pandrum_draw();
      break;
    case ClockFace::Piano:
      pm_face_piano_draw();
      break;
    case ClockFace::Level:
      pm_face_level_draw();
      break;
    case ClockFace::Orientation:
      pm_face_orientation_draw();
      break;
    case ClockFace::Luopan:
      pm_face_luopan_draw();
      break;
    case ClockFace::Tuning:
      pm_face_tuning_draw();
      break;
    case ClockFace::Alethiometer:
      pm_face_alethiometer_draw();
      break;
    case ClockFace::Runes:
      pm_face_runes_draw(&tm, pm_time_valid());
      break;
    case ClockFace::QuestionOfDay:
      pm_face_question_day_draw();
      break;
    case ClockFace::FocusTimer:
      pm_face_focus_draw();
      break;
    default:
      break;
  }

  const int banner_y = (s_clock_face == ClockFace::Apocalypso || s_clock_face == ClockFace::Spotify ||
                        s_clock_face == ClockFace::Astrology || s_clock_face == ClockFace::LiveTransits ||
                        s_clock_face == ClockFace::Moon ||
                        s_clock_face == ClockFace::CalciferCountdown || s_clock_face == ClockFace::Castalia ||
                        s_clock_face == ClockFace::Settings || s_clock_face == ClockFace::Synastry ||
                        s_clock_face == ClockFace::Spectrum || s_clock_face == ClockFace::Chakra ||
                        s_clock_face == ClockFace::TibetanBowl || s_clock_face == ClockFace::Rocket ||
                        s_clock_face == ClockFace::Radar || s_clock_face == ClockFace::HidTouchpad ||
                        s_clock_face == ClockFace::Biometrics ||
                        s_clock_face == ClockFace::Watcher ||
                        s_clock_face == ClockFace::Faculty ||
                        s_clock_face == ClockFace::Weather || s_clock_face == ClockFace::Quotes ||
                        s_clock_face == ClockFace::Globe || s_clock_face == ClockFace::Sky ||
                        s_clock_face == ClockFace::Tarot || s_clock_face == ClockFace::InqCard ||
                        s_clock_face == ClockFace::Notes || s_clock_face == ClockFace::Ocarina || s_clock_face == ClockFace::Bongo ||
                        s_clock_face == ClockFace::PanDrum || s_clock_face == ClockFace::Piano ||
                        s_clock_face == ClockFace::Level || s_clock_face == ClockFace::Orientation ||
                        s_clock_face == ClockFace::Luopan || s_clock_face == ClockFace::Tuning ||
                        s_clock_face == ClockFace::Alethiometer || s_clock_face == ClockFace::Lenormand ||
                        s_clock_face == ClockFace::Geomancy || s_clock_face == ClockFace::Pythia ||
                        s_clock_face == ClockFace::EnochianAngel || s_clock_face == ClockFace::Runes ||
                        s_clock_face == ClockFace::QuestionOfDay || s_clock_face == ClockFace::FocusTimer)
                           ? 352
                           : 320;
  if (MYNAH_DEBUG_GESTURES && g_gesture_banner[0] != '\0') {
    pm_face_draw_centered_line(g_gesture_banner, banner_y, pm_gfx->color565(255, 220, 160), 1, 1);
  }

  /** Rainbow annulus last (Moon/Daywheel draw their own; skip Castalia — QR repaint was tripping WDT/stack). */
  if (!pm_faces_castalia_active() && s_clock_face != ClockFace::Moon &&
      s_clock_face != ClockFace::Apocalypso && s_clock_face != ClockFace::Spotify &&
      s_clock_face != ClockFace::LiveTransits &&
      s_clock_face != ClockFace::CalciferCountdown && s_clock_face != ClockFace::Spectrum &&
      s_clock_face != ClockFace::TibetanBowl && s_clock_face != ClockFace::Rocket &&
      s_clock_face != ClockFace::Radar && s_clock_face != ClockFace::HidTouchpad &&
      s_clock_face != ClockFace::Biometrics && s_clock_face != ClockFace::Faculty &&
      s_clock_face != ClockFace::Watcher &&
      s_clock_face != ClockFace::Weather && s_clock_face != ClockFace::Quotes &&
      s_clock_face != ClockFace::Globe && s_clock_face != ClockFace::Sky &&
      s_clock_face != ClockFace::Tarot && s_clock_face != ClockFace::InqCard &&
      s_clock_face != ClockFace::Lenormand &&
      s_clock_face != ClockFace::Geomancy && s_clock_face != ClockFace::Pythia &&
      s_clock_face != ClockFace::EnochianAngel &&
      s_clock_face != ClockFace::Notes &&
      s_clock_face != ClockFace::Ocarina && s_clock_face != ClockFace::Bongo &&
      s_clock_face != ClockFace::PanDrum && s_clock_face != ClockFace::Piano &&
      s_clock_face != ClockFace::Level && s_clock_face != ClockFace::Orientation &&
      s_clock_face != ClockFace::Luopan && s_clock_face != ClockFace::Tuning &&
      s_clock_face != ClockFace::Alethiometer && s_clock_face != ClockFace::Runes &&
      s_clock_face != ClockFace::QuestionOfDay && s_clock_face != ClockFace::FocusTimer) {
    pm_face_draw_circumference_rainbow_24h(pm_time_valid());
    if (thinking_progress >= 0.f) {
      pm_face_draw_thinking_progress_ring(thinking_progress);
    }
  }
  if (s_navigation_mode) {
    pm_faces_draw_navigation_zoom();
  }
  s_clock_bg565 = bg;
  if (pm_time_valid()) {
    s_analog_saved_local_h = tm.tm_hour;
    s_analog_saved_local_m = tm.tm_min;
  }
  pm_gfx->flush();
}

void pm_faces_draw_home_gem_pulse(void) {
#if MYNAH_HUE_HOME_ONLY
  if (s_clock_face != ClockFace::ClassicAnalog) {
    return;
  }
  const float hue = pm_faces_home_hue_deg();
  pm_face_draw_home_gem_breath_only(hue);
  pm_gfx->flush();
#else
  (void)0;
#endif
}

bool pm_faces_banner_low(void) {
  const ClockFace f = s_clock_face;
  return f == ClockFace::Apocalypso || f == ClockFace::Spotify || f == ClockFace::Astrology ||
         f == ClockFace::LiveTransits || f == ClockFace::Moon || f == ClockFace::CalciferCountdown || f == ClockFace::Castalia ||
         f == ClockFace::Settings || f == ClockFace::Synastry || f == ClockFace::Spectrum ||
         f == ClockFace::Chakra || f == ClockFace::TibetanBowl || f == ClockFace::Rocket ||
         f == ClockFace::Radar || f == ClockFace::HidTouchpad ||
         f == ClockFace::Biometrics || f == ClockFace::Watcher ||
         f == ClockFace::Faculty || f == ClockFace::Weather ||
         f == ClockFace::Globe || f == ClockFace::Sky ||
         f == ClockFace::Quotes || f == ClockFace::Tarot || f == ClockFace::InqCard || f == ClockFace::Notes ||
         f == ClockFace::Ocarina || f == ClockFace::Bongo || f == ClockFace::PanDrum ||
         f == ClockFace::Piano || f == ClockFace::Level || f == ClockFace::Orientation ||
         f == ClockFace::Luopan || f == ClockFace::Tuning || f == ClockFace::Alethiometer ||
         f == ClockFace::Lenormand || f == ClockFace::Geomancy || f == ClockFace::Pythia ||
         f == ClockFace::EnochianAngel || f == ClockFace::Runes || f == ClockFace::QuestionOfDay ||
         f == ClockFace::FocusTimer;
}

uint16_t pm_faces_last_bg565(void) { return s_clock_bg565; }

bool pm_faces_local_hm_changed(int hour, int min) {
  if (s_clock_face == ClockFace::Settings || s_clock_face == ClockFace::Castalia ||
      s_clock_face == ClockFace::LiveTransits || s_clock_face == ClockFace::Synastry || s_clock_face == ClockFace::Spectrum ||
      s_clock_face == ClockFace::Chakra || s_clock_face == ClockFace::TibetanBowl ||
      s_clock_face == ClockFace::Rocket || s_clock_face == ClockFace::Radar ||
      s_clock_face == ClockFace::HidTouchpad || s_clock_face == ClockFace::Biometrics ||
      s_clock_face == ClockFace::Watcher ||
      s_clock_face == ClockFace::Faculty || s_clock_face == ClockFace::Weather ||
      s_clock_face == ClockFace::Globe || s_clock_face == ClockFace::Sky ||
      s_clock_face == ClockFace::Quotes || s_clock_face == ClockFace::Tarot ||
      s_clock_face == ClockFace::InqCard ||
      s_clock_face == ClockFace::Lenormand || s_clock_face == ClockFace::Geomancy ||
      s_clock_face == ClockFace::Pythia || s_clock_face == ClockFace::EnochianAngel ||
      s_clock_face == ClockFace::Notes || s_clock_face == ClockFace::Ocarina ||
      s_clock_face == ClockFace::Bongo || s_clock_face == ClockFace::PanDrum ||
      s_clock_face == ClockFace::Piano || s_clock_face == ClockFace::Level ||
      s_clock_face == ClockFace::Orientation || s_clock_face == ClockFace::Luopan ||
      s_clock_face == ClockFace::Tuning || s_clock_face == ClockFace::Alethiometer ||
      s_clock_face == ClockFace::Runes || s_clock_face == ClockFace::QuestionOfDay ||
      s_clock_face == ClockFace::FocusTimer) {
    return false;
  }
  return s_analog_saved_local_h < 0 || hour != s_analog_saved_local_h || min != s_analog_saved_local_m;
}

bool pm_faces_is_commonplace_home(void) {
  return s_clock_face == ClockFace::ClassicAnalog;
}

bool pm_faces_voice_input_enabled(void) {
#if defined(ASTROLABE_NO_ONBOARD_AUDIO) && ASTROLABE_NO_ONBOARD_AUDIO
  return false;
#endif
  return s_clock_face != ClockFace::Spectrum && s_clock_face != ClockFace::Tuning;
}
