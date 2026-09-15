#include "faculty175_ephemeris.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "faculty175_astro_math.h"

#define FACULTY175_EPHEMERIS_HD_BASE_URL "https://ephemeris.castalia.institute/data/human-design"
#define FACULTY175_EPHEMERIS_HTTP_TIMEOUT_MS 3000
#define FACULTY175_EPHEMERIS_MAX_BYTES 220000
#define FACULTY175_EPHEMERIS_IO_BYTES 2048
#define FACULTY175_EPHEMERIS_FAIL_BACKOFF_US (30LL * 1000LL * 1000LL)

static const char *TAG = "fac175_ephem";

static const char *const k_hd_json_ids[FACULTY175_HD_BODY_COUNT] = {
    "sun",      "earth",   "moon",    "mercury", "venus", "mars",
    "jupiter", "saturn",  "uranus",  "neptune", "pluto", "true_node", NULL,
};

static const char *const k_hd_labels[FACULTY175_HD_BODY_COUNT] = {
    "SUN", "EAR", "MOO", "MER", "VEN", "MAR", "JUP", "SAT", "URA", "NEP", "PLU", "NNO", "SNO",
};

static faculty175_hd_positions_t s_cache;
static time_t s_cache_epoch_min = -1;
static int64_t s_fail_backoff_until_us;

const char *faculty175_ephemeris_hd_body_label(faculty175_hd_body_t body)
{
    return body >= 0 && body < FACULTY175_HD_BODY_COUNT ? k_hd_labels[body] : "?";
}

static double norm360(double v)
{
    v = fmod(v, 360.0);
    if (v < 0.0) {
        v += 360.0;
    }
    return v;
}

static void day_key_for_epoch(time_t epoch, char *out, size_t cap)
{
    struct tm u = {};
    gmtime_r(&epoch, &u);
    snprintf(out, cap, "%04d-%02d-%02d", u.tm_year + 1900, u.tm_mon + 1, u.tm_mday);
}

static bool parse_int_field(const char *json, const char *key, int *out)
{
    char needle[24];
    snprintf(needle, sizeof(needle), "\"%s\":", key);
    const char *p = strstr(json, needle);
    if (p == NULL) {
        return false;
    }
    p += strlen(needle);
    *out = (int)strtol(p, NULL, 10);
    return true;
}

static bool parse_time_field(const char *json, const char *key, time_t *out)
{
    char needle[24];
    snprintf(needle, sizeof(needle), "\"%s\":", key);
    const char *p = strstr(json, needle);
    if (p == NULL) {
        return false;
    }
    p += strlen(needle);
    *out = (time_t)strtoll(p, NULL, 10);
    return true;
}

static bool nth_array_double(const char *json, const char *body, int idx, double *lon_out)
{
    char needle[24];
    snprintf(needle, sizeof(needle), "\"%s\":[", body);
    const char *p = strstr(json, needle);
    if (p == NULL) {
        return false;
    }
    p += strlen(needle);
    for (int i = 0; i < idx; ++i) {
        p = strchr(p, ',');
        if (p == NULL) {
            return false;
        }
        ++p;
    }
    char *end = NULL;
    const double v = strtod(p, &end);
    if (end == p) {
        return false;
    }
    *lon_out = norm360(v);
    return true;
}

static bool lookup_human_design_json(const char *json, time_t epoch, faculty175_hd_positions_t *out)
{
    int step = 0;
    time_t t0 = 0;
    time_t t1 = 0;
    if (!parse_int_field(json, "step", &step) || step <= 0 ||
        !parse_time_field(json, "t0", &t0) || !parse_time_field(json, "t1", &t1) ||
        epoch < t0 || epoch >= t1) {
        return false;
    }

    const int idx = (int)((epoch - t0) / step);
    faculty175_hd_positions_t parsed = {};
    for (int i = 0; i < FACULTY175_HD_BODY_COUNT; ++i) {
        if (i == FACULTY175_HD_BODY_SOUTH_NODE) {
            parsed.lon[i] = norm360(parsed.lon[FACULTY175_HD_BODY_TRUE_NODE] + 180.0);
            continue;
        }
        if (!nth_array_double(json, k_hd_json_ids[i], idx, &parsed.lon[i])) {
            return false;
        }
    }
    parsed.ok = true;
    parsed.from_network = true;
    *out = parsed;
    return true;
}

