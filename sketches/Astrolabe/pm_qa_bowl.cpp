#include "pm_qa_bowl.h"

#include <Arduino.h>
#include <cmath>
#include <cstring>

#include "faces/pm_faces.h"
#include "faces/shared/pm_face_draw.h"
#include "faces/tibetan_bowl/pm_face_tibetan_bowl.h"
#include "pin_config.h"
#include "pm_touch.h"

static constexpr int kChakraCount = 7;
static constexpr float kPi = 3.14159265f;
static constexpr float kTwoPi = kPi * 2.f;

static void bowl_usage(void) {
  Serial.println(
      "qa bowl: usage: bowl | bowl touch center [hold_ms] | bowl touch rim <0-6> | bowl touch "
      "move <deg> | bowl touch up | bowl touch drag <c0> <c1> [steps] | bowl touch seq | bowl status");
}

static void rim_radii(int *r_inner, int *r_out) {
  const int R = (LCD_WIDTH < LCD_HEIGHT ? LCD_WIDTH : LCD_HEIGHT) / 2;
  *r_out = R - 32;
  *r_inner = *r_out - 58;
}

static float rim_mid_radius(void) {
  int r_in = 0;
  int r_out = 0;
  rim_radii(&r_in, &r_out);
  return static_cast<float>(r_in + r_out) * 0.5f;
}

static void coords_center(int16_t *x, int16_t *y) {
  *x = static_cast<int16_t>(pm_face_lcd_cx);
  *y = static_cast<int16_t>(pm_face_lcd_cy);
}

static void coords_rim_chakra(int chakra, int16_t *x, int16_t *y) {
  int c = chakra % kChakraCount;
  if (c < 0) {
    c += kChakraCount;
  }
  const float theta = -kPi * 0.5f + (static_cast<float>(c) + 0.5f) * (kTwoPi / static_cast<float>(kChakraCount));
  const float r = rim_mid_radius();
  *x = static_cast<int16_t>(static_cast<float>(pm_face_lcd_cx) + r * cosf(theta));
  *y = static_cast<int16_t>(static_cast<float>(pm_face_lcd_cy) + r * sinf(theta));
}

static void coords_rim_deg(float deg_from_top, int16_t *x, int16_t *y) {
  const float theta = -kPi * 0.5f + deg_from_top * (kPi / 180.f);
  const float r = rim_mid_radius();
  *x = static_cast<int16_t>(static_cast<float>(pm_face_lcd_cx) + r * cosf(theta));
  *y = static_cast<int16_t>(static_cast<float>(pm_face_lcd_cy) + r * sinf(theta));
}

static bool on_bowl_face(void) { return pm_faces_current() == ClockFace::TibetanBowl; }

static bool bowl_touch_at(int16_t x, int16_t y, bool *repaint_out) {
  pm_touch_inject_set(x, y);
  bool rep = false;
  if (on_bowl_face()) {
    rep = pm_face_tibetan_bowl_touch_tick(millis());
  }
  if (repaint_out) {
    *repaint_out = rep;
  }
  return true;
}

static bool bowl_touch_up(bool *repaint_out) {
  pm_touch_inject_clear();
  bool rep = false;
  if (on_bowl_face()) {
    rep = pm_face_tibetan_bowl_touch_tick(millis());
  }
  if (repaint_out) {
    *repaint_out = rep;
  }
  return true;
}

