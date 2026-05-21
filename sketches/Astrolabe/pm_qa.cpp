#include "pm_qa.h"

#include <Arduino.h>
#include <cstring>

#include "pm_gesture.h"
#include "pm_side_buttons.h"

static const char *gesture_kind_name(PmGestureKind k) {
  switch (k) {
    case PmGestureKind::Tap:
      return "tap";
    case PmGestureKind::SwipeUp:
      return "swipe up";
    case PmGestureKind::SwipeDown:
      return "swipe down";
    case PmGestureKind::SwipeLeft:
      return "swipe left";
    case PmGestureKind::SwipeRight:
      return "swipe right";
    case PmGestureKind::LongPress:
      return "long press";
    default:
      return "gesture";
  }
}

static bool parse_swipe(const char *p, PmGestureKind *out) {
  if (strncmp(p, "swipe ", 6) != 0) {
    return false;
  }
  p += 6;
  if (strcmp(p, "left") == 0) {
    *out = PmGestureKind::SwipeLeft;
    return true;
  }
  if (strcmp(p, "right") == 0) {
    *out = PmGestureKind::SwipeRight;
    return true;
  }
  if (strcmp(p, "up") == 0) {
    *out = PmGestureKind::SwipeUp;
    return true;
  }
  if (strcmp(p, "down") == 0) {
    *out = PmGestureKind::SwipeDown;
    return true;
  }
  return false;
}

bool pm_qa_inject_command(const char *args) {
  if (!args) {
    return false;
  }
  while (*args == ' ') {
    ++args;
  }
  if (strncmp(args, "inject ", 7) != 0) {
    return false;
  }
  const char *p = args + 7;
  while (*p == ' ') {
    ++p;
  }

  if (strcmp(p, "boot") == 0) {
    pm_side_buttons_inject(PM_SIDE_BTN_BOOT);
    Serial.println("qa: inject boot");
    return true;
  }
  if (strcmp(p, "pwr") == 0 || strcmp(p, "pwr press") == 0) {
    pm_side_buttons_inject(PM_SIDE_BTN_PWR);
    Serial.println("qa: inject pwr");
    return true;
  }
  if (strcmp(p, "pwr hold") == 0) {
    pm_side_buttons_inject_pek_hold(true);
    Serial.println("qa: inject pwr hold");
    return true;
  }
  if (strcmp(p, "pwr release") == 0) {
    pm_side_buttons_inject_pek_hold(false);
    Serial.println("qa: inject pwr release");
    return true;
  }

  PmGestureKind kind = PmGestureKind::None;
  int16_t x = 233;
  int16_t y = 233;
  if (strncmp(p, "tap ", 4) == 0) {
    kind = PmGestureKind::Tap;
    int ix = 0;
    int iy = 0;
    if (sscanf(p + 4, "%d %d", &ix, &iy) != 2) {
      Serial.println("qa: usage: inject tap X Y");
      return true;
    }
    x = static_cast<int16_t>(ix);
    y = static_cast<int16_t>(iy);
  } else if (strncmp(p, "double_tap ", 11) == 0) {
    kind = PmGestureKind::DoubleTap;
    int ix = 0;
    int iy = 0;
    if (sscanf(p + 11, "%d %d", &ix, &iy) != 2) {
      Serial.println("qa: usage: inject double_tap X Y");
      return true;
    }
    x = static_cast<int16_t>(ix);
    y = static_cast<int16_t>(iy);
  } else if (!parse_swipe(p, &kind)) {
    Serial.println("qa: usage: inject swipe left|right|up|down | tap X Y | boot | pwr");
    return true;
  }

  pm_gesture_inject(kind, x, y);
  Serial.printf("qa: inject %s @ %d,%d\n", gesture_kind_name(kind), static_cast<int>(x),
                static_cast<int>(y));
  return true;
}
