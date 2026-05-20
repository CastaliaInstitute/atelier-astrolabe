#include "pm_gesture.h"

#include <cstring>

#include "pm_config.h"
#include "pm_side_buttons.h"
#include "pm_touch.h"

#ifndef MYNAH_GESTURE_TAP_SLOP_PX
#define MYNAH_GESTURE_TAP_SLOP_PX 32
#endif
#ifndef MYNAH_GESTURE_TAP_MAX_MS
#define MYNAH_GESTURE_TAP_MAX_MS 420
#endif
#ifndef MYNAH_GESTURE_SWIPE_MIN_PX
#define MYNAH_GESTURE_SWIPE_MIN_PX 26
#endif
#ifndef MYNAH_GESTURE_SWIPE_MAX_MS
#define MYNAH_GESTURE_SWIPE_MAX_MS 700
#endif
#ifndef MYNAH_GESTURE_LONG_PRESS_MS
#define MYNAH_GESTURE_LONG_PRESS_MS 720
#endif
#ifndef MYNAH_GESTURE_MULTI_CHAIN_MS
#define MYNAH_GESTURE_MULTI_CHAIN_MS 340
#endif
#ifndef MYNAH_GESTURE_DOUBLE_SLOP_PX
#define MYNAH_GESTURE_DOUBLE_SLOP_PX 56
#endif
#ifndef MYNAH_GESTURE_QUEUE
#define MYNAH_GESTURE_QUEUE 8
#endif

static int16_t i16abs(int16_t v) { return v < 0 ? static_cast<int16_t>(-v) : v; }

static int16_t max_i16(int16_t a, int16_t b) { return a > b ? a : b; }

static int16_t centroid(const int16_t *v, uint8_t n) {
  int32_t s = 0;
  for (uint8_t i = 0; i < n; ++i) {
    s += v[i];
  }
  return static_cast<int16_t>(s / static_cast<int32_t>(n > 0 ? n : 1));
}

static int32_t dist2(int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
  const int32_t dx = static_cast<int32_t>(x1) - x0;
  const int32_t dy = static_cast<int32_t>(y1) - y0;
  return dx * dx + dy * dy;
}

static PmGestureEvent g_q[MYNAH_GESTURE_QUEUE];
static uint8_t g_qh = 0;
static uint8_t g_qt = 0;
static uint8_t g_qn = 0;

static void q_push(const PmGestureEvent &e) {
  if (e.kind == PmGestureKind::None) {
    return;
  }
  if (g_qn >= MYNAH_GESTURE_QUEUE) {
    g_qh = static_cast<uint8_t>((g_qh + 1) % MYNAH_GESTURE_QUEUE);
    --g_qn;
  }
  g_q[g_qt] = e;
  g_qt = static_cast<uint8_t>((g_qt + 1) % MYNAH_GESTURE_QUEUE);
  ++g_qn;
}

void pm_gesture_reset() {
  g_qh = g_qt = g_qn = 0;
  memset(g_q, 0, sizeof(g_q));
}

void pm_gesture_inject(PmGestureKind kind, int16_t x, int16_t y) {
  PmGestureEvent e = {};
  e.kind = kind;
  e.x = x;
  e.y = y;
  q_push(e);
}

bool pm_gesture_consume(PmGestureEvent *out) {
  if (!out || g_qn == 0) {
    return false;
  }
  *out = g_q[g_qh];
  g_qh = static_cast<uint8_t>((g_qh + 1) % MYNAH_GESTURE_QUEUE);
  --g_qn;
  return true;
}

static bool g_down = false;
static bool g_candidate_down = false;
static uint32_t g_t_down = 0;
static uint32_t g_candidate_t = 0;
static int16_t g_x0 = 0;
static int16_t g_y0 = 0;
static int16_t g_candidate_x = 0;
static int16_t g_candidate_y = 0;
static int16_t g_last_cx = 0;
static int16_t g_last_cy = 0;
static int16_t g_madx = 0;
static int16_t g_mady = 0;
static uint8_t g_max_pts = 0;
static uint8_t g_candidate_samples = 0;

static uint8_t g_chain = 0;
static uint32_t g_chain_deadline = 0;
static int16_t g_chain_x = 0;
static int16_t g_chain_y = 0;

bool pm_gesture_touch_down(void) { return g_down; }

static void emit_chain_locked() {
  if (g_chain == 0) {
    return;
  }
  PmGestureEvent ev = {};
  ev.x = g_chain_x;
  ev.y = g_chain_y;
  if (g_chain >= 3) {
    ev.kind = PmGestureKind::TripleTap;
  } else if (g_chain == 2) {
    ev.kind = PmGestureKind::DoubleTap;
  } else {
    ev.kind = PmGestureKind::Tap;
  }
  q_push(ev);
  g_chain = 0;
  g_chain_deadline = 0;
}

static void try_emit_chain_idle(uint32_t now) {
  if (g_chain > 0 && g_chain_deadline != 0 && now >= g_chain_deadline) {
    emit_chain_locked();
  }
}

static PmGestureKind multi_kind(uint8_t n) {
  switch (n) {
    case 2:
      return PmGestureKind::MultiFingerTap2;
    case 3:
      return PmGestureKind::MultiFingerTap3;
    case 4:
      return PmGestureKind::MultiFingerTap4;
    case 5:
      return PmGestureKind::MultiFingerTap5;
    default:
      return PmGestureKind::None;
  }
}

