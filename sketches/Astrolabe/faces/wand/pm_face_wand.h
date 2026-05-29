#pragma once

/** Wand face — always-listening faculty pendant UI (see atom/ for AtomS3R hardware). */

void pm_face_wand_draw(void);
void pm_face_wand_draw_voice_screen(const char *status, bool speaking, float thinking_progress);
