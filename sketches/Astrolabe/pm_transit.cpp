#include "pm_transit.h"

#include "pm_ephemeris.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>

static double rev360(double x) {
  x = fmod(x, 360.0);
  if (x < 0.0) {
    x += 360.0;
  }
  return x;
}

static double julian_day_ut(const struct tm *u) {
  int Y = u->tm_year + 1900;
  int M = u->tm_mon + 1;
  const int D = u->tm_mday;
  const double h = static_cast<double>(u->tm_hour) + static_cast<double>(u->tm_min) / 60.0 +
                   static_cast<double>(u->tm_sec) / 3600.0;
  if (M <= 2) {
    Y -= 1;
    M += 12;
  }
  const int A = Y / 100;
  const int B = 2 - A + (A / 4);
  const double jd =
      floor(365.25 * (Y + 4716)) + floor(30.6001 * (M + 1)) + static_cast<double>(D) +
      static_cast<double>(B) - 1524.5 + h / 24.0;
  return jd;
}

static double deg_to_rad(double deg) { return deg * (M_PI / 180.0); }

static int zodiac_sign_index(double lon_deg) {
  return static_cast<int>(rev360(lon_deg) / 30.0) % 12;
}

static double angle_delta_deg(double a, double b) {
  double d = fabs(rev360(a) - rev360(b));
  if (d > 180.0) {
    d = 360.0 - d;
  }
  return d;
}

static double mean_obliquity_deg(double jd) {
  const double T = (jd - 2451545.0) / 36525.0;
  return 23.439291111 - 0.013004167 * T - 0.000000164 * T * T + 0.000000504 * T * T * T;
}

static double gmst_deg(double jd) {
  const double T = (jd - 2451545.0) / 36525.0;
  return rev360(280.46061837 + 360.98564736629 * (jd - 2451545.0) + 0.000387933 * T * T -
                (T * T * T) / 38710000.0);
}

static bool ascendant_lon_deg(double jd, double lat_deg, double lon_deg, double *out) {
  if (!out || !std::isfinite(lat_deg) || !std::isfinite(lon_deg) || lat_deg < -89.5 || lat_deg > 89.5) {
    return false;
  }
  const double lst = deg_to_rad(rev360(gmst_deg(jd) + lon_deg));
  const double lat = deg_to_rad(lat_deg);
  const double eps = deg_to_rad(mean_obliquity_deg(jd));
  const double y = -cos(lst);
  const double x = sin(lst) * cos(eps) + tan(lat) * sin(eps);
  *out = rev360(atan2(y, x) * (180.0 / M_PI));
  return true;
}

static void sun_rect_and_mean(double d, double *xs, double *ys, double *zs, double *lon_deg,
                              double *Ls_deg, double *Ms_deg) {
  const double w = 282.9404 + 4.70935e-5 * d;
  const double e = 0.016709 - 1.151e-9 * d;
  double M = 356.0470 + 0.9856002585 * d;
  M = rev360(M);
  const double Ls = rev360(w + M);
  const double Mr = M * (M_PI / 180.0);
  double E = M + (180.0 / M_PI) * e * sin(Mr) * (1.0 + e * cos(Mr));
  E = rev360(E);
  const double Er = E * (M_PI / 180.0);
  const double xv = cos(Er) - e;
  const double yv = sin(Er) * sqrt(1.0 - e * e);
  const double v = atan2(yv, xv) * (180.0 / M_PI);
  const double lon = rev360(v + w);
  const double r = 1.000000; /* AU */
  *xs = r * cos(lon * (M_PI / 180.0));
  *ys = r * sin(lon * (M_PI / 180.0));
  *zs = 0.0;
  *lon_deg = lon;
  if (Ls_deg) {
    *Ls_deg = Ls;
  }
  if (Ms_deg) {
    *Ms_deg = M;
  }
}

