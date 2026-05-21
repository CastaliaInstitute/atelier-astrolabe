#include "faces/radar/pm_face_radar.h"

#include <cmath>
#include <cstdio>

#include "faces/shared/pm_face_draw.h"
#include "pin_config.h"
#include "pm_display.h"
#include "pm_motion.h"
#include "pm_presence.h"
#include "pm_presence_graph.h"
#include "pm_wifi_ntp.h"

static uint32_t s_last_motion_ms = 0;

static void map_graph_xy(float x_m, float y_m, int rcx, int rcy, float ppm, int *px, int *py) {
  *px = rcx + static_cast<int>(lrintf(x_m * ppm));
  *py = rcy + static_cast<int>(lrintf(y_m * ppm));
}

void pm_face_radar_on_enter(void) {
  s_last_motion_ms = 0;
  pm_presence_graph_reset();
  if (!pm_presence_ble_begin()) {
    pm_wifi_pause_for_ble();
    if (!pm_presence_ble_begin()) {
      pm_presence_seed_demo_peers(millis());
    }
  }
  pm_presence_ble_set_radar_active(true);
}

void pm_face_radar_on_leave(void) {
  pm_presence_ble_set_radar_active(false);
  pm_presence_ble_end();
  pm_wifi_resume_after_ble();
}

void pm_face_radar_tick(uint32_t now_ms) {
  if (s_last_motion_ms == 0) {
    s_last_motion_ms = now_ms;
  }
  static float s_prev_yaw = 0.f;
  pm_motion_tick(now_ms);
  if (pm_motion_has_6dof()) {
    const float yaw = pm_motion_yaw_deg();
    float delta = yaw - s_prev_yaw;
    while (delta > 180.f) {
      delta -= 360.f;
    }
    while (delta < -180.f) {
      delta += 360.f;
    }
    pm_presence_graph_rotate(-delta);
    s_prev_yaw = yaw;
  }
  pm_presence_graph_step(now_ms, 6);
  s_last_motion_ms = now_ms;
}

void pm_face_radar_draw(const struct tm *tm, bool valid) {
  (void)valid;

  pm_gfx->fillScreen(pm_gfx->color565(12, 14, 18));

  const int rcx = pm_face_lcd_cx;
  const int rcy = pm_face_lcd_cy;
  const float ppm = pm_presence_graph_px_per_meter();
  const uint16_t c_edge_self = pm_gfx->color565(56, 120, 160);
  const uint16_t c_edge_peer = pm_gfx->color565(72, 88, 110);
  const uint16_t c_self = pm_gfx->color565(120, 220, 255);
  const uint16_t c_label = pm_gfx->color565(150, 158, 170);
  const uint16_t c_peer = pm_gfx->color565(255, 196, 96);
  const uint16_t c_loc = pm_gfx->color565(140, 200, 255);

  char title[24];
  if (valid && tm) {
    snprintf(title, sizeof(title), "%02d:%02d", tm->tm_hour, tm->tm_min);
  } else {
    snprintf(title, sizeof(title), "--:--");
  }
  pm_face_draw_centered_line(title, 58, pm_gfx->color565(0x56, 0xd3, 0x64), 1, 1);
  pm_face_draw_centered_line("NEARBY", 78, c_label, 1, 1);

  /** Faint range rings in meters (force-graph scale). */
  pm_gfx->setTextSize(1, 1);
  for (int ring_m = 2; ring_m <= 10; ring_m += 2) {
    const int rr = static_cast<int>(lrintf(static_cast<float>(ring_m) * ppm));
    if (rr > 8 && rr < 120) {
      pm_gfx->drawCircle(rcx, rcy, rr, pm_gfx->color565(40, 48, 58));
    }
  }

  const size_t n_edges = pm_presence_graph_edge_count();
  for (size_t e = 0; e < n_edges; ++e) {
    const PmPresenceGraphEdge *edge = pm_presence_graph_edge(e);
    if (!edge) {
      continue;
    }
    int ia = -1;
    int ib = -1;
    int ax = 0;
    int ay = 0;
    int bx = 0;
    int by = 0;
    const size_t n_nodes = pm_presence_graph_node_count();
    for (size_t i = 0; i < n_nodes; ++i) {
      const PmPresenceGraphNode *nd = pm_presence_graph_node(i);
      if (!nd) {
        continue;
      }
      int px = 0;
      int py = 0;
      map_graph_xy(nd->x_m, nd->y_m, rcx, rcy, ppm, &px, &py);
      if (nd->device_id == edge->id_a) {
        ia = static_cast<int>(i);
        ax = px;
        ay = py;
      }
      if (nd->device_id == edge->id_b) {
        ib = static_cast<int>(i);
        bx = px;
        by = py;
      }
    }
    if (ia >= 0 && ib >= 0) {
      pm_gfx->drawLine(ax, ay, bx, by, c_edge_peer);
    }
  }

  const size_t n = pm_presence_graph_node_count();
  for (size_t i = 0; i < n; ++i) {
    const PmPresenceGraphNode *nd = pm_presence_graph_node(i);
    if (!nd || nd->alpha < 0.05f) {
      continue;
    }
    int px = 0;
    int py = 0;
    map_graph_xy(nd->x_m, nd->y_m, rcx, rcy, ppm, &px, &py);
    pm_gfx->drawLine(rcx, rcy, px, py, c_edge_self);

    const bool is_loc = nd->kind == PmPresenceGraphNodeKind::LocationAnchor;
    const int r = static_cast<int>(lrintf(5.f + 3.f * nd->alpha));
    if (is_loc) {
      pm_gfx->fillRect(px - r, py - r, r * 2, r * 2, c_loc);
      pm_gfx->drawRect(px - r - 1, py - r - 1, r * 2 + 2, r * 2 + 2, RGB565_WHITE);
    } else {
      pm_gfx->fillCircle(px, py, r, c_peer);
      pm_gfx->drawCircle(px, py, r + 1, RGB565_WHITE);
    }

    char lab[16];
    if (is_loc && nd->label[0] != '\0') {
      snprintf(lab, sizeof(lab), "%s", nd->label);
    } else {
      snprintf(lab, sizeof(lab), "%.0fm", static_cast<double>(nd->dist_self_m));
    }
    pm_gfx->setTextSize(1, 1);
    pm_gfx->setTextColor(c_label);
    int16_t x1, y1;
    uint16_t tw, th;
    pm_gfx->getTextBounds(lab, 0, 0, &x1, &y1, &tw, &th);
    pm_gfx->setCursor(px - static_cast<int>(tw) / 2, py - r - static_cast<int>(th) - 2);
    pm_gfx->print(lab);
  }

  pm_gfx->fillCircle(rcx, rcy, 6, c_self);
  pm_gfx->drawCircle(rcx, rcy, 7, RGB565_WHITE);

  char footer[48];
  if (pm_presence_ble_failed()) {
    snprintf(footer, sizeof(footer), "BLE off · demo layout");
  } else if (!pm_presence_ble_is_ready()) {
    snprintf(footer, sizeof(footer), "BLE starting…");
  } else {
    snprintf(footer, sizeof(footer), "%u node%s %u edge%s", static_cast<unsigned>(n), n == 1 ? "" : "s",
             static_cast<unsigned>(n_edges), n_edges == 1 ? "" : "s");
  }
  pm_face_draw_centered_line(footer, 318, c_label, 1, 1);
}