static esp_err_t fetch_text_url(const char *url, char **out_body, size_t *out_len)
{
    if (url == NULL || out_body == NULL || out_len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_body = NULL;
    *out_len = 0;

    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = FACULTY175_EPHEMERIS_HTTP_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .keep_alive_enable = false,
        .buffer_size = FACULTY175_EPHEMERIS_IO_BYTES,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        return ESP_ERR_NO_MEM;
    }
    esp_http_client_set_header(client, "User-Agent", "Astrolabe-Faculty175-Ephemeris/1");
    esp_http_client_set_header(client, "Accept", "application/json");

    char *body = NULL;
    uint8_t *buf = NULL;
    size_t cap = 0;
    size_t total = 0;
    esp_err_t err = esp_http_client_open(client, 0);
    if (err == ESP_OK) {
        (void)esp_http_client_fetch_headers(client);
        const int status = esp_http_client_get_status_code(client);
        if (status != 200) {
            ESP_LOGW(TAG, "GET %s HTTP %d", url, status);
            err = ESP_FAIL;
        }
    }
    if (err == ESP_OK) {
        cap = 8192;
        body = (char *)heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (body == NULL) {
            body = (char *)malloc(cap);
        }
        buf = (uint8_t *)malloc(FACULTY175_EPHEMERIS_IO_BYTES);
        if (body == NULL || buf == NULL) {
            err = ESP_ERR_NO_MEM;
        }
    }
    while (err == ESP_OK) {
        const int n = esp_http_client_read(client, (char *)buf, FACULTY175_EPHEMERIS_IO_BYTES);
        if (n < 0) {
            err = ESP_FAIL;
            break;
        }
        if (n == 0) {
            break;
        }
        if (total + (size_t)n > FACULTY175_EPHEMERIS_MAX_BYTES) {
            err = ESP_ERR_INVALID_SIZE;
            break;
        }
        if (total + (size_t)n + 1u > cap) {
            size_t next_cap = cap * 2u;
            while (next_cap < total + (size_t)n + 1u) {
                next_cap *= 2u;
            }
            char *next = (char *)heap_caps_malloc(next_cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (next == NULL) {
                next = (char *)malloc(next_cap);
            }
            if (next == NULL) {
                err = ESP_ERR_NO_MEM;
                break;
            }
            memcpy(next, body, total);
            free(body);
            body = next;
            cap = next_cap;
        }
        memcpy(body + total, buf, (size_t)n);
        total += (size_t)n;
    }

    free(buf);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    if (err != ESP_OK) {
        free(body);
        return err;
    }
    body[total] = '\0';
    *out_body = body;
    *out_len = total;
    return ESP_OK;
}

static void local_human_design_epoch(time_t epoch, faculty175_hd_positions_t *out)
{
    static const double base[FACULTY175_HD_BODY_COUNT] = {
        280.5, 100.5, 218.3, 296.1, 334.2, 54.7, 72.0, 312.0, 41.0, 350.0, 298.0, 23.0, 203.0,
    };
    static const double rate[FACULTY175_HD_BODY_COUNT] = {
        0.985647, 0.985647, 13.176358, 4.092334, 1.602130, 0.524021,
        0.083085, 0.033444, 0.011728, 0.005981, 0.003964, -0.052953, -0.052953,
    };
    const double days = (double)(epoch - 946728000) / 86400.0;
    memset(out, 0, sizeof(*out));
    for (int i = 0; i < FACULTY175_HD_BODY_COUNT; ++i) {
        out->lon[i] = norm360(base[i] + days * rate[i]);
    }
    out->lon[FACULTY175_HD_BODY_SOUTH_NODE] =
        norm360(out->lon[FACULTY175_HD_BODY_TRUE_NODE] + 180.0);
    faculty175_chart_positions_t inner = {0};
    if (faculty175_astro_positions_at_epoch(epoch, &inner)) {
        out->lon[FACULTY175_HD_BODY_SUN] = inner.lon[0];
        out->lon[FACULTY175_HD_BODY_EARTH] = norm360(inner.lon[0] + 180.0);
        out->lon[FACULTY175_HD_BODY_MOON] = inner.lon[1];
        out->lon[FACULTY175_HD_BODY_MERCURY] = inner.lon[2];
        out->lon[FACULTY175_HD_BODY_VENUS] = inner.lon[3];
        out->lon[FACULTY175_HD_BODY_MARS] = inner.lon[4];
        out->lon[FACULTY175_HD_BODY_JUPITER] = inner.lon[5];
        out->lon[FACULTY175_HD_BODY_SATURN] = inner.lon[6];
    } else {
        out->lon[FACULTY175_HD_BODY_EARTH] = norm360(out->lon[FACULTY175_HD_BODY_SUN] + 180.0);
    }
    (void)faculty175_astro_slow_positions_at_epoch(
        epoch,
        &out->lon[FACULTY175_HD_BODY_URANUS],
        &out->lon[FACULTY175_HD_BODY_NEPTUNE],
        &out->lon[FACULTY175_HD_BODY_PLUTO],
        &out->lon[FACULTY175_HD_BODY_TRUE_NODE]);
    out->lon[FACULTY175_HD_BODY_SOUTH_NODE] =
        norm360(out->lon[FACULTY175_HD_BODY_TRUE_NODE] + 180.0);
    out->ok = true;
    out->from_network = false;
}

bool faculty175_ephemeris_fetch_human_design_epoch(time_t utc_epoch, faculty175_hd_positions_t *out)
{
    if (out == NULL || utc_epoch < 0) {
        return false;
    }
    const time_t bucket = utc_epoch / 60;
    if (s_cache.ok && s_cache_epoch_min == bucket) {
        *out = s_cache;
        return true;
    }

    const int64_t now_us = esp_timer_get_time();
    if (s_fail_backoff_until_us == 0 || now_us >= s_fail_backoff_until_us) {
        char day[11];
        char url[192];
        day_key_for_epoch(utc_epoch, day, sizeof(day));
        snprintf(url, sizeof(url), "%s/%s.json", FACULTY175_EPHEMERIS_HD_BASE_URL, day);

        char *json = NULL;
        size_t len = 0;
        if (fetch_text_url(url, &json, &len) == ESP_OK) {
            faculty175_hd_positions_t parsed = {};
            if (lookup_human_design_json(json, utc_epoch, &parsed)) {
                free(json);
                s_cache = parsed;
                s_cache_epoch_min = bucket;
                s_fail_backoff_until_us = 0;
                *out = parsed;
                return true;
            }
            ESP_LOGW(TAG, "HD JSON lookup failed %s (%u bytes)", day, (unsigned)len);
            free(json);
        }
        s_fail_backoff_until_us = now_us + FACULTY175_EPHEMERIS_FAIL_BACKOFF_US;
    }

    faculty175_hd_positions_t local = {};
    local_human_design_epoch(utc_epoch, &local);
    s_cache = local;
    s_cache_epoch_min = bucket;
    *out = local;
    return true;
}
