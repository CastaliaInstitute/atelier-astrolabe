#pragma once

#include <cstdint>

/** Visualization face: swipe up/down cycles polar audio visualizers. */
void pm_face_spectrum_on_enter(void);
void pm_face_spectrum_on_leave(void);
void pm_face_spectrum_tick(void);
/** Swipe up/down on this face (does not change clock face). */
void pm_face_spectrum_cycle(int delta);
int pm_face_spectrum_mode(void);
int pm_face_spectrum_mode_count(void);
const char *pm_face_spectrum_mode_label(void);
void pm_face_spectrum_draw(uint16_t bg);
