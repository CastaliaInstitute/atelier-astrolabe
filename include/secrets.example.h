#pragma once

// Optional: copy to `include/secrets.local.h` (gitignored) so you can keep this template unchanged.

#define MYNAH_WIFI_SSID ""
#define MYNAH_WIFI_PASSWORD ""

// Supabase project (same as Android Mynah BuildConfig).
#define MYNAH_SUPABASE_URL ""
#define MYNAH_SUPABASE_ANON_KEY ""

/* Spotify Connect (watch): deploy `mynah-spotify` and set Supabase secrets
 * SPOTIFY_CLIENT_ID, SPOTIFY_CLIENT_SECRET, SPOTIFY_REFRESH_TOKEN.
 * Refresh token must include scopes: user-read-playback-state, user-modify-playback-state
 * (controls whichever device is active in Spotify; the watch is not a Connect receiver). */
