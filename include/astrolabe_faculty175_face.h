#pragma once

/*
 * Faculty175 — Waveshare ESP32-S3-Touch-AMOLED-1.75C round faculty pendant (face=faculty).
 *
 * Native ESP-IDF target: faculty175/. Distinct from faculty18 (1.8″ SH8601), facultyatom
 * (M5 AtomS3R), and the round watch Faculty face in astrolabe175c/main/.
 */

#define ASTROLABE_FACULTY175_FACE_NAME "faculty"
#ifndef ASTROLABE_FACULTY175_OTA_CHANNEL
#define ASTROLABE_FACULTY175_OTA_CHANNEL "astrolabe-faculty-amoled175"
#endif
#define ASTROLABE_FACULTY175_DEFAULT_FACULTY_SLUG "a.darwin"
#define ASTROLABE_FACULTY175_DEFAULT_FACULTY_NAME "Charles Darwin"

#define ASTROLABE_FACULTY175_SYSTEM_INSTRUCTION \
    "You are Castalia faculty on Astrolabe: a voice conversation with a named faculty member. " \
    "Infer which faculty the user wants from their speech (for example \"ask Einstein about relativity\"). " \
    "Route to ask-faculty when they address a faculty member by name. " \
    "Keep answers concise and spoken aloud (under 25 seconds)."

/* Aliases for shared faculty face contract names. */
#define ASTROLABE_FACULTY_FACE_NAME ASTROLABE_FACULTY175_FACE_NAME
#define ASTROLABE_FACULTY_OTA_CHANNEL ASTROLABE_FACULTY175_OTA_CHANNEL
#define ASTROLABE_FACULTY_DEFAULT_FACULTY_SLUG ASTROLABE_FACULTY175_DEFAULT_FACULTY_SLUG
#define ASTROLABE_FACULTY_DEFAULT_FACULTY_NAME ASTROLABE_FACULTY175_DEFAULT_FACULTY_NAME
#define ASTROLABE_FACULTY_SYSTEM_INSTRUCTION ASTROLABE_FACULTY175_SYSTEM_INSTRUCTION
