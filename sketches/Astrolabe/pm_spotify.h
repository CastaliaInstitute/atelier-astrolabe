#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct PmSpotifyStatus {
  bool ok;
  bool is_playing;
  char track[88];
  char artist[88];
  /** Active Spotify Connect device name from GET /me/player (empty if none). */
  char device[72];
  char error[120];
};

/** POST mynah-spotify with action=status; fills [out] from JSON. */
bool pm_spotify_refresh(PmSpotifyStatus *out);

/** POST action: toggle | next | previous | play | pause | stop (stop = pause). */
bool pm_spotify_command(const char *action, PmSpotifyStatus *out);
