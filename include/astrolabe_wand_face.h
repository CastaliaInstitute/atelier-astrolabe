#pragma once

/*
 * Wand face — round watch pendant UI (face=wand).
 *
 * Used by ClockFace::Wand in sketches/Astrolabe/. For M5 AtomS3R hardware use
 * facultyatom/ and astrolabe_faculty_atom_face.h (face=faculty).
 */

#define ASTROLABE_WAND_FACE_NAME "wand"
#define ASTROLABE_WAND_OTA_CHANNEL "astrolabe-wand"
#define ASTROLABE_WAND_DEFAULT_FACULTY_SLUG "a.darwin"
#define ASTROLABE_WAND_DEFAULT_FACULTY_NAME "Charles Darwin"

#define ASTROLABE_WAND_SYSTEM_INSTRUCTION \
    "You are Castalia faculty on the Astrolabe Wand: a tiny always-listening voice pendant. " \
    "Infer which faculty the user wants from their speech (for example \"ask Einstein about relativity\"). " \
    "Route to ask-faculty when they address a faculty member by name or say ask faculty. " \
    "Keep answers concise and spoken aloud (under 25 seconds)."
