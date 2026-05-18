#pragma once

#include <cstdint>

/** Musical metronome: pendulum, beat ring, BPM. Tap center = start/stop; swipe up/down = ±5 BPM. */
void pm_face_metronome_draw(uint32_t now_ms);
void pm_face_metronome_on_face_leave(void);
void pm_face_metronome_toggle_running(void);
void pm_face_metronome_adjust_bpm(int delta);
bool pm_face_metronome_running(void);
int pm_face_metronome_bpm(void);
/** Advance beat scheduler; returns true on a new beat. */
bool pm_face_metronome_tick(uint32_t now_ms);
/** True when animation or beat flash needs another frame (~30 Hz while running). */
bool pm_face_metronome_wants_repaint(uint32_t now_ms);
