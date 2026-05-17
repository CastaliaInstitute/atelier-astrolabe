#pragma once

#include <stddef.h>
#include <time.h>

#include "pm_transit.h"

/**
 * Fetch tropical ecliptic longitudes from the optional Castalia `ephemeris` Edge Function.
 * Returns false without mutating `out` when Supabase is not configured, the server is absent,
 * or the response omits any body; callers should fall back to `pm_transit_compute_utc`.
 */
bool pm_ephemeris_fetch(time_t epoch_seconds, PmTransitPositions *out, char *err, size_t err_cap);
