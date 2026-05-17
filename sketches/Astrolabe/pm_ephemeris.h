#pragma once

#include "pm_transit.h"

struct tm;

/** Fetch tropical ecliptic longitudes from Castalia ephemeris HTTPS API. Returns false if offline or error. */
bool pm_ephemeris_fetch_utc(const struct tm *utc, PmTransitPositions *out);

/** Last fetch used network (vs cache). Cleared after read. */
bool pm_ephemeris_last_from_network(void);
