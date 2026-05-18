#pragma once

#include <cstdint>

/** Draw the active chakra (symbol in its color). */
void pm_face_chakra_draw(void);

/** Swipe up/down: cycle root → crown. Returns new index 0..6. */
int pm_face_chakra_cycle(int delta);

/** Play the solfeggio tone for the active chakra. */
bool pm_face_chakra_play_tone(void);

/** Advance ripple animation; call from main loop when face is visible. */
bool pm_face_chakra_anim_tick(uint32_t now_ms);

/** Current chakra index 0 (root) .. 6 (crown). */
int pm_face_chakra_index(void);
