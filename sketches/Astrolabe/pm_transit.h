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

enum {
  kPmTransitAspectMax = kPmBodyCount * 3,
  kPmTransitHouseEventMax = kPmBodyCount,
  kPmTransitAspectTitleLen = 48,
  kPmTransitDurationLabelLen = 24,
};

/** Tropical ecliptic longitudes in degrees [0,360). */
typedef struct {
  double lon[kPmBodyCount];
  bool ok;
} PmTransitPositions;

/** Natal points used by compact transit readings. */
typedef enum {
  kPmNatalTargetSun = 0,
  kPmNatalTargetMoon,
  kPmNatalTargetAsc,
  kPmNatalTargetCount,
} PmNatalTarget;

/** Major Ptolemaic aspects expressed by exact separation angle in degrees. */
typedef enum {
  kPmTransitAspectConjunction = 0,
  kPmTransitAspectSextile = 60,
  kPmTransitAspectSquare = 90,
  kPmTransitAspectTrine = 120,
  kPmTransitAspectOpposition = 180,
} PmTransitAspectKind;

/** Aspect orb configuration. Pass an array of these to override the default major-aspect orbs. */
typedef struct {
  PmTransitAspectKind aspect;
  double orb_deg;
} PmTransitAspectOrb;

/**
 * On-device natal chart from `PmBirthSpec`: tropical longitudes for supported bodies, approximate
 * Ascendant, and whole-sign natal house (1..12) for each body. Ascendant is computed from stored
 * birth lat/lon and UTC offset using low-precision sidereal-time math; positions use cached monthly
 * ephemeris when available and otherwise the local formulas, so this is suitable for watch readings
 * but not a Swiss Ephemeris replacement.
 */
typedef struct {
  PmTransitPositions bodies;
  double asc_lon;
  uint8_t asc_sign;
  uint8_t whole_sign_house[kPmBodyCount];
  bool ok;
} PmNatalChart;

/**
 * One live transit aspect to natal Sun, Moon, or Ascendant. `orb_delta_deg` is signed from exact.
 * `title` and `duration_label` are compact Castalian names for Pattern-like summaries; duration is
 * approximate, based on configured orb and average transiting-body motion rather than exact ingress.
 */
typedef struct {
  PmEphemBody transit_body;
  PmNatalTarget natal_target;
  PmTransitAspectKind aspect;
  double exact_delta_deg;
  double orb_delta_deg;
  double orb_limit_deg;
  double active_days;
  char title[kPmTransitAspectTitleLen];
  char duration_label[kPmTransitDurationLabelLen];
} PmTransitAspect;

/** Live body occupying a whole-sign natal house. */
typedef struct {
  PmEphemBody transit_body;
  uint8_t natal_house;
  double lon_deg;
} PmTransitHouseEvent;

/** Transit positions plus derived natal aspects and transit-through-natal-house events. */
typedef struct {
  PmTransitPositions transit;
  PmNatalChart natal;
  PmTransitAspect aspects[kPmTransitAspectMax];
  size_t aspect_count;
  PmTransitHouseEvent house_events[kPmTransitHouseEventMax];
  size_t house_event_count;
  bool ok;
} PmTransitSnapshot;

/** `utc` must be filled in calendar fields (tm_year mon mday hour min sec). Ignores tm_isdst. */
void pm_transit_compute_utc(const struct tm *utc, PmTransitPositions *out);

/** Local low-precision planetary formulas only; use as the guaranteed offline fallback. */
void pm_transit_compute_utc_local(const struct tm *utc, PmTransitPositions *out);

/** Build a natal chart from NVS-compatible birth fields plus lat/lon. */
bool pm_transit_build_natal_chart(const PmBirthSpec *birth, PmNatalChart *out);

/** Return whole-sign house number (1..12) for `lon_deg`, using the natal Ascendant sign as house 1. */
uint8_t pm_transit_whole_sign_house(double lon_deg, double asc_lon_deg);

/** Default major-aspect orb table; callers may pass their own table to snapshot helpers. */
const PmTransitAspectOrb *pm_transit_default_orbs(size_t *count_out);

/** Derive aspects and house events from already-computed current positions (remote or local). */
bool pm_transit_snapshot_from_positions(const PmNatalChart *natal, const PmTransitPositions *transit,
                                        const PmTransitAspectOrb *orbs, size_t orb_count,
                                        PmTransitSnapshot *out);

/** Compute current positions for `utc`, then derive natal aspects and house events. */
bool pm_transit_snapshot_utc(const PmBirthSpec *birth, const struct tm *utc,
                             const PmTransitAspectOrb *orbs, size_t orb_count,
                             PmTransitSnapshot *out);

/** Full natal/body positions for a stored civil birth record. Uses ephemeris cache/server when online. */
bool pm_transit_birth_positions(const PmBirthSpec *birth, PmTransitPositions *out);

/** Natal Sun longitude only (legacy helper). Birth interpreted as local civil time. */
bool pm_transit_natal_sun_lon(const PmBirthSpec *birth, double *lon_deg_out);

const char *pm_transit_aspect_label(PmTransitAspectKind aspect);
const char *pm_transit_natal_target_label(PmNatalTarget target);

static inline const char *pm_ephem_body_label(PmEphemBody b) {
  static const char *const k[] = {"Su", "Mo", "Me", "Ve", "Ma", "Ju", "Sa"};
  if (static_cast<unsigned>(b) >= kPmBodyCount) {
    return "?";
  }
  return k[static_cast<unsigned>(b)];
}
