#pragma once

#include "pm_transit.h"

struct tm;

typedef enum {
  kPmHdBodySun = 0,
  kPmHdBodyMoon,
  kPmHdBodyMercury,
  kPmHdBodyVenus,
  kPmHdBodyMars,
  kPmHdBodyJupiter,
  kPmHdBodySaturn,
  kPmHdBodyUranus,
  kPmHdBodyNeptune,
  kPmHdBodyPluto,
  kPmHdBodyTrueNode,
  kPmHdBodyMeanNode,
  kPmHdBodyCount,
} PmHumanDesignBody;

typedef struct {
  double lon[kPmHdBodyCount];
  bool ok;
} PmHumanDesignPositions;

/** Fetch tropical ecliptic longitudes from cached/server Castalia ephemeris. Returns false if unavailable. */
bool pm_ephemeris_fetch_utc(const struct tm *utc, PmTransitPositions *out);

/** Fetch daily Human Design ephemeris bodies from cached/server data for one UTC epoch. */
bool pm_ephemeris_fetch_human_design_epoch(time_t utc_epoch, PmHumanDesignPositions *out);

/** Download/cache the ephemeris month containing `utc_epoch` for later offline lookup. */
bool pm_ephemeris_prefetch_epoch(time_t utc_epoch);

/** Download/cache every ephemeris month touched by `[utc_start, utc_end]`. */
bool pm_ephemeris_prefetch_range(time_t utc_start, time_t utc_end);

/** Last fetch used network (vs cache). Cleared after read. */
bool pm_ephemeris_last_from_network(void);

/** Release cached monthly ephemeris JSON after faces that need it leave. */
void pm_ephemeris_release_cache(void);
