#pragma once

#include <cstddef>
#include <cstdint>

struct tm;

void pm_face_spotify_copy_short_line(char *dst, size_t cap, const char *src);
bool pm_face_spotify_hit_transport_bar(int16_t x, int16_t y, int *zone_out);
void pm_face_spotify_draw();
constexpr int pm_face_spotify_bar_y = 238;
constexpr int pm_face_spotify_bar_h = 62;
constexpr int pm_face_spotify_bar_pad = 20;

#include "pm_spotify.h"

extern PmSpotifyStatus g_spotify_ui;

