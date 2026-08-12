#pragma once

#include "astrolabe_baseline.h"

// Optional: copy to `include/secrets.local.h` (gitignored) so you can keep this template unchanged.

#define MYNAH_WIFI_SSID ""
#define MYNAH_WIFI_PASSWORD ""

// Local civil-time fallback when IP timezone lookup is unavailable.
// America/Denver daylight time is -6 * 3600; standard time is -7 * 3600.
#define MYNAH_TZ_FALLBACK_OFFSET_SEC (-6 * 3600)

// Supabase project (same as Android Mynah BuildConfig).
#define MYNAH_SUPABASE_URL ""
#define MYNAH_SUPABASE_ANON_KEY ""

// Optional voice endpoint overrides for local relay/mock testing.
// MYNAH_VOICE_HTTP_URL may be a base URL or a full /functions/v1/voice-pipeline URL.
// MYNAH_VOICE_STREAM_URL must be a full ws:// or wss:// /functions/v1/voice-stream URL.
#ifndef MYNAH_VOICE_HTTP_URL
#define MYNAH_VOICE_HTTP_URL ""
#endif
#ifndef MYNAH_VOICE_STREAM_URL
#define MYNAH_VOICE_STREAM_URL ""
#endif

// Optional LAN/BLE presentation control token. Leave empty to disable /control and BLE control writes.
#define MYNAH_REMOTE_CONTROL_KEY ""

/** Facial metrics service. The Watcher posts camera frames here before Castalia capture. */
#ifndef MYNAH_FACE_METRICS_URL
#define MYNAH_FACE_METRICS_URL ASTROLABE_FACE_METRICS_URL_DEFAULT
#endif

/** castalia.institute origin for QR sign-in (Google → /auth/mynah-device/ handoff). */
#ifndef MYNAH_CASTALIA_WEB_ORIGIN
#define MYNAH_CASTALIA_WEB_ORIGIN "https://castalia.institute"
#endif

/* Spotify Connect (watch): deploy `mynah-spotify` and set Supabase secrets
 * SPOTIFY_CLIENT_ID, SPOTIFY_CLIENT_SECRET, SPOTIFY_REFRESH_TOKEN.
 * Refresh token must include scopes: user-read-playback-state, user-modify-playback-state
 * (controls whichever device is active in Spotify; the watch is not a Connect receiver). */
