#pragma once

#include <cstdint>

/** Offline-first note capture face; PWR hold records Commonplace notes. */
void pm_face_notes_draw(void);
void pm_face_notes_draw_recording(float progress, uint32_t elapsed_ms);
