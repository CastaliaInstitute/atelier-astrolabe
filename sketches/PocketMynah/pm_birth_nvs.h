#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

/** Civil birth date/time at `place` using `tz_offset_sec` (same sign as pm_geo_tz_offset_sec). */
typedef struct {
  uint16_t year;
  uint8_t month;
  uint8_t day;
  uint8_t hour;
  uint8_t minute;
  float lat_deg;
  float lon_deg;
  int32_t tz_offset_sec;
  char place[40];
  bool valid;
} PmBirthSpec;

bool pm_birth_load(PmBirthSpec *out);
void pm_birth_save(const PmBirthSpec *in);
void pm_birth_clear(void);

/** UTC epoch for ephemeris from civil birth fields + stored TZ offset. */
bool pm_birth_to_utc_epoch(const PmBirthSpec *birth, time_t *utc_out);

/** If NVS is empty, store demo chart: 1972-05-06 11:30 Tallahassee, FL. */
void pm_birth_ensure_demo(void);
