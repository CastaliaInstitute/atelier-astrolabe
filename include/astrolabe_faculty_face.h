#pragma once

/*
 * Faculty face — always-listening Castalia faculty conversation UI.
 *
 * Used by dedicated faculty firmware (e.g. Waveshare ESP32-S3 Touch AMOLED 1.8″
 * under faculty18/, 1.75C under faculty175/, M5 AtomS3R under facultyatom/). Distinct from face=wand on the
 * round watch Faculty implementation (astrolabe175c/main/).
 */

#define ASTROLABE_FACULTY_FACE_NAME "faculty"
#define ASTROLABE_FACULTY_OTA_CHANNEL "astrolabe-faculty-amoled18"
#define ASTROLABE_FACULTY_DEFAULT_FACULTY_SLUG "a.darwin"
#define ASTROLABE_FACULTY_DEFAULT_FACULTY_NAME "Charles Darwin"

#define ASTROLABE_FACULTY_SYSTEM_INSTRUCTION \
    "You are Castalia faculty on Astrolabe: a voice conversation with a named faculty member. " \
    "Infer which faculty the user wants from their speech (for example \"ask Einstein about relativity\"). " \
    "Route to ask-faculty when they address a faculty member by name. " \
    "Keep answers concise and spoken aloud (under 25 seconds)."
