#include "faculty175_charts.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "nvs.h"
#include "nvs_flash.h"

#define CHARTS_NVS_NS "astro"
#define CHARTS_KEY_PRIMARY "primary"
#define CHARTS_KEY_ACTIVE "active"
#define CHARTS_KEY_SEEDED "fam_seed"
#define CHARTS_EPH_MAGIC 0x45504831u
#define CHARTS_EPH_VERSION 1u

enum {
    BODY_SUN = 0,
    BODY_MOON,
    BODY_MERCURY,
    BODY_VENUS,
    BODY_MARS,
    BODY_JUPITER,
    BODY_SATURN,
};

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t body_count;
    uint32_t birth_fingerprint;
    double lon[FACULTY175_CHART_BODY_COUNT];
    bool ok;
} faculty175_cached_positions_t;

static const faculty175_birth_chart_t k_family_primary = {
    .name = "Daniel McShan",
    .role = FACULTY175_CHART_ROLE_SELF,
    .year = 1972,
    .month = 5,
    .day = 6,
    .hour = 12,
    .minute = 0,
    .lat_deg = 30.4383f,
    .lon_deg = -84.2807f,
    .tz_offset_sec = -4 * 3600,
    .place = "Tallahassee, FL",
    .valid = true,
};

static const faculty175_birth_chart_t k_family_profiles[] = {
    {
        .name = "Camille",
        .role = FACULTY175_CHART_ROLE_PARTNER,
        .year = 1983,
        .month = 9,
        .day = 23,
        .hour = 12,
        .minute = 0,
        .lat_deg = 42.9814f,
        .lon_deg = -70.9478f,
        .tz_offset_sec = -4 * 3600,
        .place = "Exeter, NH",
        .valid = true,
    },
    {
        .name = "Finn",
        .role = FACULTY175_CHART_ROLE_CHILD,
        .year = 2024,
        .month = 4,
        .day = 30,
        .hour = 12,
        .minute = 0,
        .lat_deg = 39.0917f,
        .lon_deg = -104.8728f,
        .tz_offset_sec = -6 * 3600,
        .place = "Monument, CO",
        .valid = true,
    },
    {
        .name = "Aleia",
        .role = FACULTY175_CHART_ROLE_CHILD,
        .year = 2025,
        .month = 5,
        .day = 4,
        .hour = 12,
        .minute = 0,
        .lat_deg = 39.0917f,
        .lon_deg = -104.8728f,
        .tz_offset_sec = -6 * 3600,
        .place = "Monument, CO",
        .valid = true,
    },
};

static bool chart_sane(const faculty175_birth_chart_t *b)
{
    return b != NULL && b->valid && b->name[0] != '\0' && b->year >= 1900 && b->year <= 2100 &&
           b->month >= 1 && b->month <= 12 && b->day >= 1 && b->day <= 31 && b->hour <= 23 &&
           b->minute <= 59 && b->lat_deg >= -90.0f && b->lat_deg <= 90.0f &&
           b->lon_deg >= -180.0f && b->lon_deg <= 180.0f;
}

static void profile_key(int slot, char *out, size_t cap)
{
    snprintf(out, cap, "profile%d", slot);
}

static void positions_key_for_profile(int slot, char *out, size_t cap)
{
    snprintf(out, cap, "eph%d", slot);
}

