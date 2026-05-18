#pragma once

#include <cstdint>

/** Three FFT panels: IN low/high (left top/bottom), OUT (right center). */
void pm_face_spectrum_on_enter(void);
void pm_face_spectrum_on_leave(void);
void pm_face_spectrum_tick(void);
void pm_face_spectrum_draw(uint16_t bg);
