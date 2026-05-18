#pragma once

#include <cstdint>

/** Three FFT panels: dual mic in (left top/bottom), speaker out (right center). */
void pm_face_spectrum_on_enter(void);
void pm_face_spectrum_on_leave(void);
void pm_face_spectrum_tick(void);
void pm_face_spectrum_draw(uint16_t bg);
