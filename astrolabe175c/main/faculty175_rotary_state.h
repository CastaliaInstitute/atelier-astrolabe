#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define FACULTY175_ROTARY_STATE_KIND_COUNT 8
#define FACULTY175_ROTARY_STATE_STALE_MS 90000
#define FACULTY175_ROTARY_STATE_MAX_PEERS 8

typedef struct {
    bool valid;
    uint8_t state;
    int8_t rssi_dbm;
    char source[20];
    uint32_t age_ms;
    bool has_style;
    uint8_t facial_hair;
    uint8_t glasses;
    uint8_t skin_tone;
    uint8_t hair_color;
    uint8_t eye_color;
} faculty175_rotary_state_t;

esp_err_t faculty175_rotary_state_init(void);
bool faculty175_rotary_state_get(faculty175_rotary_state_t *out);
const char *faculty175_rotary_state_label(uint8_t state);
const char *faculty175_rotary_state_emoji(uint8_t state);
