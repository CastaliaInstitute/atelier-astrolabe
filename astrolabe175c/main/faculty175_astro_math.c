#include "faculty175_astro_math.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

enum {
    BODY_SUN = 0,
    BODY_MOON,
    BODY_MERCURY,
    BODY_VENUS,
    BODY_MARS,
    BODY_JUPITER,
    BODY_SATURN,
};

static double rev360(double x)
{
    x = fmod(x, 360.0);
    return x < 0.0 ? x + 360.0 : x;
}

static double julian_day_ut(const struct tm *u)
{
    int y = u->tm_year + 1900;
    int m = u->tm_mon + 1;
    const int d = u->tm_mday;
    const double h = (double)u->tm_hour + (double)u->tm_min / 60.0 + (double)u->tm_sec / 3600.0;
    if (m <= 2) {
        --y;
        m += 12;
    }
    const int a = y / 100;
    const int b = 2 - a + (a / 4);
    return floor(365.25 * (y + 4716)) + floor(30.6001 * (m + 1)) + (double)d + (double)b - 1524.5 +
           h / 24.0;
}

static void sun_rect_and_mean(double d,
                              double *xs,
                              double *ys,
                              double *zs,
                              double *lon_deg,
                              double *ls_deg,
                              double *ms_deg)
{
    const double w = 282.9404 + 4.70935e-5 * d;
    const double e = 0.016709 - 1.151e-9 * d;
    const double m = rev360(356.0470 + 0.9856002585 * d);
    const double ls = rev360(w + m);
    const double mr = m * (M_PI / 180.0);
    const double e_anom = rev360(m + (180.0 / M_PI) * e * sin(mr) * (1.0 + e * cos(mr)));
    const double er = e_anom * (M_PI / 180.0);
    const double xv = cos(er) - e;
    const double yv = sin(er) * sqrt(1.0 - e * e);
    const double v = atan2(yv, xv) * (180.0 / M_PI);
    const double lon = rev360(v + w);
    *xs = cos(lon * (M_PI / 180.0));
    *ys = sin(lon * (M_PI / 180.0));
    *zs = 0.0;
    *lon_deg = lon;
    *ls_deg = ls;
    *ms_deg = m;
}

static void moon_lon(double d, double ls_deg, double ms_deg, double *lon_deg)
{
    const double n = rev360(125.1228 - 0.0529538083 * d);
    const double i = 5.1454;
    const double w = rev360(318.0634 + 0.1643573223 * d);
    const double a = 60.2666;
    const double e = 0.054900;
    const double m = rev360(115.3654 + 13.0649929509 * d);
    double e_anom = m + (180.0 / M_PI) * e * sin(m * (M_PI / 180.0)) *
                            (1.0 + e * cos(m * (M_PI / 180.0)));
    for (int iter = 0; iter < 6; ++iter) {
        e_anom = rev360(e_anom);
        const double er = e_anom * (M_PI / 180.0);
        const double de = (e_anom - (180.0 / M_PI) * e * sin(er) - m) / (1.0 - e * cos(er));
        e_anom -= de;
        if (fabs(de) < 1e-6) {
            break;
        }
    }
    const double er = rev360(e_anom) * (M_PI / 180.0);
    const double xv = a * (cos(er) - e);
    const double yv = a * sqrt(1.0 - e * e) * sin(er);
    const double v = atan2(yv, xv) * (180.0 / M_PI);
    const double r = sqrt(xv * xv + yv * yv);
    const double nr = n * (M_PI / 180.0);
    const double ir = i * (M_PI / 180.0);
    const double vw = (v + w) * (M_PI / 180.0);
    const double xe = r * (cos(nr) * cos(vw) - sin(nr) * sin(vw) * cos(ir));
    const double ye = r * (sin(nr) * cos(vw) + cos(nr) * sin(vw) * cos(ir));
    double lon = rev360(atan2(ye, xe) * (180.0 / M_PI));
    const double lm = rev360(n + w + m);
    const double elongation = rev360(lm - rev360(ls_deg));
    lon = rev360(lon - 1.274 * sin((m - 2.0 * elongation) * (M_PI / 180.0)) +
                 0.658 * sin((2.0 * elongation) * (M_PI / 180.0)) -
                 0.186 * sin(rev360(ms_deg) * (M_PI / 180.0)));
    *lon_deg = lon;
}

