#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    bool configured;
    bool busy;
    bool ok;
    bool is_playing;
    char track[96];
    char artist[72];
    char device[72];
    char error[96];
    uint32_t updated_ms;
} faculty175_spotify_status_t;

esp_err_t faculty175_spotify_configure(const char *client_id, const char *refresh_token);
void faculty175_spotify_status(faculty175_spotify_status_t *out);
void faculty175_spotify_poll(void);
bool faculty175_spotify_toggle(void);
