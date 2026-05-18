#pragma once

#include <cstdint>

/** Polar audio mandala: radial spectrum, spectrogram rings, waveform ring, petals. */
void pm_face_spectrum_on_enter(void);
void pm_face_spectrum_on_leave(void);
void pm_face_spectrum_tick(void);
/** Tap cycles visual emphasis (full / spectrum ring / petals + scope). */
void pm_face_spectrum_on_tap(void);
int pm_face_spectrum_mode(void);
void pm_face_spectrum_draw(uint16_t bg);
