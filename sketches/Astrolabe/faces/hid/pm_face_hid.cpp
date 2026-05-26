#include "faces/hid/pm_face_hid.h"

#include <Arduino_GFX_Library.h>
#include <cmath>
#include <cstdio>

#include "faces/shared/pm_face_draw.h"
#include "pin_config.h"
#include "pm_colmi_r02.h"
#include "pm_display.h"
#include "pm_motion.h"
#include "pm_touch.h"
#include "pm_usb_hid.h"

static constexpr uint8_t kMouseLeft = 0x01;
static constexpr uint8_t kMouseRight = 0x02;
enum class HidFaceMode : uint8_t {
  Touchpad = 0,
  Space,
};

static bool s_armed = false;
static bool s_touching = false;
static int16_t s_last_x = 0;
static int16_t s_last_y = 0;
static uint32_t s_last_report_ms = 0;
static uint32_t s_last_activity_ms = 0;
static int s_last_dx = 0;
static int s_last_dy = 0;
static HidFaceMode s_mode = HidFaceMode::Touchpad;
static bool s_space_neutral_valid = false;
static float s_neutral_pitch = 0.f;
static float s_neutral_roll = 0.f;
static float s_neutral_yaw = 0.f;
static float s_neutral_ax = 0.f;
static float s_neutral_ay = 0.f;
static float s_neutral_az = 1.f;
static int8_t s_axis_x = 0;
static int8_t s_axis_y = 0;
static bool s_space_using_ring = false;
static int8_t s_axis_z = 0;
static int8_t s_axis_rz = 0;
static int8_t s_axis_rx = 0;
static int8_t s_axis_ry = 0;

static int8_t clamp_i8(int value) {
  if (value > 127) {
    return 127;
  }
  if (value < -127) {
    return -127;
  }
  return static_cast<int8_t>(value);
}

static int8_t axis_from_float(float value, float scale) {
  int v = static_cast<int>(lrintf(value * scale));
  if (v > 127) {
    v = 127;
  } else if (v < -127) {
    v = -127;
  }
  if (v > -4 && v < 4) {
    v = 0;
  }
  return static_cast<int8_t>(v);
}

static float wrap_deg_delta(float deg) {
  while (deg > 180.f) {
    deg -= 360.f;
  }
  while (deg < -180.f) {
    deg += 360.f;
  }
  return deg;
}

static const char *mode_label(void) {
  switch (s_mode) {
    case HidFaceMode::Space:
      return "space";
    case HidFaceMode::Touchpad:
    default:
      return "touchpad";
  }
}

static uint16_t status_color(void) {
  if (!pm_usb_hid_enabled()) {
    return pm_gfx->color565(210, 90, 80);
  }
  if (!pm_usb_hid_ready()) {
    return pm_gfx->color565(230, 190, 80);
  }
  return s_armed ? pm_gfx->color565(80, 220, 170) : pm_gfx->color565(110, 150, 180);
}

static const char *status_label(void) {
  if (!pm_usb_hid_enabled()) {
    return "HID firmware required";
  }
  if (!pm_usb_hid_ready()) {
    return "USB not mounted";
  }
  if (s_mode == HidFaceMode::Space && pm_colmi_r02_streaming()) {
    return s_armed ? "ring HID armed" : "ring ready";
  }
  if (!pm_motion_has_6dof() && s_mode == HidFaceMode::Space && !pm_colmi_r02_ready()) {
    return "IMU unavailable";
  }
  return s_armed ? "HID armed" : "tap to arm";
}

static bool read_space_pose(float *pitch, float *roll, float *yaw, float *ax, float *ay, float *az) {
  if (!pitch || !roll || !yaw || !ax || !ay || !az) {
    return false;
  }
  pm_colmi_r02_tick(millis());
  PmColmiR02AccelSample ring = {};
  if (pm_colmi_r02_accel_g(&ring)) {
    *ax = ring.x_g;
    *ay = ring.y_g;
    *az = ring.z_g;
    constexpr float kDeg = 180.f / pm_face_k_pi;
    *pitch = atan2f(-*ax, sqrtf((*ay) * (*ay) + (*az) * (*az))) * kDeg;
    *roll = atan2f(*ay, *az) * kDeg;
    *yaw = 0.f;
    s_space_using_ring = true;
    return true;
  }
  pm_motion_tick(millis());
  if (!pm_motion_accel_norm(ax, ay, az)) {
    return false;
  }
  constexpr float kDeg = 180.f / pm_face_k_pi;
  *pitch = atan2f(-*ax, sqrtf((*ay) * (*ay) + (*az) * (*az))) * kDeg;
  *roll = atan2f(*ay, *az) * kDeg;
  *yaw = pm_motion_yaw_deg();
  s_space_using_ring = false;
  return true;
}