static void planet_helio_geo(double d,
                             double n0,
                             double i0,
                             double w0,
                             double a,
                             double e0,
                             double m0,
                             double dn,
                             double di,
                             double dw,
                             double de,
                             double dm,
                             double xs,
                             double ys,
                             double *lon_deg)
{
    const double n = rev360(n0 + dn * d);
    const double i = rev360(i0 + di * d);
    const double w = rev360(w0 + dw * d);
    const double e = e0 + de * d;
    const double m = rev360(m0 + dm * d);
    double e_anom = m + (180.0 / M_PI) * e * sin(m * (M_PI / 180.0)) *
                            (1.0 + e * cos(m * (M_PI / 180.0)));
    for (int iter = 0; iter < 10; ++iter) {
        e_anom = rev360(e_anom);
        const double er = e_anom * (M_PI / 180.0);
        const double delta = (e_anom - (180.0 / M_PI) * e * sin(er) - m) / (1.0 - e * cos(er));
        e_anom -= delta;
        if (fabs(delta) < 1e-7) {
            break;
        }
    }
    const double er = rev360(e_anom) * (M_PI / 180.0);
    const double xv = a * (cos(er) - e);
    const double yv = a * sqrt(1.0 - e * e) * sin(er);
    const double v = atan2(yv, xv) * (180.0 / M_PI);
    const double r = sqrt(xv * xv + yv * yv);
    const double nr = n * (M_PI / 180.0);
    const double ir = i * (M_PI / 180.0);
    const double vw = (v + w) * (M_PI / 180.0);
    const double xh = r * (cos(nr) * cos(vw) - sin(nr) * sin(vw) * cos(ir));
    const double yh = r * (sin(nr) * cos(vw) + cos(nr) * sin(vw) * cos(ir));
    *lon_deg = rev360(atan2(yh + ys, xh + xs) * (180.0 / M_PI));
}

