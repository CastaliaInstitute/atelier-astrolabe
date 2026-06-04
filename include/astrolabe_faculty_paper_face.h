#pragma once

/*
 * FacultyPaper - M5 PaperColor faculty terminal (face=faculty).
 *
 * Native ESP-IDF target: facultypaper/. Distinct from FacultyAtom (M5 AtomS3R),
 * faculty175 (Waveshare round AMOLED), and the main Astrolabe sketch.
 */

#define ASTROLABE_FACULTY_PAPER_FACE_NAME "faculty"
#define ASTROLABE_FACULTY_PAPER_OTA_CHANNEL "astrolabe-faculty-papercolor"
#define ASTROLABE_FACULTY_PAPER_DEFAULT_FACULTY_SLUG "a.einstein"
#define ASTROLABE_FACULTY_PAPER_DEFAULT_FACULTY_NAME "Einstein"

#define ASTROLABE_FACULTY_PAPER_SYSTEM_INSTRUCTION \
    "You are Castalia faculty on Astrolabe FacultyPaper: a color e-paper voice terminal. " \
    "Infer which faculty the user wants from their speech (for example \"ask Einstein about relativity\"). " \
    "Route to ask-faculty when they address a faculty member by name or say ask faculty. " \
    "Keep answers concise and spoken aloud (under 25 seconds)."