static bool calibrate_space_neutral(void) {
  float pitch = 0.f;
  float roll = 0.f;
  float yaw = 0.f;
  float ax = 0.f;
  float ay = 0.f;
  float az = 0.f;
  if (!read_space_pose(&pitch, &roll, &yaw, &ax, &ay, &az)) {
    return false;
  }
  s_neutral_pitch = pitch;
  s_neutral_roll = roll;
  s_neutral_yaw = yaw;
  s_neutral_ax = ax;
  s_neutral_ay = ay;
  s_neutral_az = az;
  s_space_neutral_valid = true;
  s_axis_x = s_axis_y = s_axis_z = s_axis_rz = s_axis_rx = s_axis_ry = 0;
  return true;
}

static void draw_trackpad(uint16_t accent) {
  const int x = 94;
  const int y = 118;
  const int w = 292;
  const int h = 210;
  const uint16_t panel = pm_gfx->color565(14, 20, 28);
  const uint16_t grid = pm_gfx->color565(32, 48, 58);
  pm_gfx->fillRoundRect(x, y, w, h, 8, panel);
  pm_gfx->drawRoundRect(x, y, w, h, 8, accent);
  for (int i = 1; i < 4; ++i) {
    const int gx = x + (w * i) / 4;
    pm_gfx->drawFastVLine(gx, y + 12, h - 24, grid);
  }
  for (int i = 1; i < 3; ++i) {
    const int gy = y + (h * i) / 3;
    pm_gfx->drawFastHLine(x + 12, gy, w - 24, grid);
  }
  const int px = x + w / 2 + s_last_dx * 3;
  const int py = y + h / 2 + s_last_dy * 3;
  pm_gfx->fillCircle(px, py, s_armed ? 10 : 6, accent);
}

void pm_face_hid_draw(void) {
  const uint16_t bg = pm_gfx->color565(5, 9, 13);
  const uint16_t text = pm_gfx->color565(226, 236, 232);
  const uint16_t dim = pm_gfx->color565(125, 145, 150);
  const uint16_t accent = status_color();
  pm_gfx->fillScreen(bg);
  pm_face_draw_centered_line("HID Touchpad", 40, text, 2, 2);
  char top[48];
  snprintf(top, sizeof(top), "%s  %s", mode_label(), status_label());
  pm_face_draw_centered_line(top, 76, accent, 1, 1);
  draw_trackpad(accent);

  char motion[40];
  if (s_mode == HidFaceMode::Space) {
    snprintf(motion, sizeof(motion), "%s x %+d y %+d z %+d", s_space_using_ring ? "r02" : "imu", s_axis_x,
             s_axis_y, s_axis_z);
  } else {
    snprintf(motion, sizeof(motion), "dx %+d  dy %+d", s_last_dx, s_last_dy);
  }
  pm_face_draw_centered_line(motion, 354, dim, 1, 1);
  if (s_mode == HidFaceMode::Space) {
    char rot[40];
    snprintf(rot, sizeof(rot), "rx %+d ry %+d rz %+d", s_axis_rx, s_axis_ry, s_axis_rz);
    pm_face_draw_centered_line(rot, 382, dim, 1, 1);
    pm_face_draw_centered_line(pm_colmi_r02_status_label(), 410, dim, 1, 1);
  } else {
    pm_face_draw_centered_line("tap click  hold off  2f right", 392, dim, 1, 1);
    pm_face_draw_centered_line("swipe left/right mode", 418, dim, 1, 1);
  }
  if (s_last_activity_ms != 0 && millis() - s_last_activity_ms < 700u) {
    pm_gfx->fillCircle(pm_face_lcd_cx, 424, 4, accent);
  }
}

void pm_face_hid_on_enter(void) {
  s_touching = false;
  s_last_report_ms = 0;
  s_last_dx = 0;
  s_last_dy = 0;
  s_axis_x = s_axis_y = s_axis_z = s_axis_rz = s_axis_rx = s_axis_ry = 0;
  s_space_using_ring = false;
  (void)pm_colmi_r02_begin();
}

void pm_face_hid_on_leave(void) {
  s_armed = false;
  s_touching = false;
  pm_colmi_r02_stop();
}

