#pragma once

// Optional: copy to `include/secrets.local.h` (gitignored) so you can keep this template unchanged.

#define MYNAH_WIFI_SSID ""
#define MYNAH_WIFI_PASSWORD ""

// Local civil-time fallback when IP timezone lookup is unavailable.
// America/Denver daylight time is -6 * 3600; standard time is -7 * 3600.
#define MYNAH_TZ_FALLBACK_OFFSET_SEC (-6 * 3600)

// Supabase project (same as Android Mynah BuildConfig).
#define MYNAH_SUPABASE_URL ""
#define MYNAH_SUPABASE_ANON_KEY ""

/** castalia.institute origin for QR sign-in (Google → /auth/mynah-device/ handoff). */
#ifndef MYNAH_CASTALIA_WEB_ORIGIN
#define MYNAH_CASTALIA_WEB_ORIGIN "https://castalia.institute"
#endif

/* Spotify Connect (watch): deploy `mynah-spotify` and set Supabase secrets
 * SPOTIFY_CLIENT_ID, SPOTIFY_CLIENT_SECRET, SPOTIFY_REFRESH_TOKEN.
 * Refresh token must include scopes: user-read-playback-state, user-modify-playback-state
 * (controls whichever device is active in Spotify; the watch is not a Connect receiver). */