static void moon_lon_rect(double d, double Ls_deg, double Ms_deg, double *lon_deg, double *xm,
                          double *ym, double *zm) {
  /* Orbital elements (stjarnhimlen.se/comp/tutorial.html §7), d = days since J2000 epoch JD-2451543.5 */
  double N = 125.1228 - 0.0529538083 * d;
  const double i = 5.1454;
  double w_m = 318.0634 + 0.1643573223 * d;
  const double a = 60.2666; /* Earth radii */
  const double e = 0.054900;
  double M = 115.3654 + 13.0649929509 * d;
  N = rev360(N);
  w_m = rev360(w_m);
  M = rev360(M);

  double Mr = M * (M_PI / 180.0);
  double E = M + (180.0 / M_PI) * e * sin(Mr) * (1.0 + e * cos(Mr));
  for (int iter = 0; iter < 6; ++iter) {
    E = rev360(E);
    const double Er = E * (M_PI / 180.0);
    const double dE =
        (E - (180.0 / M_PI) * e * sin(Er) - M) / (1.0 - e * cos(Er));
    E -= dE;
    if (fabs(dE) < 1e-6) {
      break;
    }
  }
  E = rev360(E);
  const double Er = E * (M_PI / 180.0);
  const double xv = a * (cos(Er) - e);
  const double yv = a * sqrt(1.0 - e * e) * sin(Er);
  const double v = atan2(yv, xv) * (180.0 / M_PI);
  const double r = sqrt(xv * xv + yv * yv);

  const double Nr = N * (M_PI / 180.0);
  const double ir = i * (M_PI / 180.0);
  const double vw = (v + w_m) * (M_PI / 180.0);

  const double xe = r * (cos(Nr) * cos(vw) - sin(Nr) * sin(vw) * cos(ir));
  const double ye = r * (sin(Nr) * cos(vw) + cos(Nr) * sin(vw) * cos(ir));
  const double ze = r * sin(vw) * sin(ir);

  double lon = atan2(ye, xe) * (180.0 / M_PI);
  lon = rev360(lon);

  /* §8: main longitude perturbations (Sun mean Ls, Ms; Moon mean Mm; D, F) */
  const double Lm = rev360(N + w_m + M);
  const double Ls = rev360(Ls_deg);
  const double Ms = rev360(Ms_deg);
  const double Mm = M;
  const double D = rev360(Lm - Ls);
  const double F = rev360(Lm - N);
  const double Dr = D * (M_PI / 180.0);
  const double Mmr = Mm * (M_PI / 180.0);
  const double Msr = Ms * (M_PI / 180.0);
  const double Fr = F * (M_PI / 180.0);
  const double corr =
      -1.274 * sin(Mmr - 2.0 * Dr) + 0.658 * sin(2.0 * Dr) - 0.186 * sin(Msr);
  lon = rev360(lon + corr);

  *lon_deg = lon;
  /* Approximate geocentric rect in ecliptic frame (Earth radii): Moon distance ~60 Er */
  const double dist_au = r * (6378.137 / 149597870.0); /* very rough scale for composition */
  (void)dist_au;
  *xm = xe / 60.2666; /* normalize ~ order 1 for atan2 */
  *ym = ye / 60.2666;
  *zm = ze / 60.2666;
}

static void planet_helio_geo(double d, double N0, double i0, double w0, double a, double e0,
                             double M0, double dN, double di, double dw, double de, double dM,
                             double xs, double ys, double zs, double *lon_deg) {
  double N = N0 + dN * d;
  double i = i0 + di * d;
  double w = w0 + dw * d;
  double e = e0 + de * d;
  double M = M0 + dM * d;
  N = rev360(N);
  i = rev360(i);
  w = rev360(w);
  M = rev360(M);

  double Mr = M * (M_PI / 180.0);
  double E = M + (180.0 / M_PI) * e * sin(Mr) * (1.0 + e * cos(Mr));
  for (int iter = 0; iter < 10; ++iter) {
    E = rev360(E);
    const double Er = E * (M_PI / 180.0);
    const double dE =
        (E - (180.0 / M_PI) * e * sin(Er) - M) / (1.0 - e * cos(Er));
    E -= dE;
    if (fabs(dE) < 1e-7) {
      break;
    }
  }
  E = rev360(E);
  const double Er = E * (M_PI / 180.0);
  const double xv = a * (cos(Er) - e);
  const double yv = a * sqrt(1.0 - e * e) * sin(Er);
  const double v = atan2(yv, xv) * (180.0 / M_PI);
  const double r = sqrt(xv * xv + yv * yv);

  const double Nr = N * (M_PI / 180.0);
  const double ir = i * (M_PI / 180.0);
  const double vw = (v + w) * (M_PI / 180.0);

  const double xh = r * (cos(Nr) * cos(vw) - sin(Nr) * sin(vw) * cos(ir));
  const double yh = r * (sin(Nr) * cos(vw) + cos(Nr) * sin(vw) * cos(ir));
  const double zh = r * sin(vw) * sin(ir);

  const double xg = xh + xs;
  const double yg = yh + ys;
  const double zg = zh + zs;
  *lon_deg = rev360(atan2(yg, xg) * (180.0 / M_PI));
}

