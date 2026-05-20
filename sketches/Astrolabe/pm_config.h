#pragma once

#include <Arduino.h>

#if __has_include("secrets.local.h")
#include "secrets.local.h"
#else
#include "secrets.example.h"
#endif

/** Written to NVS on first boot when WiFi keys are empty (override in secrets.local.h if needed). */
#ifndef MYNAH_WIFI_NVS_DEFAULT_SSID
#define MYNAH_WIFI_NVS_DEFAULT_SSID "The Chateau"
#endif
#ifndef MYNAH_WIFI_NVS_DEFAULT_PASS
#define MYNAH_WIFI_NVS_DEFAULT_PASS "thechateau"
#endif

/** Wearer display name (NVS `user_name`; serial: `name Daniel`). */
#ifndef MYNAH_USER_NAME_DEFAULT
#define MYNAH_USER_NAME_DEFAULT "Daniel"
#endif

/** Castalia profile/settings identity used during face login (NVS override via serial). */
#ifndef MYNAH_CASTALIA_INDIVIDUAL_DEFAULT
#define MYNAH_CASTALIA_INDIVIDUAL_DEFAULT "DanielCMcShan"
#endif
#ifndef MYNAH_CASTALIA_SETTINGS_SOURCE
#define MYNAH_CASTALIA_SETTINGS_SOURCE "supabase"
#endif
#ifndef MYNAH_CASTALIA_REPO_OWNER
#define MYNAH_CASTALIA_REPO_OWNER "CastaliaInstitute"
#endif
#ifndef MYNAH_CASTALIA_REPO_PREFIX
#define MYNAH_CASTALIA_REPO_PREFIX "castalia-"
#endif
#ifndef MYNAH_CASTALIA_SETTINGS_PATH
#define MYNAH_CASTALIA_SETTINGS_PATH "settings/faces.json"
#endif

#ifndef MYNAH_VOICE_MAX_PCM_BYTES
#define MYNAH_VOICE_MAX_PCM_BYTES (16000 * 2 * 5)
#endif

/** Touch Y ≥ this is treated as “bottom rim” for gesture swipe suppression (not used for PTT). */
#ifndef MYNAH_PTT_MIN_Y
#define MYNAH_PTT_MIN_Y 260
#endif

/** Hold BOOT this long before mic arms (avoids accidental voice from a tap). */
#ifndef MYNAH_PTT_ARM_MS
#define MYNAH_PTT_ARM_MS 400
#endif

/** 1 = swipe/gesture debug banners on clock faces; 0 = production. */
#ifndef MYNAH_DEBUG_GESTURES
#define MYNAH_DEBUG_GESTURES 0
#endif

/** ClassicAnalog home: ambient hue + rainbow only (no clock hands). */
#ifndef MYNAH_HUE_HOME_ONLY
#define MYNAH_HUE_HOME_ONLY 1
#endif

/** Home gem breathing pulse (1 = on at boot). */
#ifndef MYNAH_HUE_GEM_PULSE_DEFAULT
#define MYNAH_HUE_GEM_PULSE_DEFAULT 1
#endif
/** Full 5-6-7 breath cycles per minute (~3 ≈ 20 s/cycle). */
#ifndef MYNAH_HUE_GEM_PULSE_BPM_DEFAULT
#define MYNAH_HUE_GEM_PULSE_BPM_DEFAULT 3
#endif
#ifndef MYNAH_HUE_GEM_PULSE_BPM_MIN
#define MYNAH_HUE_GEM_PULSE_BPM_MIN 2
#endif
#ifndef MYNAH_HUE_GEM_PULSE_BPM_MAX
#define MYNAH_HUE_GEM_PULSE_BPM_MAX 8
#endif
/** Peak-to-trough brightness swing (0..1). */
#ifndef MYNAH_HUE_GEM_PULSE_DEPTH
#define MYNAH_HUE_GEM_PULSE_DEPTH 0.11f
#endif
#ifndef MYNAH_HUE_GEM_PULSE_INHALE
#define MYNAH_HUE_GEM_PULSE_INHALE 5
#endif
#ifndef MYNAH_HUE_GEM_PULSE_HOLD
#define MYNAH_HUE_GEM_PULSE_HOLD 6
#endif
#ifndef MYNAH_HUE_GEM_PULSE_EXHALE
#define MYNAH_HUE_GEM_PULSE_EXHALE 7
#endif
#ifndef MYNAH_HUE_GEM_PULSE_REPAINT_MIN_MS
#define MYNAH_HUE_GEM_PULSE_REPAINT_MIN_MS 33u
#endif

#ifndef MYNAH_CALCIFER_POLL_MS
#define MYNAH_CALCIFER_POLL_MS 45000u
#endif

#ifndef MYNAH_WEATHER_POLL_MS
#define MYNAH_WEATHER_POLL_MS 900000u
#endif

#ifndef MYNAH_QUOTES_POLL_MS
#define MYNAH_QUOTES_POLL_MS 3600000u
#endif

