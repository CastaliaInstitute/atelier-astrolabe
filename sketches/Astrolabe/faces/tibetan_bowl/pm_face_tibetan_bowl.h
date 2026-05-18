#pragma once

#include <cstdint>

/** Draw singing-bowl face (preset label + bowl graphic + rim-drag feedback). */
void pm_face_tibetan_bowl_draw(void);

/** Swipe up/down: cycle bowl presets. Returns new index 0..n-1. */
int pm_face_tibetan_bowl_cycle(int delta);

/**
 * Poll touch each frame: finger drag on the 24h rainbow rim excites the bowl.
 * Returns true when the face should repaint (finger/ripple animation).
 */
bool pm_face_tibetan_bowl_touch_tick(uint32_t now_ms);

/** After a rim drag stroke, skip one horizontal face-swipe (tangential motion). */
bool pm_face_tibetan_bowl_consume_rim_swipe_block(void);

void pm_face_tibetan_bowl_stop(void);

bool pm_face_tibetan_bowl_anim_tick(uint32_t now_ms);

int pm_face_tibetan_bowl_index(void);