static bool cmd_touch(const char *p, bool *repaint_out) {
  if (strncmp(p, "touch ", 6) != 0) {
    return false;
  }
  p += 6;
  while (*p == ' ') {
    ++p;
  }

  if (strcmp(p, "up") == 0 || strcmp(p, "release") == 0) {
    bowl_touch_up(repaint_out);
    Serial.println("qa: bowl touch up");
    return true;
  }

  if (strcmp(p, "center") == 0 || strncmp(p, "center ", 7) == 0) {
    unsigned hold_ms = 80u;
    if (strncmp(p, "center ", 7) == 0) {
      hold_ms = static_cast<unsigned>(atoi(p + 7));
      if (hold_ms < 20u) {
        hold_ms = 20u;
      }
    }
    int16_t x = 0;
    int16_t y = 0;
    coords_center(&x, &y);
    bowl_touch_at(x, y, repaint_out);
    Serial.printf("qa: bowl touch center @ %d,%d hold=%u\n", static_cast<int>(x), static_cast<int>(y), hold_ms);
    delay(hold_ms);
    bowl_touch_up(repaint_out);
    return true;
  }

  if (strncmp(p, "rim ", 4) == 0) {
    const char *arg = p + 4;
    int16_t x = 0;
    int16_t y = 0;
    char *end = nullptr;
    const long n = strtol(arg, &end, 10);
    if (end != arg) {
      coords_rim_chakra(static_cast<int>(n), &x, &y);
    } else {
      coords_rim_deg(static_cast<float>(atof(arg)), &x, &y);
    }
    bowl_touch_at(x, y, repaint_out);
    Serial.printf("qa: bowl touch rim @ %d,%d\n", static_cast<int>(x), static_cast<int>(y));
    return true;
  }

  if (strncmp(p, "move ", 5) == 0) {
    const float deg = static_cast<float>(atof(p + 5));
    int16_t x = 0;
    int16_t y = 0;
    coords_rim_deg(deg, &x, &y);
    bowl_touch_at(x, y, repaint_out);
    Serial.printf("qa: bowl touch move @ %d,%d (%.0f deg)\n", static_cast<int>(x), static_cast<int>(y),
                  static_cast<double>(deg));
    return true;
  }

  if (strncmp(p, "drag ", 5) == 0) {
    int c0 = 0;
    int c1 = 2;
    int steps = 12;
    if (sscanf(p + 5, "%d %d %d", &c0, &c1, &steps) < 2) {
      Serial.println("qa: usage: bowl touch drag <chakra0> <chakra1> [steps]");
      return true;
    }
    if (steps < 4) {
      steps = 4;
    }
    if (steps > 48) {
      steps = 48;
    }
    int16_t x = 0;
    int16_t y = 0;
    coords_rim_chakra(c0, &x, &y);
    bowl_touch_at(x, y, repaint_out);
    Serial.printf("qa: bowl touch drag %d->%d steps=%d\n", c0, c1, steps);
    for (int s = 1; s <= steps; ++s) {
      const float t = static_cast<float>(s) / static_cast<float>(steps);
      const float c0f = static_cast<float>(c0) + 0.5f;
      const float c1f = static_cast<float>(c1) + 0.5f;
      const float cf = c0f + (c1f - c0f) * t;
      const float deg = cf * (360.f / static_cast<float>(kChakraCount));
      coords_rim_deg(deg, &x, &y);
      bowl_touch_at(x, y, repaint_out);
      delay(35);
    }
    bowl_touch_up(repaint_out);
    return true;
  }

  if (strcmp(p, "seq") == 0 || strcmp(p, "sequence") == 0) {
    if (!on_bowl_face()) {
      Serial.println("qa: bowl touch seq: switch to face 12 (bowl) first");
      return true;
    }
    Serial.println("qa: bowl touch seq start");
    int16_t x = 0;
    int16_t y = 0;
    coords_center(&x, &y);
    bowl_touch_at(x, y, repaint_out);
    delay(120);
    bowl_touch_up(repaint_out);
    delay(400);
    coords_rim_chakra(1, &x, &y);
    bowl_touch_at(x, y, repaint_out);
    for (int s = 1; s <= 16; ++s) {
      const float deg = (1.5f + static_cast<float>(s) * (2.f / 16.f)) * (360.f / 7.f);
      coords_rim_deg(deg, &x, &y);
      bowl_touch_at(x, y, repaint_out);
      delay(40);
    }
    bowl_touch_up(repaint_out);
    delay(200);
    coords_center(&x, &y);
    bowl_touch_at(x, y, repaint_out);
    delay(80);
    bowl_touch_up(repaint_out);
    Serial.printf("qa: bowl touch seq done energy=%.3f chakra=%d\n",
                  static_cast<double>(pm_face_tibetan_bowl_energy()),
                  pm_face_tibetan_bowl_chakra_index());
    return true;
  }

  Serial.println("qa: usage: bowl touch center [ms] | rim <0-6> | move <deg> | up | drag c0 c1 [steps] | seq");
  return true;
}

bool pm_qa_bowl_command(const char *args, bool *repaint_out) {
  if (repaint_out) {
    *repaint_out = false;
  }
  if (!args) {
    return false;
  }
  while (*args == ' ') {
    ++args;
  }
  if (*args == '\0') {
    return false;
  }

  if (strcmp(args, "status") == 0) {
    Serial.printf("qa: bowl status face=%d energy=%.3f chakra=%d inject=%d\n",
                  on_bowl_face() ? 1 : 0, static_cast<double>(pm_face_tibetan_bowl_energy()),
                  pm_face_tibetan_bowl_chakra_index(), pm_touch_inject_active() ? 1 : 0);
    return true;
  }

  if (cmd_touch(args, repaint_out)) {
    return true;
  }

  if (strcmp(args, "help") == 0 || strcmp(args, "?") == 0) {
    bowl_usage();
    return true;
  }

  return false;
}