bool faculty175_astro_positions_at_utc(const struct tm *utc, faculty175_chart_positions_t *out)
{
    if (utc == NULL || out == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    const double d = julian_day_ut(utc) - 2451543.5;
    double xs = 0.0, ys = 0.0, zs = 0.0, ls = 0.0, ms = 0.0;
    sun_rect_and_mean(d, &xs, &ys, &zs, &out->lon[BODY_SUN], &ls, &ms);
    moon_lon(d, ls, ms, &out->lon[BODY_MOON]);
    planet_helio_geo(d, 48.3313, 7.0047, 29.1241, 0.387098, 0.205635, 168.6562, 3.24587e-5, 5.00e-8,
                     1.01444e-5, 5.59e-10, 4.0923344368, xs, ys, &out->lon[BODY_MERCURY]);
    planet_helio_geo(d, 76.6799, 3.3946, 54.8910, 0.723330, 0.006773, 48.0052, 2.46590e-5, 2.75e-8,
                     1.38374e-5, -1.302e-9, 1.6021302244, xs, ys, &out->lon[BODY_VENUS]);
    planet_helio_geo(d, 49.5574, 1.8497, 286.5016, 1.523688, 0.093405, 18.6021, 2.11081e-5, -1.78e-8,
                     2.92961e-5, 2.516e-9, 0.5240207766, xs, ys, &out->lon[BODY_MARS]);
    planet_helio_geo(d, 100.4542, 1.3030, 273.8777, 5.20256, 0.048498, 19.8950, 2.76854e-5, -1.557e-7,
                     1.64505e-5, 4.469e-9, 0.0830853001, xs, ys, &out->lon[BODY_JUPITER]);
    planet_helio_geo(d, 113.6634, 2.4886, 339.3939, 9.55475, 0.055546, 316.9670, 2.38980e-5, -1.081e-7,
                     2.97661e-5, -9.499e-9, 0.0334442282, xs, ys, &out->lon[BODY_SATURN]);
    out->ok = true;
    (void)zs;
    return true;
}

bool faculty175_astro_positions_at_epoch(time_t epoch, faculty175_chart_positions_t *out)
{
    if (epoch <= 0 || out == NULL) {
        return false;
    }
    struct tm utc = {};
    gmtime_r(&epoch, &utc);
    return faculty175_astro_positions_at_utc(&utc, out);
}

bool faculty175_astro_slow_positions_at_epoch(time_t epoch,
                                              double *uranus_lon,
                                              double *neptune_lon,
                                              double *pluto_lon,
                                              double *mean_node_lon)
{
    if (epoch <= 0 || uranus_lon == NULL || neptune_lon == NULL ||
        pluto_lon == NULL || mean_node_lon == NULL) {
        return false;
    }
    struct tm utc = {};
    gmtime_r(&epoch, &utc);
    const double d = julian_day_ut(&utc) - 2451543.5;
    double xs = 0.0, ys = 0.0, zs = 0.0, sun_lon = 0.0, ls = 0.0, ms = 0.0;
    sun_rect_and_mean(d, &xs, &ys, &zs, &sun_lon, &ls, &ms);
    planet_helio_geo(d, 74.0005, 0.7733, 96.6612, 19.18171, 0.047318, 142.5905,
                     1.3978e-5, 1.9e-8, 3.0565e-5, 7.45e-9, 0.011725806,
                     xs, ys, uranus_lon);
    planet_helio_geo(d, 131.7806, 1.7700, 272.8461, 30.05826, 0.008606, 260.2471,
                     3.0173e-5, -2.55e-7, -6.027e-6, 2.15e-9, 0.005995147,
                     xs, ys, neptune_lon);

    const double s = rev360(50.03 + 0.033459652 * d) * (M_PI / 180.0);
    const double p = rev360(238.95 + 0.003968789 * d) * (M_PI / 180.0);
    const double pluto_helio_lon = rev360(238.9508 + 0.00400703 * d
                                          - 19.799 * sin(p) + 19.848 * cos(p)
                                          + 0.897 * sin(2.0 * p) - 4.956 * cos(2.0 * p)
                                          + 0.610 * sin(3.0 * p) + 1.211 * cos(3.0 * p)
                                          - 0.341 * sin(4.0 * p) - 0.190 * cos(4.0 * p)
                                          + 0.128 * sin(5.0 * p) - 0.034 * cos(5.0 * p)
                                          - 0.038 * sin(6.0 * p) + 0.031 * cos(6.0 * p)
                                          + 0.020 * sin(s - p) - 0.010 * cos(s - p));
    const double pluto_helio_lat = -3.9082 - 5.453 * sin(p) - 14.975 * cos(p)
                                   + 3.527 * sin(2.0 * p) + 1.673 * cos(2.0 * p)
                                   - 1.051 * sin(3.0 * p) + 0.328 * cos(3.0 * p)
                                   + 0.179 * sin(4.0 * p) - 0.292 * cos(4.0 * p)
                                   + 0.019 * sin(5.0 * p) + 0.100 * cos(5.0 * p)
                                   - 0.031 * sin(6.0 * p) - 0.026 * cos(6.0 * p)
                                   + 0.011 * cos(s - p);
    const double pluto_r = 40.72 + 6.68 * sin(p) + 6.90 * cos(p)
                           - 1.18 * sin(2.0 * p) - 0.03 * cos(2.0 * p)
                           + 0.15 * sin(3.0 * p) - 0.14 * cos(3.0 * p);
    const double pluto_lon_rad = pluto_helio_lon * (M_PI / 180.0);
    const double pluto_lat_rad = pluto_helio_lat * (M_PI / 180.0);
    const double pluto_xh = pluto_r * cos(pluto_lon_rad) * cos(pluto_lat_rad);
    const double pluto_yh = pluto_r * sin(pluto_lon_rad) * cos(pluto_lat_rad);
    *pluto_lon = rev360(atan2(pluto_yh + ys, pluto_xh + xs) * (180.0 / M_PI));
    *mean_node_lon = rev360(125.1228 - 0.0529538083 * d);
    (void)zs;
    (void)sun_lon;
    (void)ls;
    (void)ms;
    return true;
}
