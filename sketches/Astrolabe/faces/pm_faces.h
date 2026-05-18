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
  /** QR → castalia.institute Google sign-in; tokens stored on watch for Edge Functions. */
  Castalia,
  /** Dual natal wheel/aspects for saved partner/family chart profiles. */
  Synastry,
  /** Pendulum metronome; tap = start/stop, swipe up/down = tempo. */
  Metronome,
  kNumFaces,
};

ClockFace pm_faces_current(void);
void pm_faces_set(ClockFace face);
void pm_faces_cycle(int delta);
bool pm_faces_is_commonplace_home(void);
bool pm_faces_banner_low(void);
void pm_faces_draw(float thinking_progress = -1.f);
uint16_t pm_faces_last_bg565(void);
bool pm_faces_local_hm_changed(int hour, int min);
