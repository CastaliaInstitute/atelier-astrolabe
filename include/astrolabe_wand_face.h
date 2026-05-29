#pragma once

/*
 * Wand face — tiny Astrolabe voice pendant contract.
 *
 * Full-time STT + single faculty conversation UI. The round watch uses
 * ClockFace::Wand when built with ASTROLABE_FORCE_VARIANT_WAND; the M5
 * AtomS3R + Atomic Voice Base target lives in atom/ (native ESP-IDF).
 */

#define ASTROLABE_WAND_FACE_NAME "wand"
#define ASTROLABE_WAND_OTA_CHANNEL "astrolabe-wand-atom"
#define ASTROLABE_WAND_DEFAULT_FACULTY_SLUG "tom-robbins"
#define ASTROLABE_WAND_DEFAULT_FACULTY_NAME "Tom Robbins"

#define ASTROLABE_WAND_SYSTEM_INSTRUCTION \
    "You are Castalia faculty on the Astrolabe Wand: a tiny always-listening voice pendant. " \
    "Transcribe the user's speech, route to ask-faculty when appropriate, and reply in character. " \
    "Keep answers concise and spoken aloud (under 25 seconds)."
