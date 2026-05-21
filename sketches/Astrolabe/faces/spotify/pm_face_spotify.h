#pragma once

#include "pm_gesture.h"
#include "pm_spotify.h"
#include <cstddef>
#include <cstdint>

/** Browse selection snaps back to now playing after this idle period (design doc §9). */
#ifndef PM_SPOTIFY_BROWSE_TIMEOUT_MS
#define PM_SPOTIFY_BROWSE_TIMEOUT_MS 15000u
#endif

/** Stub Music Stream length for M1/M2 (real hub stream in M3). */
#ifndef PM_SPOTIFY_STREAM_MAX
#define PM_SPOTIFY_STREAM_MAX 9
#endif

enum class PmSpotifyFaceMode : uint8_t {
  NoMusic,
  NowPlaying,
  Paused,
  BrowsingWhilePlaying,
  BrowsingWhilePaused,
  Unavailable,
  Loading,
};

struct PmSpotifyStreamItem {
  char title[48];
  char artist[40];
  uint16_t disc_rgb565;
  uint16_t highlight_rgb565;
};

extern PmSpotifyStatus g_spotify_ui;

void pm_face_spotify_reset();
void pm_face_spotify_draw();
/** Handle tap / swipe / double-tap / long-press on the Spotify face. Returns true if consumed. */
bool pm_face_spotify_on_gesture(PmGestureKind kind, int16_t x, int16_t y, char *banner, size_t banner_cap);
/** Browse timeout + spin animation tick. */
void pm_face_spotify_tick(uint32_t now_ms);
bool pm_face_spotify_needs_repaint(uint32_t now_ms);
/** Overlay hub status onto the playing slot when Wi‑Fi fetch succeeds (M3 path grows here). */
void pm_face_spotify_sync_hub(const PmSpotifyStatus *hub, bool hub_ok);
