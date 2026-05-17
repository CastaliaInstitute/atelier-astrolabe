#pragma once

#include <stddef.h>
#include <stdint.h>

#include "pm_birth_nvs.h"

struct tm;

typedef enum {
  kPmBodySun = 0,
  kPmBodyMoon,
  kPmBodyMercury,
  kPmBodyVenus,
  kPmBodyMars,
  kPmBodyJupiter,
  kPmBodySaturn,
  kPmBodyCount,
} PmEphemBody;

/** Tropical ecliptic longitudes in degrees [0,360). */
typedef struct {
  double lon[kPmBodyCount];
  bool ok;
} PmTransitPositions;

/** `utc` must be filled in calendar fields (tm_year mon mday hour min sec). Ignores tm_isdst. */
void pm_transit_compute_utc(const struct tm *utc, PmTransitPositions *out);

/** Natal Sun longitude only (sufficient for sign + basic transits). Birth interpreted as local civil time. */
bool pm_transit_natal_sun_lon(const PmBirthSpec *birth, double *lon_deg_out);

static inline const char *pm_ephem_body_label(PmEphemBody b) {
  static const char *const k[] = {"Su", "Mo", "Me", "Ve", "Ma", "Ju", "Sa"};
  if (static_cast<unsigned>(b) >= kPmBodyCount) {
    return "?";
  }
  return k[static_cast<unsigned>(b)];
}
