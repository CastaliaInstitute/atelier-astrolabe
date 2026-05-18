#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

#include <mynah_mesh_protocol.h>

#ifndef PM_MESH_MAX_PEERS
#define PM_MESH_MAX_PEERS 8
#endif

struct PmMeshPeer {
  uint8_t mac[6];
  char label[12];
  uint8_t channel;
  uint8_t role;
  uint16_t device_id;
  uint32_t last_seen_ms;
};

bool pm_mesh_begin(void);
void pm_mesh_end(void);
bool pm_mesh_ready(void);

void pm_mesh_discovery_tick(uint32_t now_ms);
int pm_mesh_scan(uint32_t timeout_ms);

int pm_mesh_peer_count(void);
/** Peers with role MYNAH_MESH_ROLE_ATOM (when this device is the watch). */
int pm_mesh_atom_peer_count(void);
bool pm_mesh_peer_get(int index, PmMeshPeer *out);

bool pm_mesh_peer_register(const uint8_t mac[6]);
bool pm_mesh_send_pcm(const uint8_t peer_mac[6], const int16_t *pcm, size_t sample_count);

void pm_mesh_poll(void);

bool pm_mesh_rx_enabled(void);
void pm_mesh_set_rx_enabled(bool on);

bool pm_mesh_tx_begin(int peer_index);
void pm_mesh_tx_end(void);
bool pm_mesh_tx_active(void);
void pm_mesh_tx_tick(void);

uint8_t pm_mesh_channel(void);

bool pm_mesh_serial_command(const char *args);
