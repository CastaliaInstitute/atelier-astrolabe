#pragma once

#include <cstdint>

/** Draw the active chakra (symbol in its color). */
void pm_face_chakra_draw(void);

/** Swipe up/down cycles through chakras with wrap. Existing resonances keep decaying. */
int pm_face_chakra_cycle(int delta);

/** Tap: strike the current solfeggio chakra resonance. */
bool pm_face_chakra_strike(void);

/** Stop tone when leaving the face (e.g. horizontal swipe). */
void pm_face_chakra_stop(void);

/** Advance gem ripple animation; call from main loop when face is visible. */
bool pm_face_chakra_anim_tick(uint32_t now_ms);

/** Current chakra index 0 (root) .. 6 (crown). */
int pm_face_chakra_index(void);