void pm_transit_compute_utc_local(const struct tm *utc, PmTransitPositions *out) {
  if (!utc || !out) {
    return;
  }
  out->ok = false;
  const double jd = julian_day_ut(utc);
  const double d = jd - 2451543.5;

  double xs = 0, ys = 0, zs = 0, Ls = 0, Ms = 0;
  sun_rect_and_mean(d, &xs, &ys, &zs, &out->lon[kPmBodySun], &Ls, &Ms);

  double xm = 0, ym = 0, zm = 0;
  moon_lon_rect(d, Ls, Ms, &out->lon[kPmBodyMoon], &xm, &ym, &zm);

  planet_helio_geo(d, 48.3313, 7.0047, 29.1241, 0.387098, 0.205635, 168.6562, 3.24587e-5, 5.00e-8,
                   1.01444e-5, 5.59e-10, 4.0923344368, xs, ys, zs, &out->lon[kPmBodyMercury]);
  planet_helio_geo(d, 76.6799, 3.3946, 54.8910, 0.723330, 0.006773, 48.0052, 2.46590e-5, 2.75e-8,
                   1.38374e-5, -1.302e-9, 1.6021302244, xs, ys, zs, &out->lon[kPmBodyVenus]);
  planet_helio_geo(d, 49.5574, 1.8497, 286.5016, 1.523688, 0.093405, 18.6021, 2.11081e-5, -1.78e-8,
                   2.92961e-5, 2.516e-9, 0.5240207766, xs, ys, zs, &out->lon[kPmBodyMars]);
  planet_helio_geo(d, 100.4542, 1.3030, 273.8777, 5.20256, 0.048498, 19.8950, 2.76854e-5, -1.557e-7,
                   1.64505e-5, 4.469e-9, 0.0830853001, xs, ys, zs, &out->lon[kPmBodyJupiter]);
  planet_helio_geo(d, 113.6634, 2.4886, 339.3939, 9.55475, 0.055546, 316.9670, 2.38980e-5, -1.081e-7,
                   2.97661e-5, -9.499e-9, 0.0334442282, xs, ys, zs, &out->lon[kPmBodySaturn]);

  out->ok = true;
}

void pm_transit_compute_utc(const struct tm *utc, PmTransitPositions *out) {
  if (!utc || !out) {
    return;
  }
  out->ok = false;
  if (pm_ephemeris_fetch_utc(utc, out)) {
    return;
  }
  pm_transit_compute_utc_local(utc, out);
}

uint8_t pm_transit_whole_sign_house(double lon_deg, double asc_lon_deg) {
  if (!std::isfinite(lon_deg) || !std::isfinite(asc_lon_deg)) {
    return 0;
  }
  const int asc_sign = zodiac_sign_index(asc_lon_deg);
  const int body_sign = zodiac_sign_index(lon_deg);
  return static_cast<uint8_t>(((body_sign - asc_sign + 12) % 12) + 1);
}

bool pm_transit_build_natal_chart(const PmBirthSpec *birth, PmNatalChart *out) {
  if (!out) {
    return false;
  }
  memset(out, 0, sizeof(*out));
  if (!birth || !birth->valid || !std::isfinite(birth->lat_deg) || !std::isfinite(birth->lon_deg)) {
    return false;
  }
  time_t epoch = 0;
  if (!pm_birth_to_utc_epoch(birth, &epoch)) {
    return false;
  }
  struct tm utc = {};
  gmtime_r(&epoch, &utc);
  pm_transit_compute_utc(&utc, &out->bodies);
  if (!out->bodies.ok) {
    return false;
  }
  const double jd = julian_day_ut(&utc);
  if (!ascendant_lon_deg(jd, static_cast<double>(birth->lat_deg), static_cast<double>(birth->lon_deg),
                         &out->asc_lon)) {
    return false;
  }
  out->asc_sign = static_cast<uint8_t>(zodiac_sign_index(out->asc_lon));
  for (int i = 0; i < kPmBodyCount; ++i) {
    out->whole_sign_house[i] = pm_transit_whole_sign_house(out->bodies.lon[i], out->asc_lon);
  }
  out->ok = true;
  return true;
}

const PmTransitAspectOrb *pm_transit_default_orbs(size_t *count_out) {
  static const PmTransitAspectOrb k_default_orbs[] = {
      {kPmTransitAspectConjunction, 8.0}, {kPmTransitAspectSextile, 4.0},
      {kPmTransitAspectSquare, 6.0},      {kPmTransitAspectTrine, 6.0},
      {kPmTransitAspectOpposition, 6.0},
  };
  if (count_out) {
    *count_out = sizeof(k_default_orbs) / sizeof(k_default_orbs[0]);
  }
  return k_default_orbs;
}