bool pm_face_hid_touch_tick(uint32_t now_ms) {
  if (!s_armed || !pm_usb_hid_ready()) {
    s_touching = false;
    pm_colmi_r02_tick(now_ms);
    return false;
  }
  if (now_ms - s_last_report_ms < 12u) {
    return false;
  }

  if (s_mode == HidFaceMode::Space) {
    pm_colmi_r02_tick(now_ms);
    if (!s_space_neutral_valid && !calibrate_space_neutral()) {
      return false;
    }
    float pitch = 0.f;
    float roll = 0.f;
    float yaw = 0.f;
    float ax = 0.f;
    float ay = 0.f;
    float az = 0.f;
    if (!read_space_pose(&pitch, &roll, &yaw, &ax, &ay, &az)) {
      return false;
    }
    s_axis_x = axis_from_float(roll - s_neutral_roll, 4.0f);
    s_axis_y = axis_from_float(pitch - s_neutral_pitch, 4.0f);
    s_axis_z = axis_from_float(az - s_neutral_az, 110.0f);
    s_axis_rz = axis_from_float(wrap_deg_delta(yaw - s_neutral_yaw), 2.4f);
    s_axis_rx = axis_from_float(ax - s_neutral_ax, 105.0f);
    s_axis_ry = axis_from_float(ay - s_neutral_ay, 105.0f);
    s_last_report_ms = now_ms;
    s_last_activity_ms = now_ms;
    return pm_usb_hid_gamepad_send(s_axis_x, s_axis_y, s_axis_z, s_axis_rz, s_axis_rx,
                                   s_axis_ry, s_armed ? 1u : 0u);
  }

  int16_t xs[1] = {0};
  int16_t ys[1] = {0};
  if (pm_touch_sample(xs, ys, 1) == 0) {
    s_touching = false;
    return false;
  }

  if (!s_touching) {
    s_touching = true;
    s_last_x = xs[0];
    s_last_y = ys[0];
    return false;
  }

  const int dx = static_cast<int>(xs[0] - s_last_x);
  const int dy = static_cast<int>(ys[0] - s_last_y);
  s_last_x = xs[0];
  s_last_y = ys[0];
  if (std::abs(dx) < 2 && std::abs(dy) < 2) {
    return false;
  }

  s_last_report_ms = now_ms;
  s_last_activity_ms = now_ms;
  s_last_dx = dx;
  s_last_dy = dy;
  return pm_usb_hid_mouse_move(clamp_i8(dx), clamp_i8(dy), 0, 0);
}

bool pm_face_hid_on_gesture(PmGestureKind kind, int16_t x, int16_t y, char *banner, size_t banner_cap) {
  (void)x;
  (void)y;
  if (!banner || banner_cap == 0) {
    return true;
  }
  if (!pm_usb_hid_enabled()) {
    snprintf(banner, banner_cap, "hid: build target");
    return true;
  }
  if (kind == PmGestureKind::SwipeLeft || kind == PmGestureKind::SwipeRight) {
    s_mode = s_mode == HidFaceMode::Touchpad ? HidFaceMode::Space : HidFaceMode::Touchpad;
    s_touching = false;
    s_space_neutral_valid = false;
    s_axis_x = s_axis_y = s_axis_z = s_axis_rz = s_axis_rx = s_axis_ry = 0;
    snprintf(banner, banner_cap, "hid: %s", mode_label());
    return true;
  }
  if (kind == PmGestureKind::LongPress) {
    if (s_mode == HidFaceMode::Space) {
      if (calibrate_space_neutral()) {
        s_armed = pm_usb_hid_ready();
        snprintf(banner, banner_cap, "hid: zeroed");
      } else {
        snprintf(banner, banner_cap, "hid: no imu");
      }
    } else {
      s_armed = false;
      s_touching = false;
      snprintf(banner, banner_cap, "hid: off");
    }
    return true;
  }
  if (kind == PmGestureKind::MultiFingerTap2) {
    if (s_armed && pm_usb_hid_mouse_click(kMouseRight)) {
      snprintf(banner, banner_cap, "hid: right click");
    } else {
      snprintf(banner, banner_cap, "hid: not ready");
    }
    return true;
  }
  if (kind == PmGestureKind::Tap) {
    if (!s_armed) {
      s_armed = pm_usb_hid_ready();
      if (s_armed && s_mode == HidFaceMode::Space) {
        (void)calibrate_space_neutral();
      }
      snprintf(banner, banner_cap, "hid: %s", s_armed ? "armed" : "not mounted");
    } else if (s_mode == HidFaceMode::Space) {
      s_armed = false;
      (void)pm_usb_hid_gamepad_send(0, 0, 0, 0, 0, 0, 0);
      snprintf(banner, banner_cap, "hid: off");
    } else if (pm_usb_hid_mouse_click(kMouseLeft)) {
      snprintf(banner, banner_cap, "hid: click");
    } else {
      snprintf(banner, banner_cap, "hid: not ready");
    }
    return true;
  }
  if (kind == PmGestureKind::SwipeUp || kind == PmGestureKind::SwipeDown) {
    if (s_mode == HidFaceMode::Space) {
      snprintf(banner, banner_cap, "hid: %s", s_armed ? "space" : "tap to arm");
      return true;
    }
    const int8_t wheel = kind == PmGestureKind::SwipeUp ? 3 : -3;
    if (s_armed && pm_usb_hid_mouse_move(0, 0, wheel, 0)) {
      snprintf(banner, banner_cap, "hid: scroll");
    } else {
      snprintf(banner, banner_cap, "hid: not ready");
    }
    return true;
  }
  snprintf(banner, banner_cap, "hid: %s", s_armed ? "armed" : "tap to arm");
  return true;
}
