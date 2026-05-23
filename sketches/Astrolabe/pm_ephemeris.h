#pragma once

#include "pm_transit.h"

struct tm;

/** Fetch tropical ecliptic longitudes from cached/server Castalia ephemeris. Returns false if unavailable. */
bool pm_ephemeris_fetch_utc(const struct tm *utc, PmTransitPositions *out);

/** Last fetch used network (vs cache). Cleared after read. */
bool pm_ephemeris_last_from_network(void);

/** Release cached monthly ephemeris JSON after faces that need it leave. */
void pm_ephemeris_release_cache(void);
