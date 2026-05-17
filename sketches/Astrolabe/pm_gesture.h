#pragma once

#include <Arduino.h>
#include <stdint.h>

/** High-level gestures (software + optional multitouch tap). */
enum class PmGestureKind : uint8_t {
  None = 0,
  Tap,
  DoubleTap,
  TripleTap,
  SwipeUp,
  SwipeDown,
  SwipeLeft,
  SwipeRight,
  LongPress,
  /** Short tap with N fingers down together (N = 2..5). */
  MultiFingerTap2,
  MultiFingerTap3,
  MultiFingerTap4,
  MultiFingerTap5,
};

struct PmGestureEvent {
  PmGestureKind kind = PmGestureKind::None;
  int16_t x = 0;
  int16_t y = 0;
};

void pm_gesture_reset();

/** Call each frame from loop(); feeds the internal recognizer from the touch driver. */
void pm_gesture_poll(uint32_t now_ms);

/** Pop one pending gesture (FIFO). Returns false if empty. */
bool pm_gesture_consume(PmGestureEvent *out);