static double natal_target_lon(const PmNatalChart *natal, PmNatalTarget target) {
  switch (target) {
    case kPmNatalTargetSun:
      return natal->bodies.lon[kPmBodySun];
    case kPmNatalTargetMoon:
      return natal->bodies.lon[kPmBodyMoon];
    case kPmNatalTargetAsc:
      return natal->asc_lon;
    default:
      return 0.0;
  }
}

static const char *aspect_literary_label(PmTransitAspectKind aspect) {
  switch (aspect) {
    case kPmTransitAspectConjunction:
      return "Confluence";
    case kPmTransitAspectSextile:
      return "Invitation";
    case kPmTransitAspectSquare:
      return "Crossing";
    case kPmTransitAspectTrine:
      return "Current";
    case kPmTransitAspectOpposition:
      return "Mirror";
    default:
      return "Signal";
  }
}

static const char *body_literary_label(PmEphemBody body) {
  switch (body) {
    case kPmBodySun:
      return "Sunfire";
    case kPmBodyMoon:
      return "Moon-tide";
    case kPmBodyMercury:
      return "Mercury Lantern";
    case kPmBodyVenus:
      return "Venus Rose";
    case kPmBodyMars:
      return "Mars Ember";
    case kPmBodyJupiter:
      return "Jupiter Oracle";
    case kPmBodySaturn:
      return "Saturn Gate";
    default:
      return "Wanderer";
  }
}

static const char *target_literary_label(PmNatalTarget target) {
  switch (target) {
    case kPmNatalTargetSun:
      return "Solar Self";
    case kPmNatalTargetMoon:
      return "Inner Moon";
    case kPmNatalTargetAsc:
      return "Horizon";
    default:
      return "Chart";
  }
}

static double body_avg_motion_deg_per_day(PmEphemBody body) {
  switch (body) {
    case kPmBodyMoon:
      return 13.176;
    case kPmBodySun:
      return 0.986;
    case kPmBodyMercury:
      return 1.20;
    case kPmBodyVenus:
      return 1.00;
    case kPmBodyMars:
      return 0.524;
    case kPmBodyJupiter:
      return 0.083;
    case kPmBodySaturn:
      return 0.033;
    default:
      return 1.0;
  }
}

static void format_duration_label(double days, char *out, size_t cap) {
  if (!out || cap == 0) {
    return;
  }
  if (!std::isfinite(days) || days <= 0.0) {
    snprintf(out, cap, "briefly");
  } else if (days < 1.5) {
    int hours = static_cast<int>(lrint(days * 24.0));
    if (hours < 1) {
      hours = 1;
    }
    snprintf(out, cap, "about %d hour%s", hours, hours == 1 ? "" : "s");
  } else if (days < 14.0) {
    const int whole_days = static_cast<int>(lrint(days));
    snprintf(out, cap, "about %d days", whole_days < 1 ? 1 : whole_days);
  } else if (days < 70.0) {
    const int weeks = static_cast<int>(lrint(days / 7.0));
    snprintf(out, cap, "about %d weeks", weeks < 1 ? 1 : weeks);
  } else {
    const int months = static_cast<int>(lrint(days / 30.0));
    snprintf(out, cap, "about %d months", months < 1 ? 1 : months);
  }
}

static void describe_transit_aspect(PmTransitAspect *asp) {
  if (!asp) {
    return;
  }
  snprintf(asp->title, sizeof(asp->title), "%s %s: %s", body_literary_label(asp->transit_body),
           aspect_literary_label(asp->aspect), target_literary_label(asp->natal_target));
  const double speed = body_avg_motion_deg_per_day(asp->transit_body);
  asp->active_days = speed > 0.0 ? (2.0 * asp->orb_limit_deg) / speed : 0.0;
  format_duration_label(asp->active_days, asp->duration_label, sizeof(asp->duration_label));
}

