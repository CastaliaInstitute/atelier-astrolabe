#include "faces/pm_face_registry.h"

#include <cstring>

#include "faces/apocalypso/pm_face_apocalypso.h"
#include "faces/alethiometer/pm_face_alethiometer.h"
#include "faces/astrology/pm_face_astrology.h"
#include "faces/bongo/pm_face_bongo.h"
#include "faces/calcifer/pm_face_calcifer.h"
#include "faces/castalia/pm_face_castalia.h"
#include "faces/chakra/pm_face_chakra.h"
#include "faces/classic_analog/pm_face_classic_analog.h"
#include "faces/cycle/pm_face_cycle.h"
#include "faces/digital/pm_face_digital.h"
#include "faces/faculty/pm_face_faculty.h"
#include "faces/level/pm_face_level.h"
#include "faces/live_transits/pm_face_live_transits.h"
#include "faces/moon/pm_face_moon.h"
#include "faces/notes/pm_face_notes.h"
#include "faces/ocarina/pm_face_ocarina.h"
#include "faces/pandrum/pm_face_pandrum.h"
#include "faces/piano/pm_face_piano.h"
#include "faces/quotes/pm_face_quotes.h"
#include "faces/radar/pm_face_radar.h"
#include "faces/rocket/pm_face_rocket.h"
#include "faces/runes/pm_face_runes.h"
#include "faces/settings/pm_face_settings_wifi.h"
#include "faces/shared/pm_face_draw.h"
#include "faces/spotify/pm_face_spotify.h"
#include "faces/spectrum/pm_face_spectrum.h"
#include "faces/synastry/pm_face_synastry.h"
#include "faces/tarot/pm_face_tarot.h"
#include "faces/tibetan_bowl/pm_face_tibetan_bowl.h"
#include "faces/tuning/pm_face_tuning.h"
#include "faces/weather/pm_face_weather.h"
#include "pm_ephemeris.h"
#include "pm_faculty.h"
#include "pm_settings.h"

