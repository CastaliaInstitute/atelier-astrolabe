#pragma once

/** Wand face — round watch pendant UI (see facultyatom/ for M5 AtomS3R hardware). */

void pm_face_wand_draw(void);
void pm_face_wand_draw_voice_screen(const char *status, bool speaking, float thinking_progress);
