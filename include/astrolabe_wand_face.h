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
#define ASTROLABE_WAND_DEFAULT_FACULTY_SLUG "a.einstein"
#define ASTROLABE_WAND_DEFAULT_FACULTY_NAME "Einstein"

#define ASTROLABE_WAND_SYSTEM_INSTRUCTION \
    "You are Castalia faculty on the Astrolabe Wand: a tiny always-listening voice pendant. " \
    "Infer which faculty the user wants from their speech (for example \"ask Einstein about relativity\"). " \
    "Route to ask-faculty when they address a faculty member by name or say ask faculty. " \
    "Keep answers concise and spoken aloud (under 25 seconds)."
