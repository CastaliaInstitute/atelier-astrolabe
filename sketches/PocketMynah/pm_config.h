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