/** Launch Library 2 poll interval while Rocket face is visible. */
#ifndef MYNAH_ROCKET_POLL_MS
#define MYNAH_ROCKET_POLL_MS 120000u
#endif

#ifndef MYNAH_ROCKET_HTTP_MS
#define MYNAH_ROCKET_HTTP_MS 20000
#endif

#ifndef MYNAH_ROCKET_MAX_BYTES
#define MYNAH_ROCKET_MAX_BYTES 65536
#endif

/** Max JPEG download size for launch / pad photo (LL2 CDN). */
#ifndef MYNAH_ROCKET_IMAGE_MAX_BYTES
#define MYNAH_ROCKET_IMAGE_MAX_BYTES 400000
#endif

/** Longest edge after decode (scaled down if larger). */
#ifndef MYNAH_ROCKET_IMAGE_MAX_DIM
#define MYNAH_ROCKET_IMAGE_MAX_DIM 240
#endif

/** Skip Rocket HTTPS refresh when BLE/Radar has left too little heap for TLS. */
#ifndef MYNAH_ROCKET_MIN_FETCH_HEAP
#define MYNAH_ROCKET_MIN_FETCH_HEAP 100000u
#endif

/** Skip Faculty portrait HTTPS refresh when Radar/BLE leaves too little heap for TLS. */
#ifndef MYNAH_FACULTY_MIN_FETCH_HEAP
#define MYNAH_FACULTY_MIN_FETCH_HEAP 100000u
#endif
#ifndef MYNAH_FACULTY_BUST_WIDTH
#define MYNAH_FACULTY_BUST_WIDTH 192
#endif
#ifndef MYNAH_FACULTY_BUST_HEIGHT
#define MYNAH_FACULTY_BUST_HEIGHT 240
#endif
#ifndef MYNAH_FACULTY_BUST_QUALITY
#define MYNAH_FACULTY_BUST_QUALITY 72
#endif
#ifndef MYNAH_FACULTY_BUST_ORIGIN
#define MYNAH_FACULTY_BUST_ORIGIN MYNAH_CASTALIA_WEB_ORIGIN
#endif

/** Reject unsynced/stale RTC values older than 2024-01-01 UTC. */
#ifndef MYNAH_TIME_VALID_MIN_EPOCH
#define MYNAH_TIME_VALID_MIN_EPOCH 1704067200
#endif

/** Skip Spotify HTTPS calls when internal heap is too low for TLS. */
#ifndef MYNAH_SPOTIFY_MIN_FETCH_HEAP
#define MYNAH_SPOTIFY_MIN_FETCH_HEAP 140000u
#endif

/** Skip foreground HTTPS refreshes on face load when loopTask memory is constrained. */
#ifndef MYNAH_FACE_FETCH_MIN_HEAP
#define MYNAH_FACE_FETCH_MIN_HEAP 140000u
#endif

/** Skip ephemeris HTTPS month loads when loopTask/TLS memory is constrained. */
#ifndef MYNAH_EPHEMERIS_MIN_FETCH_HEAP
#define MYNAH_EPHEMERIS_MIN_FETCH_HEAP 140000u
#endif

/** TLS is fragile when internal heap is fragmented even if total free looks okay. */
#ifndef MYNAH_TLS_MIN_LARGEST_INTERNAL
#define MYNAH_TLS_MIN_LARGEST_INTERNAL 36000u
#endif

/** Never let response-buffer fallback malloc drain internal RAM below this reserve. */
#ifndef MYNAH_RESPONSE_INTERNAL_FLOOR
#define MYNAH_RESPONSE_INTERNAL_FLOOR 90000u
#endif

/** 1 = fetch Swiss Ephemeris from ephemeris.castalia.institute when WiFi is up. */
#ifndef MYNAH_EPHEMERIS_ENABLE
#define MYNAH_EPHEMERIS_ENABLE 1
#endif

/** Base URL for precomputed monthly JSON (GitHub Pages). */
#ifndef MYNAH_EPHEMERIS_DATA_BASE
#define MYNAH_EPHEMERIS_DATA_BASE "https://ephemeris.castalia.institute/data/ephem"
#endif

#ifndef MYNAH_EPHEMERIS_HTTP_MS
#define MYNAH_EPHEMERIS_HTTP_MS 8000
#endif

#ifndef MYNAH_EPHEMERIS_MONTH_MAX_BYTES
#define MYNAH_EPHEMERIS_MONTH_MAX_BYTES (220000)
#endif

/** Static bright-star catalog (J2000); Stellarium-compatible data, not a live Stellarium host. */
#ifndef MYNAH_STARS_CATALOG_URL
#define MYNAH_STARS_CATALOG_URL "https://ephemeris.castalia.institute/data/stars/bright-stars.json"
#endif

#ifndef MYNAH_STARS_CATALOG_MAX_BYTES
#define MYNAH_STARS_CATALOG_MAX_BYTES (16384)
#endif
