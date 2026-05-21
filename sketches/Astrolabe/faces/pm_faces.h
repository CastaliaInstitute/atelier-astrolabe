#pragma once

/** Clock-face modules live in `faces/<name>/` — see faces/README.md. */

#include <cstdint>

enum class ClockFace : uint8_t {
  ClassicAnalog = 0,
  Apocalypso,
  DigitalLocal,
  Spotify,
  Astrology,
  /** Lunar phase disk; tap = daily fortune, PWR hold = ask, BOOT = replay last TTS. */
  Moon,
  /** Hue Daywheel — rolling 12h calendar via `calcifer-status`. */
  CalciferCountdown,
  /** Legacy serial index; opens Settings → Castalia page. Not in swipe dial. */
  Castalia,
  /** Settings hub (home swipe down): WiFi + Castalia QR; swipe ← → between pages. */
  Settings,
  /** Dual natal wheel/aspects for saved partner/family chart profiles. */
  Synastry,
  /** Polar audio visualizers; swipe up/down cycles mode on this face. */
  Spectrum,
  /** Chakra symbols + solfeggio tones; swipe up/down, tap toggles tone. */
  Chakra,
  /** Singing bowl; drag rainbow rim to strike, swipe up/down for presets. */
  TibetanBowl,
  /** Launch clock — upcoming orbital launches on a 14-day dial (Launch Library 2). */
  Rocket,
  /** BLE peer radar: force graph + 6DOF gyro bearing (no magnetometer). */
  Radar,
  /** ask-faculty conversations; swipe up/down cycles recent faculty. */
  Faculty,
  /** 24h radial temp + humidity rings; current conditions center. */
  Weather,
  /** Castalia quote of the day + tiny faculty bust. */
  Quotes,
  /** Live transits: now + next Moon sign ingress as paired celestial spheres. */
  LiveTransits,
  /** Daily Major Arcana tarot card; swipe up/down browses deck. */
  Tarot,
  /** Offline-first voice notes queued for Commonplace. */
  Notes,
  /** Touch-playable clay ocarina; tap holes for notes, swipe up/down changes key. */
  Ocarina,
  /** Touch-playable bongo; tap distance from center controls drum pitch. */
  Bongo,
  /** One-octave circular piano; white keys outside, black keys inside. */
  Piano,
  /** IMU bubble level: top of the display is forward; BOOT speaks the correction. */
  Level,
  /** 14-note touch-playable handpan / pan drum face. */
  PanDrum,
  /** Alethiometer compass: 36 symbols, three question needles, one answer needle. */
  Alethiometer,
  kNumFaces,
};

ClockFace pm_faces_current(void);
void pm_faces_set(ClockFace face);
void pm_faces_cycle(int delta);
void pm_faces_open_settings(void);
bool pm_faces_castalia_active(void);
bool pm_faces_is_commonplace_home(void);
bool pm_faces_banner_low(void);
void pm_faces_draw(float thinking_progress = -1.f);
/** Home gem + rainbow only (breath animation). */
void pm_faces_draw_home_gem_pulse(void);
uint16_t pm_faces_last_bg565(void);
bool pm_faces_local_hm_changed(int hour, int min);

/** False on Spectrum — PWR/BOOT are not used for voice STT/TTS on that face. */
bool pm_faces_voice_input_enabled(void);
