#include "pm_transit.h"

#include "pm_ephemeris.h"

#include <cmath>
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

static void pm_transit_compute_utc_local(const struct tm *utc, PmTransitPositions *out) {
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