static uint32_t fnv1a_update(uint32_t h, const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    for (size_t i = 0; i < len; ++i) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

static uint32_t birth_fingerprint(const faculty175_birth_chart_t *b)
{
    uint32_t h = 2166136261u;
    const size_t name_len = strnlen(b->name, sizeof(b->name));
    const size_t place_len = strnlen(b->place, sizeof(b->place));
    h = fnv1a_update(h, b->name, name_len);
    h = fnv1a_update(h, &b->role, sizeof(b->role));
    h = fnv1a_update(h, &b->year, sizeof(b->year));
    h = fnv1a_update(h, &b->month, sizeof(b->month));
    h = fnv1a_update(h, &b->day, sizeof(b->day));
    h = fnv1a_update(h, &b->hour, sizeof(b->hour));
    h = fnv1a_update(h, &b->minute, sizeof(b->minute));
    h = fnv1a_update(h, &b->lat_deg, sizeof(b->lat_deg));
    h = fnv1a_update(h, &b->lon_deg, sizeof(b->lon_deg));
    h = fnv1a_update(h, &b->tz_offset_sec, sizeof(b->tz_offset_sec));
    h = fnv1a_update(h, b->place, place_len);
    return h;
}

static bool positions_cache_load(const char *key, uint32_t fingerprint, faculty175_chart_positions_t *out)
{
    if (key == NULL || out == NULL) {
        return false;
    }
    nvs_handle_t nvs;
    if (nvs_open(CHARTS_NVS_NS, NVS_READONLY, &nvs) != ESP_OK) {
        return false;
    }
    faculty175_cached_positions_t cached = {};
    size_t len = sizeof(cached);
    const esp_err_t err = nvs_get_blob(nvs, key, &cached, &len);
    nvs_close(nvs);
    if (err != ESP_OK || len != sizeof(cached) || cached.magic != CHARTS_EPH_MAGIC ||
        cached.version != CHARTS_EPH_VERSION || cached.body_count != FACULTY175_CHART_BODY_COUNT ||
        cached.birth_fingerprint != fingerprint || !cached.ok) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    memcpy(out->lon, cached.lon, sizeof(out->lon));
    out->ok = true;
    return true;
}

static void positions_cache_save(const char *key, uint32_t fingerprint, const faculty175_chart_positions_t *pos)
{
    if (key == NULL || pos == NULL || !pos->ok) {
        return;
    }
    nvs_handle_t nvs;
    if (nvs_open(CHARTS_NVS_NS, NVS_READWRITE, &nvs) != ESP_OK) {
        return;
    }
    faculty175_cached_positions_t cached = {
        .magic = CHARTS_EPH_MAGIC,
        .version = CHARTS_EPH_VERSION,
        .body_count = FACULTY175_CHART_BODY_COUNT,
        .birth_fingerprint = fingerprint,
        .ok = true,
    };
    memcpy(cached.lon, pos->lon, sizeof(cached.lon));
    if (nvs_set_blob(nvs, key, &cached, sizeof(cached)) == ESP_OK) {
        (void)nvs_commit(nvs);
    }
    nvs_close(nvs);
}

static void positions_cache_erase(const char *key)
{
    if (key == NULL) {
        return;
    }
    nvs_handle_t nvs;
    if (nvs_open(CHARTS_NVS_NS, NVS_READWRITE, &nvs) != ESP_OK) {
        return;
    }
    if (nvs_erase_key(nvs, key) == ESP_OK) {
        (void)nvs_commit(nvs);
    }
    nvs_close(nvs);
}

static bool positions_cache_key_for_birth(const faculty175_birth_chart_t *birth, char *out, size_t cap)
{
    faculty175_birth_chart_t primary = {};
    if (faculty175_charts_primary(&primary) && birth_fingerprint(&primary) == birth_fingerprint(birth)) {
        snprintf(out, cap, "eph_primary");
        return true;
    }
    for (int i = 0; i < FACULTY175_CHART_PROFILE_SLOTS; ++i) {
        faculty175_birth_chart_t profile = {};
        if (faculty175_charts_profile_get(i, &profile) &&
            birth_fingerprint(&profile) == birth_fingerprint(birth)) {
            positions_key_for_profile(i, out, cap);
            return true;
        }
    }
    return false;
}

static double rev360(double x)
{
    x = fmod(x, 360.0);
    if (x < 0.0) {
        x += 360.0;
    }
    return x;
}

static bool leap_year(int y)
{
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

static int64_t days_from_civil(int y, unsigned m, unsigned d)
{
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

bool faculty175_charts_birth_to_utc(const faculty175_birth_chart_t *birth, time_t *utc_out)
{
    if (!chart_sane(birth) || utc_out == NULL) {
        return false;
    }
    static const uint8_t k_days_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const uint8_t max_day = birth->month == 2 && leap_year(birth->year) ? 29 : k_days_month[birth->month - 1];
    if (birth->day > max_day) {
        return false;
    }
    const int64_t days = days_from_civil((int)birth->year, birth->month, birth->day);
    const int64_t local = days * 86400 + (int64_t)birth->hour * 3600 + (int64_t)birth->minute * 60;
    *utc_out = (time_t)(local - (int64_t)birth->tz_offset_sec);
    return true;
}

esp_err_t faculty175_charts_save_primary(const faculty175_birth_chart_t *chart)
{
    if (!chart_sane(chart)) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(CHARTS_NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_blob(nvs, CHARTS_KEY_PRIMARY, chart, sizeof(*chart));
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    if (err == ESP_OK) {
        positions_cache_erase("eph_primary");
    }
    return err;
}

bool faculty175_charts_primary(faculty175_birth_chart_t *out)
{
    if (out == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    nvs_handle_t nvs;
    if (nvs_open(CHARTS_NVS_NS, NVS_READONLY, &nvs) != ESP_OK) {
        return false;
    }
    size_t len = sizeof(*out);
    const esp_err_t err = nvs_get_blob(nvs, CHARTS_KEY_PRIMARY, out, &len);
    nvs_close(nvs);
    return err == ESP_OK && len == sizeof(*out) && chart_sane(out);
}

esp_err_t faculty175_charts_profile_save(int slot, const faculty175_birth_chart_t *chart)
{
    if (slot < 0 || slot >= FACULTY175_CHART_PROFILE_SLOTS || !chart_sane(chart)) {
        return ESP_ERR_INVALID_ARG;
    }
    char key[16];
    profile_key(slot, key, sizeof(key));
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(CHARTS_NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_blob(nvs, key, chart, sizeof(*chart));
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    if (err == ESP_OK) {
        char eph_key[16];
        positions_key_for_profile(slot, eph_key, sizeof(eph_key));
        positions_cache_erase(eph_key);
    }
    return err;
}

bool faculty175_charts_profile_get(int slot, faculty175_birth_chart_t *out)
{
    if (slot < 0 || slot >= FACULTY175_CHART_PROFILE_SLOTS || out == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    char key[16];
    profile_key(slot, key, sizeof(key));
    nvs_handle_t nvs;
    if (nvs_open(CHARTS_NVS_NS, NVS_READONLY, &nvs) != ESP_OK) {
        return false;
    }
    size_t len = sizeof(*out);
    const esp_err_t err = nvs_get_blob(nvs, key, out, &len);
    nvs_close(nvs);
    return err == ESP_OK && len == sizeof(*out) && chart_sane(out);
}

int faculty175_charts_profile_count(void)
{
    int count = 0;
    for (int i = 0; i < FACULTY175_CHART_PROFILE_SLOTS; ++i) {
        faculty175_birth_chart_t tmp = {};
        if (faculty175_charts_profile_get(i, &tmp)) {
            ++count;
        }
    }
    return count;
}

static bool profile_name_matches(const char *stored, const char *seed)
{
    if (stored == NULL || seed == NULL || stored[0] == '\0' || seed[0] == '\0') {
        return false;
    }
    if (strcasecmp(stored, seed) == 0) {
        return true;
    }
    const size_t seed_len = strlen(seed);
    return strncasecmp(stored, seed, seed_len) == 0 &&
           (stored[seed_len] == '\0' || stored[seed_len] == ' ');
}

static bool profile_seed_exists(const faculty175_birth_chart_t *seed)
{
    for (int i = 0; i < FACULTY175_CHART_PROFILE_SLOTS; ++i) {
        faculty175_birth_chart_t existing = {};
        if (faculty175_charts_profile_get(i, &existing) && profile_name_matches(existing.name, seed->name)) {
            return true;
        }
    }
    return false;
}

static int first_free_profile_slot(void)
{
    for (int i = 0; i < FACULTY175_CHART_PROFILE_SLOTS; ++i) {
        faculty175_birth_chart_t existing = {};
        if (!faculty175_charts_profile_get(i, &existing)) {
            return i;
        }
    }
    return -1;
}

void faculty175_charts_ensure_family_seed(void)
{
    nvs_handle_t nvs;
    if (nvs_open(CHARTS_NVS_NS, NVS_READWRITE, &nvs) != ESP_OK) {
        return;
    }
    nvs_close(nvs);

    faculty175_birth_chart_t primary = {};
    if (!faculty175_charts_primary(&primary)) {
        (void)faculty175_charts_save_primary(&k_family_primary);
    }
    for (size_t i = 0; i < sizeof(k_family_profiles) / sizeof(k_family_profiles[0]); ++i) {
        if (!profile_seed_exists(&k_family_profiles[i])) {
            const int slot = first_free_profile_slot();
            if (slot < 0) {
                break;
            }
            (void)faculty175_charts_profile_save(slot, &k_family_profiles[i]);
        }
    }
    if (faculty175_charts_active_slot() < 0) {
        (void)faculty175_charts_set_active_slot(0);
    }
    if (faculty175_charts_primary(&primary)) {
        faculty175_chart_positions_t pos = {};
        (void)faculty175_charts_birth_positions(&primary, &pos);
    }
    for (int i = 0; i < FACULTY175_CHART_PROFILE_SLOTS; ++i) {
        faculty175_birth_chart_t profile = {};
        if (faculty175_charts_profile_get(i, &profile)) {
            faculty175_chart_positions_t pos = {};
            (void)faculty175_charts_birth_positions(&profile, &pos);
        }
    }

    if (nvs_open(CHARTS_NVS_NS, NVS_READWRITE, &nvs) == ESP_OK) {
        (void)nvs_set_u8(nvs, CHARTS_KEY_SEEDED, 1);
        (void)nvs_commit(nvs);
        nvs_close(nvs);
    }
}

int faculty175_charts_active_slot(void)
{
    nvs_handle_t nvs;
    if (nvs_open(CHARTS_NVS_NS, NVS_READONLY, &nvs) != ESP_OK) {
        return -1;
    }
    int32_t slot = -1;
    (void)nvs_get_i32(nvs, CHARTS_KEY_ACTIVE, &slot);
    nvs_close(nvs);
    faculty175_birth_chart_t tmp = {};
    return faculty175_charts_profile_get((int)slot, &tmp) ? (int)slot : -1;
}

bool faculty175_charts_set_active_slot(int slot)
{
    faculty175_birth_chart_t tmp = {};
    if (!faculty175_charts_profile_get(slot, &tmp)) {
        return false;
    }
    nvs_handle_t nvs;
    if (nvs_open(CHARTS_NVS_NS, NVS_READWRITE, &nvs) != ESP_OK) {
        return false;
    }
    const esp_err_t err = nvs_set_i32(nvs, CHARTS_KEY_ACTIVE, slot);
    if (err == ESP_OK) {
        (void)nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err == ESP_OK;
}

bool faculty175_charts_active(faculty175_birth_chart_t *out)
{
    int slot = faculty175_charts_active_slot();
    if (slot < 0) {
        for (int i = 0; i < FACULTY175_CHART_PROFILE_SLOTS; ++i) {
            faculty175_birth_chart_t tmp = {};
            if (faculty175_charts_profile_get(i, &tmp)) {
                slot = i;
                break;
            }
        }
    }
    return slot >= 0 && faculty175_charts_profile_get(slot, out);
}

bool faculty175_charts_cycle_active(int delta, int *slot_out, faculty175_birth_chart_t *profile_out)
{
    int slots[FACULTY175_CHART_PROFILE_SLOTS];
    int n = 0;
    for (int i = 0; i < FACULTY175_CHART_PROFILE_SLOTS; ++i) {
        faculty175_birth_chart_t tmp = {};
        if (faculty175_charts_profile_get(i, &tmp)) {
            slots[n++] = i;
        }
    }
    if (n <= 0) {
        return false;
    }
    int cur = faculty175_charts_active_slot();
    int pos = 0;
    for (int i = 0; i < n; ++i) {
        if (slots[i] == cur) {
            pos = i;
            break;
        }
    }
    const int next_pos = (pos + (delta % n) + n) % n;
    const int next_slot = slots[next_pos];
    if (!faculty175_charts_set_active_slot(next_slot)) {
        return false;
    }
    if (slot_out != NULL) {
        *slot_out = next_slot;
    }
    return profile_out == NULL || faculty175_charts_profile_get(next_slot, profile_out);
}

static double julian_day_ut(const struct tm *u)
{
    int y = u->tm_year + 1900;
    int m = u->tm_mon + 1;
    const int d = u->tm_mday;
    const double h = (double)u->tm_hour + (double)u->tm_min / 60.0 + (double)u->tm_sec / 3600.0;
    if (m <= 2) {
        y -= 1;
        m += 12;
    }
    const int a = y / 100;
    const int b = 2 - a + (a / 4);
    return floor(365.25 * (y + 4716)) + floor(30.6001 * (m + 1)) + (double)d + (double)b - 1524.5 +
           h / 24.0;
}

static void sun_rect_and_mean(double d, double *xs, double *ys, double *zs, double *lon_deg, double *ls_deg,
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
    double n = rev360(125.1228 - 0.0529538083 * d);
    const double i = 5.1454;
    double w = rev360(318.0634 + 0.1643573223 * d);
    const double a = 60.2666;
    const double e = 0.054900;
    double m = rev360(115.3654 + 13.0649929509 * d);
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
    const double dd = rev360(lm - rev360(ls_deg));
    const double f = rev360(lm - n);
    lon = rev360(lon - 1.274 * sin((m - 2.0 * dd) * (M_PI / 180.0)) +
                 0.658 * sin((2.0 * dd) * (M_PI / 180.0)) -
                 0.186 * sin(rev360(ms_deg) * (M_PI / 180.0)) + 0.0 * f);
    *lon_deg = lon;
}

static void planet_helio_geo(double d, double n0, double i0, double w0, double a, double e0, double m0,
                             double dn, double di, double dw, double de, double dm, double xs, double ys,
                             double zs, double *lon_deg)
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
        const double d_e = (e_anom - (180.0 / M_PI) * e * sin(er) - m) / (1.0 - e * cos(er));
        e_anom -= d_e;
        if (fabs(d_e) < 1e-7) {
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
    const double zh = r * sin(vw) * sin(ir);
    (void)zs;
    *lon_deg = rev360(atan2(yh + ys, xh + xs) * (180.0 / M_PI) + 0.0 * zh);
}

static void compute_utc(const struct tm *utc, faculty175_chart_positions_t *out)
{
    memset(out, 0, sizeof(*out));
    const double jd = julian_day_ut(utc);
    const double d = jd - 2451543.5;
    double xs = 0, ys = 0, zs = 0, ls = 0, ms = 0;
    sun_rect_and_mean(d, &xs, &ys, &zs, &out->lon[BODY_SUN], &ls, &ms);
    moon_lon(d, ls, ms, &out->lon[BODY_MOON]);
    planet_helio_geo(d, 48.3313, 7.0047, 29.1241, 0.387098, 0.205635, 168.6562, 3.24587e-5, 5.00e-8,
                     1.01444e-5, 5.59e-10, 4.0923344368, xs, ys, zs, &out->lon[BODY_MERCURY]);
    planet_helio_geo(d, 76.6799, 3.3946, 54.8910, 0.723330, 0.006773, 48.0052, 2.46590e-5, 2.75e-8,
                     1.38374e-5, -1.302e-9, 1.6021302244, xs, ys, zs, &out->lon[BODY_VENUS]);
    planet_helio_geo(d, 49.5574, 1.8497, 286.5016, 1.523688, 0.093405, 18.6021, 2.11081e-5, -1.78e-8,
                     2.92961e-5, 2.516e-9, 0.5240207766, xs, ys, zs, &out->lon[BODY_MARS]);
    planet_helio_geo(d, 100.4542, 1.3030, 273.8777, 5.20256, 0.048498, 19.8950, 2.76854e-5, -1.557e-7,
                     1.64505e-5, 4.469e-9, 0.0830853001, xs, ys, zs, &out->lon[BODY_JUPITER]);
    planet_helio_geo(d, 113.6634, 2.4886, 339.3939, 9.55475, 0.055546, 316.9670, 2.38980e-5, -1.081e-7,
                     2.97661e-5, -9.499e-9, 0.0334442282, xs, ys, zs, &out->lon[BODY_SATURN]);
    out->ok = true;
}

bool faculty175_charts_birth_positions(const faculty175_birth_chart_t *birth, faculty175_chart_positions_t *out)
{
    if (!chart_sane(birth) || out == NULL) {
        return false;
    }
    char cache_key[16] = {};
    const uint32_t fingerprint = birth_fingerprint(birth);
    const bool cacheable = positions_cache_key_for_birth(birth, cache_key, sizeof(cache_key));
    if (cacheable && positions_cache_load(cache_key, fingerprint, out)) {
        return true;
    }
    time_t epoch = 0;
    if (!faculty175_charts_birth_to_utc(birth, &epoch)) {
        return false;
    }
    struct tm utc = {};
    gmtime_r(&epoch, &utc);
    compute_utc(&utc, out);
    if (cacheable && out->ok) {
        positions_cache_save(cache_key, fingerprint, out);
    }
    return out->ok;
}

const char *faculty175_charts_body_label(int body)
{
    static const char *const k[] = {"Su", "Mo", "Me", "Ve", "Ma", "Ju", "Sa"};
    return body >= 0 && body < FACULTY175_CHART_BODY_COUNT ? k[body] : "?";
}

const char *faculty175_charts_zodiac_abbr(double lon)
{
    static const char *const k[] = {"Ar", "Ta", "Ge", "Cn", "Le", "Vi", "Li", "Sc", "Sg", "Cp", "Aq", "Pi"};
    const int idx = ((int)(rev360(lon) / 30.0)) % 12;
    return k[idx];
}

const char *faculty175_charts_role_label(faculty175_chart_role_t role)
{
    switch (role) {
        case FACULTY175_CHART_ROLE_SELF:
            return "self";
        case FACULTY175_CHART_ROLE_CHILD:
            return "child";
        case FACULTY175_CHART_ROLE_PARTNER:
        default:
            return "partner";
    }
}

bool faculty175_charts_handle(const char *line)
{
    if (line == NULL || (strcasecmp(line, "charts") != 0 && strncasecmp(line, "charts ", 7) != 0 &&
                         strcasecmp(line, "chart") != 0 && strncasecmp(line, "chart ", 6) != 0)) {
        return false;
    }
    faculty175_charts_ensure_family_seed();
    const char *sub = strchr(line, ' ');
    sub = sub != NULL ? sub + 1 : "";
    while (*sub == ' ') {
        ++sub;
    }
    if (*sub == '\0' || strcasecmp(sub, "status") == 0 || strcasecmp(sub, "list") == 0) {
        faculty175_birth_chart_t primary = {};
        printf("charts: primary=%s profiles=%d active=%d\n",
               faculty175_charts_primary(&primary) ? primary.name : "-", faculty175_charts_profile_count(),
               faculty175_charts_active_slot());
        for (int i = 0; i < FACULTY175_CHART_PROFILE_SLOTS; ++i) {
            faculty175_birth_chart_t p = {};
            if (faculty175_charts_profile_get(i, &p)) {
                printf("chart: slot=%d name=%s role=%s date=%04u-%02u-%02u time=%02u:%02u place=%s%s\n", i,
                       p.name, faculty175_charts_role_label(p.role), p.year, p.month, p.day, p.hour, p.minute,
                       p.place, i == faculty175_charts_active_slot() ? " active" : "");
            }
        }
        fflush(stdout);
        return true;
    }
    if (strcasecmp(sub, "seed") == 0) {
        printf("charts: family seed ok profiles=%d\n", faculty175_charts_profile_count());
        fflush(stdout);
        return true;
    }
    if (strcasecmp(sub, "next") == 0 || strcasecmp(sub, "prev") == 0) {
        int slot = -1;
        faculty175_birth_chart_t p = {};
        if (faculty175_charts_cycle_active(strcasecmp(sub, "next") == 0 ? 1 : -1, &slot, &p)) {
            printf("charts: active=%d %s\n", slot, p.name);
        } else {
            printf("charts: no active profile\n");
        }
        fflush(stdout);
        return true;
    }
    if (strcasecmp(sub, "help") == 0) {
        printf("charts commands:\n  charts status\n  charts seed\n  charts next\n  charts prev\n");
        fflush(stdout);
        return true;
    }
    printf("charts: unknown command\n");
    fflush(stdout);
    return true;
}
