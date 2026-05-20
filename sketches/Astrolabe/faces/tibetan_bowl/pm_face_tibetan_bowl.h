#pragma once

#include <cstdint>

void pm_face_tibetan_bowl_draw(void);

/** Poll touch: rim drag sustains bowl; center tap strikes. Returns true if repaint needed. */
bool pm_face_tibetan_bowl_touch_tick(uint32_t now_ms);

/** After a tangential rim drag, skip one horizontal face-swipe. */
bool pm_face_tibetan_bowl_consume_rim_swipe_block(void);

/** Swipe up/down on face: cycle root → crown like the Chakra face. */
int pm_face_tibetan_bowl_cycle_chakra(int delta);

void pm_face_tibetan_bowl_stop(void);

bool pm_face_tibetan_bowl_anim_tick(uint32_t now_ms);

/** Resonator energy 0..1 for visuals. */
float pm_face_tibetan_bowl_energy(void);

/** Current chakra region 0..6 around the rim. */
int pm_face_tibetan_bowl_chakra_index(void);