static void on_release(uint32_t now, int16_t cx, int16_t cy) {
  const uint32_t dt = now - g_t_down;
  const int16_t move = max_i16(g_madx, g_mady);
  const int32_t slop2 =
      static_cast<int32_t>(MYNAH_GESTURE_DOUBLE_SLOP_PX) * MYNAH_GESTURE_DOUBLE_SLOP_PX;

  if (g_max_pts >= 2 && dt <= MYNAH_GESTURE_TAP_MAX_MS && move <= MYNAH_GESTURE_TAP_SLOP_PX) {
    g_chain = 0;
    g_chain_deadline = 0;
    const PmGestureKind mk = multi_kind(g_max_pts);
    if (mk != PmGestureKind::None) {
      PmGestureEvent ev = {};
      ev.kind = mk;
      ev.x = cx;
      ev.y = cy;
      q_push(ev);
    }
    return;
  }

  if (dt >= MYNAH_GESTURE_LONG_PRESS_MS && move <= MYNAH_GESTURE_TAP_SLOP_PX) {
    g_chain = 0;
    g_chain_deadline = 0;
    PmGestureEvent ev = {};
    ev.kind = PmGestureKind::LongPress;
    ev.x = cx;
    ev.y = cy;
    q_push(ev);
    return;
  }

  if (dt <= MYNAH_GESTURE_SWIPE_MAX_MS && move >= MYNAH_GESTURE_SWIPE_MIN_PX) {
    g_chain = 0;
    g_chain_deadline = 0;
    /** Bottom-rim drift while holding PTT — not a face swipe. */
    const bool from_ptt_rim = g_y0 >= MYNAH_PTT_MIN_Y && pm_ptt_button_held();
    PmGestureKind g = PmGestureKind::None;
    if (!from_ptt_rim && g_madx > g_mady + 4) {
      g = (cx > g_x0) ? PmGestureKind::SwipeRight : PmGestureKind::SwipeLeft;
    } else if (g_mady > g_madx + 4) {
      /** CST92xx reports Y opposite to GFX for some boards; swap so “up” matches screen top. */
      g = (cy > g_y0) ? PmGestureKind::SwipeUp : PmGestureKind::SwipeDown;
    }
    if (g != PmGestureKind::None) {
      PmGestureEvent ev = {};
      ev.kind = g;
      ev.x = cx;
      ev.y = cy;
      q_push(ev);
    }
    return;
  }

  if (dt <= MYNAH_GESTURE_TAP_MAX_MS && move <= MYNAH_GESTURE_TAP_SLOP_PX && g_max_pts <= 1) {
    if (g_chain > 0 && now <= g_chain_deadline && dist2(cx, cy, g_chain_x, g_chain_y) <= slop2) {
      if (g_chain < 3) {
        ++g_chain;
      }
    } else {
      if (g_chain > 0) {
        emit_chain_locked();
      }
      g_chain = 1;
    }
    g_chain_x = cx;
    g_chain_y = cy;
    g_chain_deadline = now + MYNAH_GESTURE_MULTI_CHAIN_MS;
    return;
  }

  if (g_chain > 0) {
    emit_chain_locked();
  }
}

void pm_gesture_poll(uint32_t now_ms) {
  int16_t xs[5];
  int16_t ys[5];
  const uint8_t n = pm_touch_sample(xs, ys, 5);

  if (n == 0) {
    g_candidate_down = false;
    g_candidate_samples = 0;
    try_emit_chain_idle(now_ms);
    if (g_down) {
      const int16_t cx = g_last_cx;
      const int16_t cy = g_last_cy;
      g_down = false;
      on_release(now_ms, cx, cy);
    }
    return;
  }

  if (!g_down) {
    const int16_t cx = centroid(xs, n);
    const int16_t cy = centroid(ys, n);
    if (!g_candidate_down) {
      g_candidate_down = true;
      g_candidate_samples = 1;
      g_candidate_t = now_ms;
      g_candidate_x = cx;
      g_candidate_y = cy;
      g_max_pts = n;
      return;
    }
    const int16_t dx = i16abs(static_cast<int16_t>(cx - g_candidate_x));
    const int16_t dy = i16abs(static_cast<int16_t>(cy - g_candidate_y));
    const bool fast_swipe_start = max_i16(dx, dy) >= MYNAH_GESTURE_SWIPE_MIN_PX &&
                                  max_i16(dx, dy) > (dx < dy ? dx : dy) + 4;
    ++g_candidate_samples;
    g_max_pts = n > g_max_pts ? n : g_max_pts;
    if (!fast_swipe_start && g_candidate_samples < 2 && now_ms - g_candidate_t < 24u) {
      return;
    }
    g_down = true;
    g_candidate_down = false;
    g_t_down = g_candidate_t;
    g_x0 = g_candidate_x;
    g_y0 = g_candidate_y;
    g_last_cx = cx;
    g_last_cy = cy;
    g_madx = dx;
    g_mady = dy;
    return;
  }

  g_max_pts = n > g_max_pts ? n : g_max_pts;
  g_last_cx = centroid(xs, n);
  g_last_cy = centroid(ys, n);
  g_madx = max_i16(g_madx, i16abs(static_cast<int16_t>(g_last_cx - g_x0)));
  g_mady = max_i16(g_mady, i16abs(static_cast<int16_t>(g_last_cy - g_y0)));
}
