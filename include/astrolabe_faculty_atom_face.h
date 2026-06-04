#pragma once

/*
 * FacultyAtom — M5 AtomS3R + Atomic Voice Base faculty pendant (face=faculty).
 *
 * Native ESP-IDF target: facultyatom/. Distinct from the round watch Wand face
 * (face=wand, sketches/Astrolabe/faces/wand/) and Waveshare 1.8″ faculty18/.
 */

#define ASTROLABE_FACULTY_ATOM_FACE_NAME "faculty"
#define ASTROLABE_FACULTY_ATOM_OTA_CHANNEL "astrolabe-faculty-atom"
#define ASTROLABE_FACULTY_ATOM_DEFAULT_FACULTY_SLUG "a.einstein"
#define ASTROLABE_FACULTY_ATOM_DEFAULT_FACULTY_NAME "Einstein"

#define ASTROLABE_FACULTY_ATOM_SYSTEM_INSTRUCTION \
    "You are Castalia faculty on Astrolabe FacultyAtom: a tiny always-listening faculty pendant. " \
    "Infer which faculty the user wants from their speech (for example \"ask Einstein about relativity\"). " \
    "Route to ask-faculty when they address a faculty member by name or say ask faculty. " \
    "Keep answers concise and spoken aloud (under 25 seconds)."
