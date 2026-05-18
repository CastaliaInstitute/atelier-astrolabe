#pragma once

#include <cstdint>

/** Draw the active chakra (symbol in its color). */
void pm_face_chakra_draw(void);

/** Swipe down root → crown; swipe up back. Clamped at ends (no wrap). If tone was on, switches to new Hz. */
int pm_face_chakra_cycle(int delta);

/** Tap: start looping solfeggio tone; tap again to stop. */
bool pm_face_chakra_toggle_tone(void);

/** Stop tone when leaving the face (e.g. horizontal swipe). */
void pm_face_chakra_stop(void);

/** Advance gem ripple animation; call from main loop when face is visible. */
bool pm_face_chakra_anim_tick(uint32_t now_ms);

/** Current chakra index 0 (root) .. 6 (crown). */
int pm_face_chakra_index(void);
