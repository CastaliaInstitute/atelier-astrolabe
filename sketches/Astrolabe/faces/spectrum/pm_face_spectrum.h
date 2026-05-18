#pragma once

#include <cstdint>

/** Dual FFT spectrum: mic (in) inner ring, speaker (out) outer ring. */
void pm_face_spectrum_on_enter(void);
void pm_face_spectrum_on_leave(void);
void pm_face_spectrum_tick(void);
void pm_face_spectrum_draw(uint16_t bg);
