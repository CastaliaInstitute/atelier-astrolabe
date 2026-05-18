#include "pm_presence_graph.h"

#include <Arduino.h>
#include <cmath>
#include <cstring>

#include "pm_presence.h"
#include "pm_presence_locations.h"

namespace {

constexpr float kPi = 3.14159265f;
constexpr size_t kMaxNodes = kPmPresenceMaxPeers + kPmPresenceMaxLocations;
constexpr size_t kMaxEdges = 20;
constexpr uint32_t kEdgeStaleMs = 20000;
constexpr float kSpringK = 0.85f;
constexpr float kRepulseK = 0.12f;
constexpr float kDamping = 0.72f;
constexpr float kDt = 0.18f;

struct NodeState {
  uint32_t id = 0;
  PmPresenceGraphNodeKind kind = PmPresenceGraphNodeKind::MobilePeer;
  float x = 0.f;
  float y = 0.f;
  float vx = 0.f;
  float vy = 0.f;
  float dist_self = 3.f;
  float alpha = 0.f;
  uint32_t born_ms = 0;
  bool pinned = false;
  float pin_x = 0.f;
  float pin_y = 0.f;
  char label[16] = {};
};

struct EdgeState {
  uint32_t a = 0;
  uint32_t b = 0;
  float rest_m = 0.f;
  uint32_t last_ms = 0;
};

NodeState s_nodes[kMaxNodes];
size_t s_node_count = 0;
EdgeState s_edges[kMaxEdges];
size_t s_edge_count = 0;
float s_px_per_m = 28.f;

uint32_t edge_key_minmax(uint32_t id_a, uint32_t id_b, uint32_t *out_lo, uint32_t *out_hi) {
  if (id_a < id_b) {
    *out_lo = id_a;
    *out_hi = id_b;
  } else {
    *out_lo = id_b;
    *out_hi = id_a;
  }
  return *out_lo;
}

int find_node(uint32_t id) {
  for (size_t i = 0; i < s_node_count; ++i) {
    if (s_nodes[i].id == id) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

int find_edge(uint32_t lo, uint32_t hi) {
  for (size_t i = 0; i < s_edge_count; ++i) {
    if (s_edges[i].a == lo && s_edges[i].b == hi) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

float clamp_dist(float d) {
  if (d < 0.4f) {
    return 0.4f;
  }
  if (d > 40.f) {
    return 40.f;
  }
  return d;
}

void expire_edges(uint32_t now_ms) {
  size_t w = 0;
  for (size_t i = 0; i < s_edge_count; ++i) {
    if (now_ms - s_edges[i].last_ms <= kEdgeStaleMs) {
      if (w != i) {
        s_edges[w] = s_edges[i];
      }
      ++w;
    }
  }
  s_edge_count = w;
}

static void copy_node_state(NodeState &nd, const NodeState *old, uint32_t now_ms, float dist_self, uint32_t id) {
  nd.dist_self = dist_self;
  if (old) {
    nd.x = old->x;
    nd.y = old->y;
    nd.vx = old->vx * 0.5f;
    nd.vy = old->vy * 0.5f;
    nd.born_ms = old->born_ms;
    nd.alpha = old->alpha;
    if (nd.alpha < 1.f) {
      nd.alpha += 0.12f;
    }
  } else {
    const float d = dist_self;
    const float ang = (static_cast<float>((id * 137u) % 360u) - 90.f) * (kPi / 180.f);
    nd.x = d * cosf(ang);
    nd.y = d * sinf(ang);
    nd.vx = 0.f;
    nd.vy = 0.f;
    nd.born_ms = now_ms;
    nd.alpha = 0.25f;
  }
}

void sync_nodes_from_peers(uint32_t now_ms) {
  NodeState next[kMaxNodes] = {};
  size_t next_n = 0;
  const size_t n_peers = pm_presence_peer_count();
  for (size_t i = 0; i < n_peers && next_n < kMaxNodes; ++i) {
    const PmPresencePeer *p = pm_presence_peer(i);
    if (!p || p->node_kind == PmPresenceGraphNodeKind::LocationAnchor) {
      continue;
    }
    const int old = find_node(p->device_id);
    NodeState &nd = next[next_n];
    nd.id = p->device_id;
    nd.kind = PmPresenceGraphNodeKind::MobilePeer;
    nd.pinned = false;
    copy_node_state(nd, old >= 0 ? &s_nodes[old] : nullptr, now_ms, pm_presence_rssi_to_meters(p->rssi_ema), p->device_id);
    ++next_n;
  }

  for (size_t li = 0; li < pm_presence_locations_count() && next_n < kMaxNodes; ++li) {
    const PmPresenceLocationDef *loc = pm_presence_locations_get(li);
    if (!loc) {
      continue;
    }
    const PmPresencePeer *p = nullptr;
    for (size_t pi = 0; pi < n_peers; ++pi) {
      const PmPresencePeer *cand = pm_presence_peer(pi);
      if (cand && cand->device_id == loc->beacon_id) {
        p = cand;
        break;
      }
    }
    if (!p) {
      continue;
    }
    const int old = find_node(loc->beacon_id);
    NodeState &nd = next[next_n];
    nd.id = loc->beacon_id;
    nd.kind = PmPresenceGraphNodeKind::LocationAnchor;
    snprintf(nd.label, sizeof(nd.label), "%s", loc->name);
    nd.dist_self = pm_presence_rssi_to_meters(p->rssi_ema);
    nd.pinned = loc->has_anchor;
    if (loc->has_anchor) {
      nd.pin_x = loc->anchor_x_m;
      nd.pin_y = loc->anchor_y_m;
      if (old >= 0) {
        nd.x = s_nodes[old].x;
        nd.y = s_nodes[old].y;
        nd.alpha = s_nodes[old].alpha;
        if (nd.alpha < 1.f) {
          nd.alpha += 0.12f;
        }
      } else {
        nd.x = loc->anchor_x_m;
        nd.y = loc->anchor_y_m;
        nd.alpha = 0.35f;
      }
      nd.vx = 0.f;
      nd.vy = 0.f;
      nd.born_ms = now_ms;
    } else {
      copy_node_state(nd, old >= 0 ? &s_nodes[old] : nullptr, now_ms, nd.dist_self, loc->beacon_id);
    }
    ++next_n;
  }

  memcpy(s_nodes, next, next_n * sizeof(NodeState));
  s_node_count = next_n;
}

void apply_spring_pair(int ia, int ib, float rest_m, float *fx, float *fy, float *fx2, float *fy2) {
  const float dx = s_nodes[ib].x - s_nodes[ia].x;
  const float dy = s_nodes[ib].y - s_nodes[ia].y;
  float d = sqrtf(dx * dx + dy * dy);
  if (d < 0.05f) {
    d = 0.05f;
  }
  const float f = kSpringK * (d - rest_m) / d;
  const float fx_s = f * dx;
  const float fy_s = f * dy;
  *fx += fx_s;
  *fy += fy_s;
  *fx2 -= fx_s;
  *fy2 -= fy_s;
}

void layout_iterate(void) {
  float fx[kMaxNodes] = {};
  float fy[kMaxNodes] = {};

  for (size_t i = 0; i < s_node_count; ++i) {
    float d = sqrtf(s_nodes[i].x * s_nodes[i].x + s_nodes[i].y * s_nodes[i].y);
    if (d < 0.05f) {
      d = 0.05f;
    }
    const float f = kSpringK * (d - s_nodes[i].dist_self) / d;
    fx[i] -= f * s_nodes[i].x;
    fy[i] -= f * s_nodes[i].y;
  }

  for (size_t e = 0; e < s_edge_count; ++e) {
    const int ia = find_node(s_edges[e].a);
    const int ib = find_node(s_edges[e].b);
    if (ia < 0 || ib < 0) {
      continue;
    }
    apply_spring_pair(ia, ib, s_edges[e].rest_m, &fx[ia], &fy[ia], &fx[ib], &fy[ib]);
  }

  for (size_t i = 0; i < s_node_count; ++i) {
    for (size_t j = i + 1; j < s_node_count; ++j) {
      float dx = s_nodes[j].x - s_nodes[i].x;
      float dy = s_nodes[j].y - s_nodes[i].y;
      float d2 = dx * dx + dy * dy;
      if (d2 < 0.25f) {
        d2 = 0.25f;
      }
      const float rep = kRepulseK / d2;
      fx[i] -= rep * dx;
      fy[i] -= rep * dy;
      fx[j] += rep * dx;
      fy[j] += rep * dy;
    }
  }

  constexpr float kPinK = 2.2f;
  for (size_t i = 0; i < s_node_count; ++i) {
    if (s_nodes[i].pinned) {
      s_nodes[i].x += (s_nodes[i].pin_x - s_nodes[i].x) * kPinK * kDt;
      s_nodes[i].y += (s_nodes[i].pin_y - s_nodes[i].y) * kPinK * kDt;
      s_nodes[i].vx = 0.f;
      s_nodes[i].vy = 0.f;
      continue;
    }
    s_nodes[i].vx = (s_nodes[i].vx + fx[i] * kDt) * kDamping;
    s_nodes[i].vy = (s_nodes[i].vy + fy[i] * kDt) * kDamping;
    s_nodes[i].x += s_nodes[i].vx * kDt;
    s_nodes[i].y += s_nodes[i].vy * kDt;
  }
}

void update_scale(void) {
  float max_r = 2.f;
  for (size_t i = 0; i < s_node_count; ++i) {
    const float r = sqrtf(s_nodes[i].x * s_nodes[i].x + s_nodes[i].y * s_nodes[i].y);
    if (r > max_r) {
      max_r = r;
    }
  }
  s_px_per_m = 105.f / max_r;
  if (s_px_per_m > 48.f) {
    s_px_per_m = 48.f;
  }
  if (s_px_per_m < 12.f) {
    s_px_per_m = 12.f;
  }
}

}  // namespace

float pm_presence_rssi_to_meters(int8_t rssi_dbm) {
  /** Piecewise anchors tuned for ~2–10 m indoor BLE (not absolute truth). */
  static const int8_t k_rssi[] = {-48, -58, -68, -78, -88};
  static const float k_m[] = {1.5f, 2.5f, 4.f, 6.5f, 10.f};
  if (rssi_dbm <= k_rssi[0]) {
    return k_m[0];
  }
  if (rssi_dbm >= k_rssi[4]) {
    return k_m[4];
  }
  for (int i = 0; i < 4; ++i) {
    if (rssi_dbm >= k_rssi[i] && rssi_dbm < k_rssi[i + 1]) {
      const float t = static_cast<float>(rssi_dbm - k_rssi[i]) / static_cast<float>(k_rssi[i + 1] - k_rssi[i]);
      return k_m[i] + t * (k_m[i + 1] - k_m[i]);
    }
  }
  return k_m[4];
}

void pm_presence_graph_set_edge(uint32_t id_a, uint32_t id_b, float dist_m, uint32_t now_ms) {
  if (id_a == 0 || id_b == 0 || id_a == id_b) {
    return;
  }
  uint32_t lo = 0;
  uint32_t hi = 0;
  edge_key_minmax(id_a, id_b, &lo, &hi);
  dist_m = clamp_dist(dist_m);
  int idx = find_edge(lo, hi);
  if (idx < 0) {
    if (s_edge_count >= kMaxEdges) {
      return;
    }
    idx = static_cast<int>(s_edge_count++);
    s_edges[idx].a = lo;
    s_edges[idx].b = hi;
    s_edges[idx].rest_m = dist_m;
  } else {
    s_edges[idx].rest_m = s_edges[idx].rest_m * 0.65f + dist_m * 0.35f;
  }
  s_edges[idx].last_ms = now_ms;
}

void pm_presence_graph_step(uint32_t now_ms, int layout_iterations) {
  expire_edges(now_ms);
  sync_nodes_from_peers(now_ms);
  if (layout_iterations < 1) {
    layout_iterations = 1;
  }
  if (layout_iterations > 12) {
    layout_iterations = 12;
  }
  for (int i = 0; i < layout_iterations; ++i) {
    layout_iterate();
  }
  update_scale();
}

void pm_presence_graph_rotate(float delta_deg) {
  if (fabsf(delta_deg) < 0.02f) {
    return;
  }
  const float rad = delta_deg * (kPi / 180.f);
  const float c = cosf(rad);
  const float s = sinf(rad);
  for (size_t i = 0; i < s_node_count; ++i) {
    if (s_nodes[i].pinned) {
      continue;
    }
    const float x = s_nodes[i].x;
    const float y = s_nodes[i].y;
    s_nodes[i].x = x * c - y * s;
    s_nodes[i].y = x * s + y * c;
    const float vx = s_nodes[i].vx;
    const float vy = s_nodes[i].vy;
    s_nodes[i].vx = vx * c - vy * s;
    s_nodes[i].vy = vx * s + vy * c;
  }
}

void pm_presence_graph_reset(void) {
  s_node_count = 0;
  s_edge_count = 0;
  s_px_per_m = 28.f;
}

size_t pm_presence_graph_node_count(void) { return s_node_count; }

const PmPresenceGraphNode *pm_presence_graph_node(size_t index) {
  static PmPresenceGraphNode out;
  if (index >= s_node_count) {
    return nullptr;
  }
  out.device_id = s_nodes[index].id;
  out.kind = s_nodes[index].kind;
  out.x_m = s_nodes[index].x;
  out.y_m = s_nodes[index].y;
  out.dist_self_m = s_nodes[index].dist_self;
  out.alpha = s_nodes[index].alpha;
  out.pinned = s_nodes[index].pinned;
  snprintf(out.label, sizeof(out.label), "%s", s_nodes[index].label);
  return &out;
}

size_t pm_presence_graph_edge_count(void) { return s_edge_count; }

const PmPresenceGraphEdge *pm_presence_graph_edge(size_t index) {
  static PmPresenceGraphEdge out;
  if (index >= s_edge_count) {
    return nullptr;
  }
  out.id_a = s_edges[index].a;
  out.id_b = s_edges[index].b;
  out.dist_m = s_edges[index].rest_m;
  return &out;
}

float pm_presence_graph_px_per_meter(void) { return s_px_per_m; }
