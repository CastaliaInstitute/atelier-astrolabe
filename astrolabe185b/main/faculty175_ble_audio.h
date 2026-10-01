/*
 * BLE Audio Bridge header (device peripheral) — contract: docs/design/ble-audio-bridge.md
 * Shipped byte-identically in astrolabe175c/main and astrolabe185b/main.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "host/ble_gap.h"
#include "host/ble_gatt.h"

struct ble_gatt_svc_def;

typedef struct {
    /* 0x01 control msgs: epoch seconds; wired from faculty175_ble.c via astrolabe_time */
    void (*time_set)(uint32_t epoch_s);
    /* capture-only spool route (V1: may be NULL — hook stays a labeled no-op) */
    void (*spool_pcm)(const int16_t *samples, size_t sample_count);
} faculty175_ble_audio_hooks_t;

/* Audio service pieces consumed by faculty175_ble.c's k_ble_svcs table */
extern const ble_uuid128_t faculty175_ble_audio_svc_uuid;
extern const struct ble_gatt_chr_def faculty175_ble_audio_chr_defs[];

void faculty175_ble_audio_hooks(const faculty175_ble_audio_hooks_t *hooks);
bool faculty175_ble_audio_start(void);
void faculty175_ble_audio_set_ring(bool on);
void faculty175_ble_audio_gap(const struct ble_gap_event *event);
void faculty175_ble_audio_qa(void);
