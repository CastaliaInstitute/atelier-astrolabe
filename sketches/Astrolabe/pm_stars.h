#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

/** Horizon position in degrees (azimuth from north clockwise, altitude above horizon). */
typedef struct {
  char id[12];
  float az;
  float alt;
  float mag;
  bool above_horizon;
} PmStarHorizon;

#define PM_STARS_MAX 64

typedef struct {
  PmStarHorizon star[PM_STARS_MAX];
  int count;
  bool ok;
} PmStarField;

/** Load static catalog from GitHub Pages (or embedded fallback). Safe to call repeatedly. */
bool pm_stars_ensure_catalog(void);

/** Bright stars for observer lat/lon (degrees) at UTC epoch. */
bool pm_stars_compute_horizon(time_t epoch_utc, float lat_deg, float lon_deg, PmStarField *out);

/** Planet ecliptic longitude (tropical) → horizon az/alt for observer. */
bool pm_stars_ecliptic_lon_to_horizon(double ecliptic_lon_deg, time_t epoch_utc, float lat_deg,
                                      float lon_deg, float *az_out, float *alt_out);
