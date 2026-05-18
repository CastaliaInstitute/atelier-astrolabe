#pragma once

#include <cstddef>
#include <cstdint>

#include "pm_presence_locations.h"

/** Convert smoothed RSSI (dBm) to estimated range in meters. */
float pm_presence_rssi_to_meters(int8_t rssi_dbm);

/** Record or refresh an undirected range edge (peer–peer or derived). */
void pm_presence_graph_set_edge(uint32_t id_a, uint32_t id_b, float dist_m, uint32_t now_ms);

/** Sync node list from active peers; run force-layout steps. */
void pm_presence_graph_step(uint32_t now_ms, int layout_iterations);

/** Rotate layout in the body frame (6DOF gyro, degrees CW). */
void pm_presence_graph_rotate(float delta_deg);

void pm_presence_graph_reset(void);

struct PmPresenceGraphNode {
  uint32_t device_id = 0;
  PmPresenceGraphNodeKind kind = PmPresenceGraphNodeKind::MobilePeer;
  float x_m = 0.f;
  float y_m = 0.f;
  float dist_self_m = 0.f;
  /** 0..1 appearance blend when a node is new or stale. */
  float alpha = 0.f;
  bool pinned = false;
  char label[16] = {};
};

size_t pm_presence_graph_node_count(void);
const PmPresenceGraphNode *pm_presence_graph_node(size_t index);

struct PmPresenceGraphEdge {
  uint32_t id_a = 0;
  uint32_t id_b = 0;
  float dist_m = 0.f;
};

size_t pm_presence_graph_edge_count(void);
const PmPresenceGraphEdge *pm_presence_graph_edge(size_t index);

/** Meters → pixels for the current peer bounding box (fits round safe area). */
float pm_presence_graph_px_per_meter(void);
