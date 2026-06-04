#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    FACULTY175_GESTURE_NONE = 0,
    FACULTY175_GESTURE_SWIPE_LEFT,
    FACULTY175_GESTURE_SWIPE_RIGHT,
    FACULTY175_GESTURE_SWIPE_UP,
    FACULTY175_GESTURE_SWIPE_DOWN,
    FACULTY175_GESTURE_TAP,
    FACULTY175_GESTURE_LONG_TAP,
    FACULTY175_GESTURE_BEZEL_TAP,
    FACULTY175_GESTURE_BEZEL_ROTATE_CW,
    FACULTY175_GESTURE_BEZEL_ROTATE_CCW,
} faculty175_gesture_kind_t;

typedef struct {
    faculty175_gesture_kind_t kind;
    int16_t x;
    int16_t y;
    int16_t value;
} faculty175_gesture_t;

/** Poll touch and classify swipes (call ~20 Hz from main loop). */
void faculty175_gesture_poll(uint32_t now_ms);

/** Returns true when a gesture is ready; clears the queue slot. */
bool faculty175_gesture_consume(faculty175_gesture_t *out);

/** 100 Hz touch poll (call faculty175_gesture_consume from main loop). */
void faculty175_gesture_start_task(void);