namespace {

constexpr uint32_t kLow = kPmFaceLowGestureBanner;
constexpr uint32_t kOwnBg = kPmFaceDrawsOwnBackground;
constexpr uint32_t kSkipRainbow = kPmFaceSkipRainbow;
constexpr uint32_t kNoMinute = kPmFaceNoMinuteRedraw;
constexpr uint32_t kHidden = kPmFaceHiddenFromDial;
constexpr uint32_t kNoVoice = kPmFaceDisableVoiceInput;

void draw_classic_analog(const PmFaceDrawContext &ctx) {
  pm_face_classic_analog_draw(ctx.bg565, ctx.local_time, ctx.time_valid);
}

void draw_apocalypso(const PmFaceDrawContext &ctx) {
  pm_face_apocalypso_draw(ctx.local_time, ctx.time_valid);
}

void draw_digital(const PmFaceDrawContext &ctx) {
  pm_face_digital_draw(ctx.local_time, ctx.time_valid);
}

void draw_spotify(const PmFaceDrawContext &) { pm_face_spotify_draw(); }

bool tick_spotify(const PmFaceTickContext &ctx) {
  pm_face_spotify_tick(ctx.now_ms);
  return pm_face_spotify_needs_repaint(ctx.now_ms);
}

void leave_spotify(ClockFace) { memset(&g_spotify_ui, 0, sizeof(g_spotify_ui)); }

void draw_astrology(const PmFaceDrawContext &ctx) {
  pm_face_astrology_draw(ctx.local_time, ctx.time_valid, -1, -1, false);
}

void leave_ephemeris(ClockFace) { pm_ephemeris_release_cache(); }

void draw_live_transits(const PmFaceDrawContext &ctx) {
  pm_face_live_transits_draw(ctx.local_time, ctx.time_valid);
}

void draw_moon(const PmFaceDrawContext &ctx) { pm_face_moon_draw(ctx.local_time, ctx.time_valid); }

void draw_calcifer(const PmFaceDrawContext &) { pm_face_calcifer_draw(); }

void leave_calcifer(ClockFace) {
  memset(&g_calcifer_ui, 0, sizeof(g_calcifer_ui));
  s_calcifer_have_data = false;
}

void draw_castalia(const PmFaceDrawContext &) {
  pm_settings_set_page(SettingsPage::Castalia);
  pm_settings_draw();
}

void draw_settings(const PmFaceDrawContext &) { pm_settings_draw(); }

void draw_synastry(const PmFaceDrawContext &ctx) {
  pm_face_synastry_draw(ctx.local_time, ctx.time_valid);
}

void draw_spectrum(const PmFaceDrawContext &ctx) { pm_face_spectrum_draw(ctx.bg565); }

void enter_spectrum(ClockFace) { pm_face_spectrum_on_enter(); }
bool tick_spectrum(const PmFaceTickContext &ctx) {
  static uint32_t s_last_ms = 0;
  if (ctx.now_ms - s_last_ms < 50u) {
    return false;
  }
  s_last_ms = ctx.now_ms;
  pm_face_spectrum_tick();
  return true;
}
void leave_spectrum(ClockFace) { pm_face_spectrum_on_leave(); }

void draw_chakra(const PmFaceDrawContext &) { pm_face_chakra_draw(); }
bool tick_chakra(const PmFaceTickContext &ctx) { return pm_face_chakra_anim_tick(ctx.now_ms); }
void leave_chakra(ClockFace) { pm_face_chakra_stop(); }

void draw_tibetan_bowl(const PmFaceDrawContext &) { pm_face_tibetan_bowl_draw(); }
bool tick_tibetan_bowl(const PmFaceTickContext &ctx) {
  return pm_face_tibetan_bowl_anim_tick(ctx.now_ms);
}
void leave_tibetan_bowl(ClockFace) { pm_face_tibetan_bowl_stop(); }

void draw_rocket(const PmFaceDrawContext &) { pm_face_rocket_draw(); }
bool tick_rocket(const PmFaceTickContext &ctx) { return pm_face_rocket_needs_repaint(ctx.now_ms); }

void leave_rocket(ClockFace) {
  memset(&g_rocket_ui, 0, sizeof(g_rocket_ui));
  s_rocket_have_data = false;
  pm_face_rocket_set_stream_qr_visible(false);
  pm_rocket_pad_image_release();
}

void draw_radar(const PmFaceDrawContext &ctx) { pm_face_radar_draw(ctx.local_time, ctx.time_valid); }
void enter_radar(ClockFace) { pm_face_radar_on_enter(); }
bool tick_radar(const PmFaceTickContext &ctx) {
  static uint32_t s_last_ms = 0;
  if (ctx.now_ms - s_last_ms < 80u) {
    return false;
  }
  s_last_ms = ctx.now_ms;
  pm_face_radar_tick(ctx.now_ms);
  return true;
}
void leave_radar(ClockFace) { pm_face_radar_on_leave(); }

void draw_faculty(const PmFaceDrawContext &) { pm_face_faculty_draw(); }
bool tick_faculty(const PmFaceTickContext &ctx) { return pm_faculty_tick(ctx.now_ms); }
void leave_faculty(ClockFace) { pm_faculty_release_bust_cache(); }

void draw_weather(const PmFaceDrawContext &ctx) {
  pm_face_weather_draw(ctx.time_valid, ctx.local_hour, ctx.local_min);
}

void leave_weather(ClockFace) { memset(&g_weather_ui, 0, sizeof(g_weather_ui)); }

void draw_quotes(const PmFaceDrawContext &) { pm_face_quotes_draw(); }
bool tick_quotes(const PmFaceTickContext &ctx) { return pm_faculty_tick(ctx.now_ms); }

void leave_quotes(ClockFace) {
  pm_faculty_release_bust_cache();
  memset(&g_quotes_ui, 0, sizeof(g_quotes_ui));
}

void draw_tarot(const PmFaceDrawContext &ctx) { pm_face_tarot_draw(ctx.local_time, ctx.time_valid); }
void draw_notes(const PmFaceDrawContext &) { pm_face_notes_draw(); }

void draw_ocarina(const PmFaceDrawContext &) { pm_face_ocarina_draw(); }
bool tick_ocarina(const PmFaceTickContext &ctx) { return pm_face_ocarina_anim_tick(ctx.now_ms); }
void leave_ocarina(ClockFace) { pm_face_ocarina_stop(); }

void draw_bongo(const PmFaceDrawContext &) { pm_face_bongo_draw(); }
bool tick_bongo(const PmFaceTickContext &ctx) {
  return pm_face_bongo_motion_tick(ctx.now_ms) || pm_face_bongo_anim_tick(ctx.now_ms);
}
void leave_bongo(ClockFace) { pm_face_bongo_stop(); }

void draw_pandrum(const PmFaceDrawContext &) { pm_face_pandrum_draw(); }
bool tick_pandrum(const PmFaceTickContext &ctx) {
  return pm_face_pandrum_motion_tick(ctx.now_ms) || pm_face_pandrum_anim_tick(ctx.now_ms);
}
void leave_pandrum(ClockFace) { pm_face_pandrum_stop(); }

void draw_piano(const PmFaceDrawContext &) { pm_face_piano_draw(); }
bool tick_piano(const PmFaceTickContext &ctx) { return pm_face_piano_anim_tick(ctx.now_ms); }
void leave_piano(ClockFace) { pm_face_piano_stop(); }

void draw_level(const PmFaceDrawContext &) { pm_face_level_draw(); }
bool tick_level(const PmFaceTickContext &ctx) {
  static uint32_t s_last_ms = 0;
  if (ctx.now_ms - s_last_ms < 50u) {
    return false;
  }
  s_last_ms = ctx.now_ms;
  return pm_face_level_anim_tick(ctx.now_ms);
}

void draw_tuning(const PmFaceDrawContext &) { pm_face_tuning_draw(); }
void enter_tuning(ClockFace) { pm_face_tuning_on_enter(); }
bool tick_tuning(const PmFaceTickContext &ctx) { return pm_face_tuning_tick(ctx.now_ms); }
void leave_tuning(ClockFace) { pm_face_tuning_on_leave(); }

void draw_alethiometer(const PmFaceDrawContext &) { pm_face_alethiometer_draw(); }
bool tick_alethiometer(const PmFaceTickContext &ctx) {
  return pm_face_alethiometer_anim_tick(ctx.now_ms);
}
void draw_runes(const PmFaceDrawContext &ctx) { pm_face_runes_draw(ctx.local_time, ctx.time_valid); }
void draw_cycle(const PmFaceDrawContext &ctx) { pm_face_cycle_draw(ctx.local_time, ctx.time_valid); }
bool tick_cycle(const PmFaceTickContext &ctx) {
  return pm_face_cycle_confirm_active(ctx.now_ms) || pm_face_cycle_take_full_ring_scheduled(ctx.now_ms);
}
void leave_settings(ClockFace) { pm_settings_on_leave(); }

constexpr uint32_t kStandardLowOwnNoMinuteSkip = kLow | kOwnBg | kNoMinute | kSkipRainbow;

const PmFaceDescriptor kFaces[] = {
    {ClockFace::ClassicAnalog, "classic_analog", "Classic Analog", 0, draw_classic_analog, nullptr, nullptr, nullptr},
    {ClockFace::Apocalypso, "apocalypso", "Apocalypso", kLow | kOwnBg | kSkipRainbow, draw_apocalypso, nullptr, nullptr, nullptr},
    {ClockFace::DigitalLocal, "digital", "Digital", 0, draw_digital, nullptr, nullptr, nullptr},
    {ClockFace::Spotify, "spotify", "Spotify", kLow | kSkipRainbow, draw_spotify, tick_spotify, nullptr, leave_spotify},
    {ClockFace::Astrology, "astrology", "Astrology", kLow, draw_astrology, nullptr, nullptr, leave_ephemeris},
    {ClockFace::Moon, "moon", "Moon", kLow | kSkipRainbow, draw_moon, nullptr, nullptr, nullptr},
    {ClockFace::CalciferCountdown, "calcifer", "Calcifer", kLow | kOwnBg | kSkipRainbow, draw_calcifer, nullptr, nullptr, leave_calcifer},
    {ClockFace::Castalia, "castalia", "Castalia", kLow | kHidden, draw_castalia, nullptr, nullptr, nullptr},
    {ClockFace::Settings, "settings", "Settings", kLow | kHidden | kNoMinute, draw_settings, nullptr, nullptr, leave_settings},
    {ClockFace::Synastry, "synastry", "Synastry", kLow | kNoMinute, draw_synastry, nullptr, nullptr, leave_ephemeris},
    {ClockFace::Spectrum, "spectrum", "Spectrum", kStandardLowOwnNoMinuteSkip | kNoVoice, draw_spectrum, tick_spectrum, enter_spectrum, leave_spectrum},
    {ClockFace::Chakra, "chakra", "Chakra", kLow | kOwnBg | kNoMinute, draw_chakra, tick_chakra, nullptr, leave_chakra},
    {ClockFace::TibetanBowl, "tibetan_bowl", "Tibetan Bowl", kStandardLowOwnNoMinuteSkip | kHidden, draw_tibetan_bowl, tick_tibetan_bowl, nullptr, leave_tibetan_bowl},
    {ClockFace::Rocket, "rocket", "Rocket", kStandardLowOwnNoMinuteSkip, draw_rocket, tick_rocket, nullptr, leave_rocket},
    {ClockFace::Radar, "radar", "Radar", kStandardLowOwnNoMinuteSkip, draw_radar, tick_radar, enter_radar, leave_radar},
    {ClockFace::Faculty, "faculty", "Faculty", kStandardLowOwnNoMinuteSkip, draw_faculty, tick_faculty, nullptr, leave_faculty},
    {ClockFace::Weather, "weather", "Weather", kStandardLowOwnNoMinuteSkip, draw_weather, nullptr, nullptr, leave_weather},
    {ClockFace::Quotes, "quotes", "Quotes", kStandardLowOwnNoMinuteSkip, draw_quotes, tick_quotes, nullptr, leave_quotes},
    {ClockFace::LiveTransits, "live_transits", "Live Transits", kStandardLowOwnNoMinuteSkip, draw_live_transits, nullptr, nullptr, leave_ephemeris},
    {ClockFace::Tarot, "tarot", "Tarot", kLow | kNoMinute | kSkipRainbow, draw_tarot, nullptr, nullptr, nullptr},
    {ClockFace::Notes, "notes", "Notes", kStandardLowOwnNoMinuteSkip, draw_notes, nullptr, nullptr, nullptr},
    {ClockFace::Ocarina, "ocarina", "Ocarina", kStandardLowOwnNoMinuteSkip, draw_ocarina, tick_ocarina, nullptr, leave_ocarina},
    {ClockFace::Bongo, "bongo", "Bongo", kStandardLowOwnNoMinuteSkip, draw_bongo, tick_bongo, nullptr, leave_bongo},
    {ClockFace::Piano, "piano", "Piano", kStandardLowOwnNoMinuteSkip, draw_piano, tick_piano, nullptr, leave_piano},
    {ClockFace::Level, "level", "Level", kStandardLowOwnNoMinuteSkip, draw_level, tick_level, nullptr, nullptr},
    {ClockFace::Tuning, "tuning", "Tuning", kStandardLowOwnNoMinuteSkip | kNoVoice, draw_tuning, tick_tuning, enter_tuning, leave_tuning},
    {ClockFace::PanDrum, "pandrum", "Pan Drum", kStandardLowOwnNoMinuteSkip, draw_pandrum, tick_pandrum, nullptr, leave_pandrum},
    {ClockFace::Alethiometer, "alethiometer", "Alethiometer", kStandardLowOwnNoMinuteSkip, draw_alethiometer, tick_alethiometer, nullptr, nullptr},
    {ClockFace::Runes, "runes", "Runes", kStandardLowOwnNoMinuteSkip, draw_runes, nullptr, nullptr, nullptr},
    {ClockFace::Cycle, "cycle", "Cycle", kStandardLowOwnNoMinuteSkip, draw_cycle, tick_cycle, nullptr, nullptr},
};

}  // namespace

size_t pm_face_registry_count(void) { return sizeof(kFaces) / sizeof(kFaces[0]); }

const PmFaceDescriptor *pm_face_registry_at(size_t index) {
  return index < pm_face_registry_count() ? &kFaces[index] : nullptr;
}

const PmFaceDescriptor *pm_face_registry_find(ClockFace face) {
  for (const PmFaceDescriptor &descriptor : kFaces) {
    if (descriptor.id == face) {
      return &descriptor;
    }
  }
  return nullptr;
}

bool pm_face_registry_has_flag(ClockFace face, PmFaceFlags flag) {
  return pm_face_has_flag(pm_face_registry_find(face), flag);
}
