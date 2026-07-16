#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define FACULTY175_BLE_PEER_MAX 10
#define FACULTY175_BLE_PEER_NAME_MAX 32
#define FACULTY175_BLE_OBS_MAX 2
#define FACULTY175_BLE_RING_TELEM_MAX 32

typedef struct {
    bool valid;
    bool astrolabe;
    bool ring;
    uint16_t addr_hash;
    int8_t rssi;
} faculty175_ble_observation_t;

typedef struct {
    bool valid;
    bool astrolabe;
    bool ring;
    bool known;
    uint8_t addr[6];
    uint16_t addr_hash;
    int8_t rssi;
    int8_t tx_power;
    uint32_t seen_ms;
    uint16_t bearing_deg;
    uint8_t range_pct;
    uint8_t confidence_pct;
    bool imu_valid;
    int8_t imu_pitch_deg;
    int8_t imu_roll_deg;
    int8_t accel_x_q6;
    int8_t accel_y_q6;
    int8_t accel_z_q6;
    uint8_t imu_seq;
    uint8_t observation_count;
    faculty175_ble_observation_t observations[FACULTY175_BLE_OBS_MAX];
    char name[FACULTY175_BLE_PEER_NAME_MAX];
} faculty175_ble_peer_t;

typedef struct {
    bool valid;
    uint32_t age_ms;
    uint16_t ring_id;
    int8_t rssi;
    int8_t rssi_delta;
    bool local_imu_valid;
    int8_t pitch_deg;
    int8_t roll_deg;
    int8_t accel_x_q6;
    int8_t accel_y_q6;
    int8_t accel_z_q6;
    uint8_t motion_score;
    char gesture[16];
    char name[FACULTY175_BLE_PEER_NAME_MAX];
} faculty175_ble_ring_telem_t;

esp_err_t faculty175_ble_init(void);
bool faculty175_ble_enabled(void);
bool faculty175_ble_advertising(void);
bool faculty175_ble_scanning(void);
esp_err_t faculty175_ble_set_enabled(bool enabled);
esp_err_t faculty175_ble_set_device_name(const char *name);
const char *faculty175_ble_device_name(void);
esp_err_t faculty175_ble_scan_start(uint32_t duration_ms);
void faculty175_ble_serial_activity(void);
void faculty175_ble_radar_tick(uint32_t now_ms);
size_t faculty175_ble_peers_snapshot(faculty175_ble_peer_t *out, size_t cap);
size_t faculty175_ble_ring_telemetry_snapshot(faculty175_ble_ring_telem_t *out, size_t cap);
bool faculty175_ble_handle(const char *line);
