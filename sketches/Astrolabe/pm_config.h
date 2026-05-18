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