bool pm_transit_snapshot_from_positions(const PmNatalChart *natal, const PmTransitPositions *transit,
                                        const PmTransitAspectOrb *orbs, size_t orb_count,
                                        PmTransitSnapshot *out) {
  if (!out) {
    return false;
  }
  memset(out, 0, sizeof(*out));
  if (!natal || !natal->ok || !transit || !transit->ok) {
    return false;
  }
  if (!orbs || orb_count == 0) {
    orbs = pm_transit_default_orbs(&orb_count);
  }
  out->natal = *natal;
  out->transit = *transit;

  for (int body = 0; body < kPmBodyCount; ++body) {
    if (out->house_event_count < kPmTransitHouseEventMax) {
      PmTransitHouseEvent *ev = &out->house_events[out->house_event_count++];
      ev->transit_body = static_cast<PmEphemBody>(body);
      ev->natal_house = pm_transit_whole_sign_house(transit->lon[body], natal->asc_lon);
      ev->lon_deg = transit->lon[body];
    }

    for (int target = 0; target < kPmNatalTargetCount; ++target) {
      const double delta = angle_delta_deg(transit->lon[body],
                                           natal_target_lon(natal, static_cast<PmNatalTarget>(target)));
      bool have_match = false;
      PmTransitAspectKind best_aspect = kPmTransitAspectConjunction;
      double best_orb_delta = 999.0;
      double best_orb_limit = 0.0;
      for (size_t oi = 0; oi < orb_count; ++oi) {
        if (orbs[oi].orb_deg < 0.0 || !std::isfinite(orbs[oi].orb_deg)) {
          continue;
        }
        const double exact = static_cast<double>(static_cast<int>(orbs[oi].aspect));
        const double orb_delta = delta - exact;
        if (fabs(orb_delta) <= orbs[oi].orb_deg && fabs(orb_delta) < fabs(best_orb_delta)) {
          have_match = true;
          best_aspect = orbs[oi].aspect;
          best_orb_delta = orb_delta;
          best_orb_limit = orbs[oi].orb_deg;
        }
      }
      if (have_match && out->aspect_count < kPmTransitAspectMax) {
        PmTransitAspect *asp = &out->aspects[out->aspect_count++];
        asp->transit_body = static_cast<PmEphemBody>(body);
        asp->natal_target = static_cast<PmNatalTarget>(target);
        asp->aspect = best_aspect;
        asp->exact_delta_deg = delta;
        asp->orb_delta_deg = best_orb_delta;
        asp->orb_limit_deg = best_orb_limit;
        describe_transit_aspect(asp);
      }
    }
  }
  out->ok = true;
  return true;
}

bool pm_transit_snapshot_utc(const PmBirthSpec *birth, const struct tm *utc,
                             const PmTransitAspectOrb *orbs, size_t orb_count,
                             PmTransitSnapshot *out) {
  if (!out) {
    return false;
  }
  memset(out, 0, sizeof(*out));
  if (!birth || !utc) {
    return false;
  }
  PmNatalChart natal = {};
  if (!pm_transit_build_natal_chart(birth, &natal)) {
    return false;
  }
  PmTransitPositions transit = {};
  pm_transit_compute_utc(utc, &transit);
  return pm_transit_snapshot_from_positions(&natal, &transit, orbs, orb_count, out);
}

bool pm_transit_natal_sun_lon(const PmBirthSpec *birth, double *lon_deg_out) {
  if (!birth || !birth->valid || !lon_deg_out) {
    return false;
  }
  PmTransitPositions tp = {};
  if (!pm_transit_birth_positions(birth, &tp)) {
    return false;
  }
  *lon_deg_out = tp.lon[kPmBodySun];
  return true;
}

bool pm_transit_birth_positions(const PmBirthSpec *birth, PmTransitPositions *out) {
  if (!birth || !birth->valid || !out) {
    return false;
  }
  out->ok = false;
  time_t epoch = 0;
  if (!pm_birth_to_utc_epoch(birth, &epoch)) {
    return false;
  }
  struct tm utc = {};
  gmtime_r(&epoch, &utc);
  pm_transit_compute_utc(&utc, out);
  if (!out->ok) {
    return false;
  }
  return true;
}

const char *pm_transit_aspect_label(PmTransitAspectKind aspect) {
  switch (aspect) {
    case kPmTransitAspectConjunction:
      return "conj";
    case kPmTransitAspectSextile:
      return "sextile";
    case kPmTransitAspectSquare:
      return "square";
    case kPmTransitAspectTrine:
      return "trine";
    case kPmTransitAspectOpposition:
      return "opp";
    default:
      return "?";
  }
}

const char *pm_transit_natal_target_label(PmNatalTarget target) {
  switch (target) {
    case kPmNatalTargetSun:
      return "natal Sun";
    case kPmNatalTargetMoon:
      return "natal Moon";
    case kPmNatalTargetAsc:
      return "Asc";
    default:
      return "?";
  }
}
