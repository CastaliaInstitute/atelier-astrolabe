#include "faculty175_charts.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "cJSON.h"
#include "esp_attr.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "faculty175_device_auth.h"

#if __has_include("secrets.local.h")
#include "secrets.local.h"
#else
#include "secrets.example.h"
#endif

#define CHARTS_NVS_NS "astro"
#define CHARTS_KEY_PRIMARY "primary"
#define CHARTS_KEY_ACTIVE "active"
#define CHARTS_KEY_SEEDED "fam_seed4"
#define CHARTS_KEY_REPO_PATH "repo_path"
#define CHARTS_KEY_REPO_SYNC "repo_sync"
#define CASTALIA_NVS_NS "castalia"
#define CASTALIA_KEY_INDIVIDUAL "individual"
#define CHARTS_HTTP_TIMEOUT_MS 8000
#define CHARTS_HTTP_IO_BYTES 1024
#define CHARTS_HTTP_MAX_BYTES (64 * 1024)

#ifndef MYNAH_CASTALIA_INDIVIDUAL_DEFAULT
#if defined(ASTROLABE_FORCE_VARIANT_LUNASAY)
#define MYNAH_CASTALIA_INDIVIDUAL_DEFAULT ""
#else
#define MYNAH_CASTALIA_INDIVIDUAL_DEFAULT "DanielCMcShan"
#endif
#endif
#ifndef MYNAH_CASTALIA_REPO_OWNER
#define MYNAH_CASTALIA_REPO_OWNER "CastaliaInstitute"
#endif
#ifndef MYNAH_CASTALIA_REPO_PREFIX
#define MYNAH_CASTALIA_REPO_PREFIX "castalia-"
#endif
#ifndef MYNAH_CASTALIA_GITHUB_TOKEN
#define MYNAH_CASTALIA_GITHUB_TOKEN ""
#endif
#ifndef MYNAH_CASTALIA_FAMILY_REPO_URL
#define MYNAH_CASTALIA_FAMILY_REPO_URL ""
#endif

static const char *TAG = "fac175_charts";

static bool valid_profile_char(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
}

static void normalize_individual(const char *in, char *out, size_t cap)
{
    if (out == NULL || cap == 0) {
        return;
    }
    out[0] = '\0';
    if (in == NULL) {
        return;
    }
    while (*in == ' ') {
        ++in;
    }
    const char *slash = strrchr(in, '/');
    if (slash != NULL) {
        in = slash + 1;
    }
    if (strncmp(in, MYNAH_CASTALIA_REPO_PREFIX, strlen(MYNAH_CASTALIA_REPO_PREFIX)) == 0) {
        in += strlen(MYNAH_CASTALIA_REPO_PREFIX);
    }
    size_t o = 0;
    for (size_t i = 0; in[i] != '\0' && o + 1 < cap; ++i) {
        if (in[i] == ' ') {
            continue;
        }
        if (!valid_profile_char(in[i])) {
            break;
        }
        out[o++] = in[i];
    }
    out[o] = '\0';
}

static void trim_url_slash(char *url)
{
    if (url == NULL) {
        return;
    }
    size_t len = strlen(url);
    while (len > 0 && url[len - 1] == '/') {
        url[--len] = '\0';
    }
}

static void url_encode_component(const char *in, char *out, size_t cap)
{
    static const char hex[] = "0123456789ABCDEF";
    if (out == NULL || cap == 0) {
        return;
    }
    out[0] = '\0';
    if (in == NULL) {
        return;
    }
    size_t o = 0;
    for (size_t i = 0; in[i] != '\0' && o + 4 < cap; ++i) {
        const unsigned char c = (unsigned char)in[i];
        const bool safe = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                          (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~';
        if (safe) {
            out[o++] = (char)c;
        } else {
            out[o++] = '%';
            out[o++] = hex[(c >> 4) & 0x0f];
            out[o++] = hex[c & 0x0f];
        }
    }
    out[o] = '\0';
}

enum {
    BODY_SUN = 0,
    BODY_MOON,
    BODY_MERCURY,
    BODY_VENUS,
    BODY_MARS,
    BODY_JUPITER,
    BODY_SATURN,
};

#if !defined(ASTROLABE_FORCE_VARIANT_LUNASAY)
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
        .name = "Camille St Martin",
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
#endif

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

static bool chart_sane(const faculty175_birth_chart_t *b);

/* LunaSay's LVGL task uses a PSRAM stack to preserve scarce internal RAM.
 * NVS operations temporarily disable the flash cache, and ESP-IDF cannot do
 * that safely while the current stack lives in PSRAM. Hydrate chart state on
 * the internal boot stack and serve render-time reads from this cache. */
static portMUX_TYPE s_chart_cache_mux = portMUX_INITIALIZER_UNLOCKED;
static bool s_chart_cache_ready;
static bool s_chart_cache_primary_valid;
static EXT_RAM_BSS_ATTR faculty175_birth_chart_t s_chart_cache_primary;
static EXT_RAM_BSS_ATTR bool s_chart_cache_profile_valid[FACULTY175_CHART_PROFILE_SLOTS];
static EXT_RAM_BSS_ATTR faculty175_birth_chart_t s_chart_cache_profiles[FACULTY175_CHART_PROFILE_SLOTS];
static int s_chart_cache_active = -1;

static bool charts_read_primary_nvs(faculty175_birth_chart_t *out)
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

static bool charts_read_profile_nvs(int slot, faculty175_birth_chart_t *out)
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

static void charts_cache_reload_from_nvs(void)
{
    faculty175_birth_chart_t primary = {};
    faculty175_birth_chart_t profiles[FACULTY175_CHART_PROFILE_SLOTS] = {};
    bool valid[FACULTY175_CHART_PROFILE_SLOTS] = {};
    const bool primary_valid = charts_read_primary_nvs(&primary);
    for (int i = 0; i < FACULTY175_CHART_PROFILE_SLOTS; ++i) {
        valid[i] = charts_read_profile_nvs(i, &profiles[i]);
    }
    int32_t active = -1;
    nvs_handle_t nvs;
    if (nvs_open(CHARTS_NVS_NS, NVS_READONLY, &nvs) == ESP_OK) {
        (void)nvs_get_i32(nvs, CHARTS_KEY_ACTIVE, &active);
        nvs_close(nvs);
    }
    if (active < 0 || active >= FACULTY175_CHART_PROFILE_SLOTS || !valid[active]) {
        active = -1;
    }

    portENTER_CRITICAL(&s_chart_cache_mux);
    s_chart_cache_primary = primary;
    s_chart_cache_primary_valid = primary_valid;
    memcpy(s_chart_cache_profiles, profiles, sizeof(profiles));
    memcpy(s_chart_cache_profile_valid, valid, sizeof(valid));
    s_chart_cache_active = (int)active;
    s_chart_cache_ready = true;
    portEXIT_CRITICAL(&s_chart_cache_mux);
}

static void copy_json_string(const cJSON *item, char *out, size_t cap)
{
    if (out == NULL || cap == 0) {
        return;
    }
    out[0] = '\0';
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        strncpy(out, item->valuestring, cap - 1);
        out[cap - 1] = '\0';
    }
}

static const cJSON *json_get_any(const cJSON *obj, const char *a, const char *b, const char *c)
{
    const cJSON *item = obj != NULL && a != NULL ? cJSON_GetObjectItemCaseSensitive(obj, a) : NULL;
    if (item == NULL && b != NULL) {
        item = cJSON_GetObjectItemCaseSensitive(obj, b);
    }
    if (item == NULL && c != NULL) {
        item = cJSON_GetObjectItemCaseSensitive(obj, c);
    }
    return item;
}

static bool json_number_any(const cJSON *obj, double *out, const char *a, const char *b, const char *c)
{
    const cJSON *item = json_get_any(obj, a, b, c);
    if (cJSON_IsNumber(item)) {
        *out = item->valuedouble;
        return true;
    }
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        char *end = NULL;
        const double v = strtod(item->valuestring, &end);
        if (end != item->valuestring) {
            *out = v;
            return true;
        }
    }
    return false;
}

static bool json_int_any(const cJSON *obj, int *out, const char *a, const char *b, const char *c)
{
    double v = 0.0;
    if (!json_number_any(obj, &v, a, b, c)) {
        return false;
    }
    *out = (int)v;
    return true;
}

static faculty175_chart_role_t parse_role(const char *role)
{
    if (role == NULL) {
        return FACULTY175_CHART_ROLE_PARTNER;
    }
    if (strcasecmp(role, "self") == 0 || strcasecmp(role, "primary") == 0 || strcasecmp(role, "me") == 0) {
        return FACULTY175_CHART_ROLE_SELF;
    }
    if (strcasecmp(role, "child") == 0 || strcasecmp(role, "kid") == 0) {
        return FACULTY175_CHART_ROLE_CHILD;
    }
    return FACULTY175_CHART_ROLE_PARTNER;
}

static bool parse_date_string(const char *s, int *year, int *month, int *day)
{
    return s != NULL && sscanf(s, "%d-%d-%d", year, month, day) == 3;
}

static bool parse_time_string(const char *s, int *hour, int *minute)
{
    return s != NULL && sscanf(s, "%d:%d", hour, minute) >= 2;
}

static const cJSON *chart_birth_object(const cJSON *src)
{
    const cJSON *birth = json_get_any(src, "birth", "birth_chart", "birthChart");
    return cJSON_IsObject(birth) ? birth : src;
}

static bool chart_from_json(const cJSON *src, faculty175_birth_chart_t *out)
{
    if (!cJSON_IsObject(src) || out == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    copy_json_string(json_get_any(src, "name", "display_name", "displayName"), out->name, sizeof(out->name));
    if (out->name[0] == '\0') {
        copy_json_string(json_get_any(src, "title", "id", "slug"), out->name, sizeof(out->name));
    }
    char role[24] = "";
    copy_json_string(json_get_any(src, "role", "relationship", "family_role"), role, sizeof(role));
    out->role = parse_role(role);

    const cJSON *birth = chart_birth_object(src);
    int y = 0;
    int mo = 0;
    int d = 0;
    int h = 12;
    int mi = 0;
    const cJSON *date = json_get_any(birth, "date", "birth_date", "birthDate");
    if (!parse_date_string(cJSON_IsString(date) ? date->valuestring : NULL, &y, &mo, &d)) {
        (void)json_int_any(birth, &y, "year", "birth_year", "birthYear");
        (void)json_int_any(birth, &mo, "month", "birth_month", "birthMonth");
        (void)json_int_any(birth, &d, "day", "birth_day", "birthDay");
    }
    const cJSON *time = json_get_any(birth, "time", "birth_time", "birthTime");
    if (!parse_time_string(cJSON_IsString(time) ? time->valuestring : NULL, &h, &mi)) {
        (void)json_int_any(birth, &h, "hour", "birth_hour", "birthHour");
        (void)json_int_any(birth, &mi, "minute", "birth_minute", "birthMinute");
    }
    out->year = (uint16_t)y;
    out->month = (uint8_t)mo;
    out->day = (uint8_t)d;
    out->hour = (uint8_t)h;
    out->minute = (uint8_t)mi;

    double lat = 0.0;
    double lon = 0.0;
    double tz = 0.0;
    (void)json_number_any(birth, &lat, "lat_deg", "lat", "latitude");
    (void)json_number_any(birth, &lon, "lon_deg", "lon", "longitude");
    if (json_number_any(birth, &tz, "tz_offset_sec", "tzOffsetSec", "utc_offset_sec")) {
        out->tz_offset_sec = (int32_t)tz;
    } else if (json_number_any(birth, &tz, "tz_offset_hours", "tzOffsetHours", "utc_offset_hours")) {
        out->tz_offset_sec = (int32_t)(tz * 3600.0);
    }
    out->lat_deg = (float)lat;
    out->lon_deg = (float)lon;
    copy_json_string(json_get_any(birth, "place", "location", "birthplace"), out->place, sizeof(out->place));
    if (out->place[0] == '\0') {
        copy_json_string(cJSON_GetObjectItemCaseSensitive(birth, "birth_place"), out->place, sizeof(out->place));
    }
    if (out->place[0] == '\0') {
        copy_json_string(json_get_any(src, "place", "location", "birthplace"), out->place, sizeof(out->place));
    }
    if (out->place[0] == '\0') {
        copy_json_string(cJSON_GetObjectItemCaseSensitive(src, "birth_place"), out->place, sizeof(out->place));
    }
    out->valid = true;
    return chart_sane(out);
}

static void charts_repo_individual(char *out, size_t cap)
{
    if (out == NULL || cap == 0) {
        return;
    }
    out[0] = '\0';
    nvs_handle_t nvs;
    if (nvs_open(CASTALIA_NVS_NS, NVS_READONLY, &nvs) == ESP_OK) {
        size_t len = cap;
        (void)nvs_get_str(nvs, CASTALIA_KEY_INDIVIDUAL, out, &len);
        nvs_close(nvs);
    }
    if (out[0] == '\0') {
        normalize_individual(MYNAH_CASTALIA_INDIVIDUAL_DEFAULT, out, cap);
    }
}

void faculty175_charts_family_repo(char *out, size_t out_cap)
{
    if (out == NULL || out_cap == 0) {
        return;
    }
    char individual[48];
    charts_repo_individual(individual, sizeof(individual));
    if (individual[0] == '\0') {
        out[0] = '\0';
        return;
    }
    snprintf(out, out_cap, "%s/%s%s", MYNAH_CASTALIA_REPO_OWNER, MYNAH_CASTALIA_REPO_PREFIX, individual);
}

static bool charts_set_repo_individual(const char *raw)
{
    char individual[48];
    normalize_individual(raw, individual, sizeof(individual));
    if (individual[0] == '\0') {
        return false;
    }
    nvs_handle_t nvs;
    if (nvs_open(CASTALIA_NVS_NS, NVS_READWRITE, &nvs) != ESP_OK) {
        return false;
    }
    esp_err_t err = nvs_set_str(nvs, CASTALIA_KEY_INDIVIDUAL, individual);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    if (err != ESP_OK) {
        return false;
    }
    if (nvs_open(CHARTS_NVS_NS, NVS_READWRITE, &nvs) == ESP_OK) {
        (void)nvs_erase_key(nvs, CHARTS_KEY_REPO_SYNC);
        (void)nvs_erase_key(nvs, CHARTS_KEY_REPO_PATH);
        (void)nvs_erase_key(nvs, CHARTS_KEY_SEEDED);
        (void)nvs_commit(nvs);
        nvs_close(nvs);
    }
    return true;
}

static void charts_apply_castalia_headers(esp_http_client_handle_t client)
{
    if (client == NULL) {
        return;
    }
    (void)faculty175_device_auth_headers(client);
    if (strlen(MYNAH_SUPABASE_ANON_KEY) > 0) {
        esp_http_client_set_header(client, "apikey", MYNAH_SUPABASE_ANON_KEY);
        char auth[256];
        snprintf(auth, sizeof(auth), "Bearer %s", MYNAH_SUPABASE_ANON_KEY);
        esp_http_client_set_header(client, "Authorization", auth);
    }
}

static esp_err_t fetch_text_url(const char *url, bool castalia_auth, bool github_auth, char **out_body, size_t *out_len)
{
    if (url == NULL || out_body == NULL || out_len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_body = NULL;
    *out_len = 0;
    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = CHARTS_HTTP_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .keep_alive_enable = false,
        .buffer_size = CHARTS_HTTP_IO_BYTES,
        .buffer_size_tx = 2048,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        return ESP_ERR_NO_MEM;
    }
    esp_http_client_set_header(client, "User-Agent", "Astrolabe-Faculty175-Charts/1");
    esp_http_client_set_header(client, "Accept", "application/json,*/*;q=0.8");
    if (castalia_auth) {
        charts_apply_castalia_headers(client);
    }
    if (github_auth && MYNAH_CASTALIA_GITHUB_TOKEN[0] != '\0') {
        char auth[256];
        snprintf(auth, sizeof(auth), "Bearer %s", MYNAH_CASTALIA_GITHUB_TOKEN);
        esp_http_client_set_header(client, "Authorization", auth);
    }
    char *body = NULL;
    uint8_t *buf = NULL;
    size_t cap = 4096;
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
        body = (char *)heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (body == NULL) {
            body = (char *)malloc(cap);
        }
        buf = (uint8_t *)malloc(CHARTS_HTTP_IO_BYTES);
        if (body == NULL || buf == NULL) {
            err = ESP_ERR_NO_MEM;
        }
    }
    while (err == ESP_OK) {
        const int n = esp_http_client_read(client, (char *)buf, CHARTS_HTTP_IO_BYTES);
        if (n < 0) {
            err = ESP_FAIL;
            break;
        }
        if (n == 0) {
            break;
        }
        if (total + (size_t)n > CHARTS_HTTP_MAX_BYTES) {
            err = ESP_ERR_INVALID_SIZE;
            break;
        }
        if (total + (size_t)n + 1 > cap) {
            size_t next_cap = cap * 2;
            while (next_cap < total + (size_t)n + 1) {
                next_cap *= 2;
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

static void clear_profiles(void)
{
    nvs_handle_t nvs;
    if (nvs_open(CHARTS_NVS_NS, NVS_READWRITE, &nvs) != ESP_OK) {
        return;
    }
    for (int i = 0; i < FACULTY175_CHART_PROFILE_SLOTS; ++i) {
        char key[16];
        profile_key(i, key, sizeof(key));
        (void)nvs_erase_key(nvs, key);
    }
    (void)nvs_set_i32(nvs, CHARTS_KEY_ACTIVE, -1);
    (void)nvs_commit(nvs);
    nvs_close(nvs);
}

static bool parse_family_json(const char *body, size_t len, faculty175_birth_chart_t *primary,
                              faculty175_birth_chart_t *profiles, int *profile_count)
{
    if (body == NULL || primary == NULL || profiles == NULL || profile_count == NULL) {
        return false;
    }
    *profile_count = 0;
    memset(primary, 0, sizeof(*primary));
    cJSON *root = cJSON_ParseWithLength(body, len);
    if (root == NULL) {
        return false;
    }
    bool have_primary = false;
    const cJSON *guardians = cJSON_IsObject(root) ? cJSON_GetObjectItemCaseSensitive(root, "guardians") : NULL;
    const cJSON *children = cJSON_IsObject(root) ? cJSON_GetObjectItemCaseSensitive(root, "children") : NULL;
    if (cJSON_IsArray(guardians)) {
        const cJSON *person = NULL;
        cJSON_ArrayForEach(person, guardians) {
            faculty175_birth_chart_t chart = {};
            if (!chart_from_json(person, &chart)) {
                continue;
            }
            if (!have_primary) {
                chart.role = FACULTY175_CHART_ROLE_SELF;
                *primary = chart;
                have_primary = true;
            } else if (*profile_count < FACULTY175_CHART_PROFILE_SLOTS) {
                chart.role = FACULTY175_CHART_ROLE_PARTNER;
                profiles[(*profile_count)++] = chart;
            }
        }
    }
    if (cJSON_IsArray(children)) {
        const cJSON *person = NULL;
        cJSON_ArrayForEach(person, children) {
            faculty175_birth_chart_t chart = {};
            if (chart_from_json(person, &chart) && *profile_count < FACULTY175_CHART_PROFILE_SLOTS) {
                chart.role = FACULTY175_CHART_ROLE_CHILD;
                profiles[(*profile_count)++] = chart;
            }
        }
    }
    const cJSON *primary_obj = NULL;
    if (!have_primary && cJSON_IsObject(root)) {
        primary_obj = json_get_any(root, "primary", "self", "user");
        if (primary_obj == NULL) {
            primary_obj = cJSON_GetObjectItemCaseSensitive(root, "owner");
        }
        have_primary = chart_from_json(primary_obj, primary);
    }
    const cJSON *arr = cJSON_IsArray(root) ? root : NULL;
    if (arr == NULL && cJSON_IsObject(root)) {
        arr = json_get_any(root, "profiles", "family", "people");
        if (arr == NULL) {
            arr = cJSON_GetObjectItemCaseSensitive(root, "charts");
        }
    }
    if (*profile_count == 0 && cJSON_IsArray(arr)) {
        const cJSON *item = NULL;
        cJSON_ArrayForEach(item, arr) {
            faculty175_birth_chart_t chart = {};
            if (!chart_from_json(item, &chart)) {
                continue;
            }
            if (!have_primary && chart.role == FACULTY175_CHART_ROLE_SELF) {
                *primary = chart;
                have_primary = true;
                continue;
            }
            if (*profile_count < FACULTY175_CHART_PROFILE_SLOTS) {
                if (chart.role == FACULTY175_CHART_ROLE_SELF) {
                    chart.role = FACULTY175_CHART_ROLE_PARTNER;
                }
                profiles[(*profile_count)++] = chart;
            }
        }
    } else if (!have_primary && cJSON_IsObject(root)) {
        have_primary = chart_from_json(root, primary);
    }
    cJSON_Delete(root);
    return have_primary || *profile_count > 0;
}

static bool apply_family_json(const char *body, size_t len, const char *path)
{
    faculty175_birth_chart_t primary = {};
    faculty175_birth_chart_t profiles[FACULTY175_CHART_PROFILE_SLOTS] = {};
    int n_profiles = 0;
    if (!parse_family_json(body, len, &primary, profiles, &n_profiles)) {
        return false;
    }
    if (chart_sane(&primary)) {
        (void)faculty175_charts_save_primary(&primary);
    }
    clear_profiles();
    for (int i = 0; i < n_profiles; ++i) {
        (void)faculty175_charts_profile_save(i, &profiles[i]);
    }
    if (n_profiles > 0) {
        (void)faculty175_charts_set_active_slot(0);
    }
    nvs_handle_t nvs;
    if (nvs_open(CHARTS_NVS_NS, NVS_READWRITE, &nvs) == ESP_OK) {
        (void)nvs_set_u8(nvs, CHARTS_KEY_REPO_SYNC, 1);
        if (path != NULL) {
            (void)nvs_set_str(nvs, CHARTS_KEY_REPO_PATH, path);
        }
        (void)nvs_commit(nvs);
        nvs_close(nvs);
    }
    ESP_LOGI(TAG, "synced charts from Castalia repo path=%s profiles=%d", path != NULL ? path : "-", n_profiles);
    return true;
}

static bool build_castalia_family_url(const char *repo, const char *path, char *url, size_t cap)
{
    if (repo == NULL || path == NULL || url == NULL || cap == 0) {
        return false;
    }
    char base[192] = "";
    if (MYNAH_CASTALIA_FAMILY_REPO_URL[0] != '\0') {
        strncpy(base, MYNAH_CASTALIA_FAMILY_REPO_URL, sizeof(base) - 1);
    } else if (strlen(MYNAH_SUPABASE_URL) > 0) {
        strncpy(base, MYNAH_SUPABASE_URL, sizeof(base) - 1);
        base[sizeof(base) - 1] = '\0';
        trim_url_slash(base);
        strlcat(base, "/functions/v1/mynah-family-repo", sizeof(base));
    } else {
        return false;
    }
    trim_url_slash(base);
    char enc_repo[160];
    char enc_path[96];
    url_encode_component(repo, enc_repo, sizeof(enc_repo));
    url_encode_component(path, enc_path, sizeof(enc_path));
    const int n = snprintf(url, cap, "%s?repo=%s&path=%s", base, enc_repo, enc_path);
    return n > 0 && (size_t)n < cap;
}

static bool fetch_family_path_via_castalia(const char *repo, const char *path)
{
    char url[360] = "";
    if (!build_castalia_family_url(repo, path, url, sizeof(url))) {
        return false;
    }
    char *body = NULL;
    size_t len = 0;
    if (fetch_text_url(url, true, false, &body, &len) != ESP_OK) {
        return false;
    }
    const bool ok = apply_family_json(body, len, path);
    free(body);
    return ok;
}

static bool fetch_family_path_via_raw_github(const char *owner, const char *name, const char *path)
{
    char url[256];
    snprintf(url, sizeof(url), "https://raw.githubusercontent.com/%s/%s/main/%s", owner, name, path);
    char *body = NULL;
    size_t len = 0;
    if (fetch_text_url(url, false, true, &body, &len) != ESP_OK) {
        return false;
    }
    const bool ok = apply_family_json(body, len, path);
    free(body);
    return ok;
}

bool faculty175_charts_sync_family_repo(void)
{
    char repo[112];
    faculty175_charts_family_repo(repo, sizeof(repo));
    const char *slash = strchr(repo, '/');
    if (slash == NULL || slash[1] == '\0') {
        return false;
    }
    char owner[56];
    char name[72];
    const size_t owner_len = (size_t)(slash - repo);
    if (owner_len >= sizeof(owner)) {
        return false;
    }
    memcpy(owner, repo, owner_len);
    owner[owner_len] = '\0';
    strncpy(name, slash + 1, sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';

    static const char *const k_paths[] = {
        "castalia-family.json",
        "settings/family.json",
        "settings/charts.json",
        "family.json",
        "charts.json",
        "settings/birth.json",
    };
    for (size_t i = 0; i < sizeof(k_paths) / sizeof(k_paths[0]); ++i) {
        if (fetch_family_path_via_castalia(repo, k_paths[i])) {
            return true;
        }
    }
    for (size_t i = 0; i < sizeof(k_paths) / sizeof(k_paths[0]); ++i) {
        if (fetch_family_path_via_raw_github(owner, name, k_paths[i])) {
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
        portENTER_CRITICAL(&s_chart_cache_mux);
        if (s_chart_cache_ready) {
            s_chart_cache_primary = *chart;
            s_chart_cache_primary_valid = true;
        }
        portEXIT_CRITICAL(&s_chart_cache_mux);
    }
    return err;
}

bool faculty175_charts_primary(faculty175_birth_chart_t *out)
{
    if (out == NULL) {
        return false;
    }
    portENTER_CRITICAL(&s_chart_cache_mux);
    const bool cached = s_chart_cache_ready;
    const bool valid = s_chart_cache_primary_valid;
    if (cached) {
        *out = s_chart_cache_primary;
    }
    portEXIT_CRITICAL(&s_chart_cache_mux);
    return cached ? valid : charts_read_primary_nvs(out);
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
        portENTER_CRITICAL(&s_chart_cache_mux);
        if (s_chart_cache_ready) {
            s_chart_cache_profiles[slot] = *chart;
            s_chart_cache_profile_valid[slot] = true;
        }
        portEXIT_CRITICAL(&s_chart_cache_mux);
    }
    return err;
}

esp_err_t faculty175_charts_profile_clear(int slot)
{
    if (slot < 0 || slot >= FACULTY175_CHART_PROFILE_SLOTS) {
        return ESP_ERR_INVALID_ARG;
    }
    char key[16];
    profile_key(slot, key, sizeof(key));
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(CHARTS_NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_erase_key(nvs, key);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK;
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    if (err == ESP_OK) {
        portENTER_CRITICAL(&s_chart_cache_mux);
        if (s_chart_cache_ready) {
            memset(&s_chart_cache_profiles[slot], 0, sizeof(s_chart_cache_profiles[slot]));
            s_chart_cache_profile_valid[slot] = false;
            if (s_chart_cache_active == slot) {
                s_chart_cache_active = -1;
            }
        }
        portEXIT_CRITICAL(&s_chart_cache_mux);
    }
    return err;
}

bool faculty175_charts_profile_get(int slot, faculty175_birth_chart_t *out)
{
    if (slot < 0 || slot >= FACULTY175_CHART_PROFILE_SLOTS || out == NULL) {
        return false;
    }
    portENTER_CRITICAL(&s_chart_cache_mux);
    const bool cached = s_chart_cache_ready;
    const bool valid = s_chart_cache_profile_valid[slot];
    if (cached) {
        *out = s_chart_cache_profiles[slot];
    }
    portEXIT_CRITICAL(&s_chart_cache_mux);
    return cached ? valid : charts_read_profile_nvs(slot, out);
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

void faculty175_charts_ensure_family_seed(void)
{
    portENTER_CRITICAL(&s_chart_cache_mux);
    const bool cache_ready = s_chart_cache_ready;
    portEXIT_CRITICAL(&s_chart_cache_mux);
    if (cache_ready) {
        return;
    }
    nvs_handle_t nvs;
    if (nvs_open(CHARTS_NVS_NS, NVS_READWRITE, &nvs) != ESP_OK) {
        return;
    }
    uint8_t seeded = 0;
    if (nvs_get_u8(nvs, CHARTS_KEY_SEEDED, &seeded) == ESP_OK && seeded != 0) {
        nvs_close(nvs);
        charts_cache_reload_from_nvs();
        return;
    }
    nvs_close(nvs);

    if (faculty175_charts_sync_family_repo()) {
        if (nvs_open(CHARTS_NVS_NS, NVS_READWRITE, &nvs) == ESP_OK) {
            (void)nvs_set_u8(nvs, CHARTS_KEY_SEEDED, 1);
            (void)nvs_commit(nvs);
            nvs_close(nvs);
        }
        charts_cache_reload_from_nvs();
        return;
    }

#if defined(ASTROLABE_FORCE_VARIANT_LUNASAY)
    /* A retail LunaSay must never inherit the developer family's names or birth
     * data from its firmware image. Existing NVS survives an app-only update;
     * a fresh device remains empty until the owner configures or syncs it. */
    if (nvs_open(CHARTS_NVS_NS, NVS_READWRITE, &nvs) == ESP_OK) {
        (void)nvs_set_u8(nvs, CHARTS_KEY_SEEDED, 1);
        (void)nvs_commit(nvs);
        nvs_close(nvs);
    }
    charts_cache_reload_from_nvs();
    ESP_LOGI(TAG, "LunaSay family charts await owner setup");
    return;
#else
    faculty175_birth_chart_t primary = {};
    if (!faculty175_charts_primary(&primary)) {
        (void)faculty175_charts_save_primary(&k_family_primary);
    }
    for (size_t i = 0; i < sizeof(k_family_profiles) / sizeof(k_family_profiles[0]) &&
                       i < FACULTY175_CHART_PROFILE_SLOTS;
         ++i) {
        faculty175_birth_chart_t existing = {};
        if (!faculty175_charts_profile_get((int)i, &existing)) {
            (void)faculty175_charts_profile_save((int)i, &k_family_profiles[i]);
        }
    }
    if (faculty175_charts_active_slot() < 0) {
        (void)faculty175_charts_set_active_slot(0);
    }

    if (nvs_open(CHARTS_NVS_NS, NVS_READWRITE, &nvs) == ESP_OK) {
        (void)nvs_set_u8(nvs, CHARTS_KEY_SEEDED, 1);
        (void)nvs_commit(nvs);
        nvs_close(nvs);
    }
    charts_cache_reload_from_nvs();
#endif
}

int faculty175_charts_active_slot(void)
{
    portENTER_CRITICAL(&s_chart_cache_mux);
    const bool cached = s_chart_cache_ready;
    const int cached_slot = s_chart_cache_active;
    portEXIT_CRITICAL(&s_chart_cache_mux);
    if (cached) {
        return cached_slot;
    }
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
    if (err == ESP_OK) {
        portENTER_CRITICAL(&s_chart_cache_mux);
        if (s_chart_cache_ready) {
            s_chart_cache_active = slot;
        }
        portEXIT_CRITICAL(&s_chart_cache_mux);
    }
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

bool faculty175_charts_positions_at(time_t epoch, faculty175_chart_positions_t *out)
{
    if (out == NULL || epoch <= 0) {
        return false;
    }
    struct tm utc = {};
    gmtime_r(&epoch, &utc);
    compute_utc(&utc, out);
    return out->ok;
}

bool faculty175_charts_birth_positions(const faculty175_birth_chart_t *birth, faculty175_chart_positions_t *out)
{
    if (!chart_sane(birth) || out == NULL) {
        return false;
    }
    time_t epoch = 0;
    if (!faculty175_charts_birth_to_utc(birth, &epoch)) {
        return false;
    }
    struct tm utc = {};
    gmtime_r(&epoch, &utc);
    compute_utc(&utc, out);
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
        char repo[112];
        char path[48] = "";
        uint8_t synced = 0;
        faculty175_charts_family_repo(repo, sizeof(repo));
        nvs_handle_t nvs;
        if (nvs_open(CHARTS_NVS_NS, NVS_READONLY, &nvs) == ESP_OK) {
            size_t path_len = sizeof(path);
            (void)nvs_get_str(nvs, CHARTS_KEY_REPO_PATH, path, &path_len);
            (void)nvs_get_u8(nvs, CHARTS_KEY_REPO_SYNC, &synced);
            nvs_close(nvs);
        }
        printf("charts: primary=%s profiles=%d active=%d repo=%s source=%s%s%s\n",
               faculty175_charts_primary(&primary) ? primary.name : "-", faculty175_charts_profile_count(),
               faculty175_charts_active_slot(), repo, synced ? "castalia" : "fallback", path[0] ? " path=" : "",
               path);
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
    if (strncasecmp(sub, "repo ", 5) == 0 || strncasecmp(sub, "individual ", 11) == 0 ||
        strncasecmp(sub, "profile ", 8) == 0) {
        const char *arg = strchr(sub, ' ');
        arg = arg != NULL ? arg + 1 : "";
        if (charts_set_repo_individual(arg)) {
            char repo[112];
            faculty175_charts_family_repo(repo, sizeof(repo));
            printf("charts: saved repo=%s\n", repo);
        } else {
            printf("charts: usage: charts repo CastaliaInstitute/castalia-DanielCMcShan\n");
        }
        fflush(stdout);
        return true;
    }
    if (strcasecmp(sub, "sync") == 0 || strcasecmp(sub, "sync castalia") == 0 ||
        strcasecmp(sub, "sync repo") == 0) {
        char repo[112];
        faculty175_charts_family_repo(repo, sizeof(repo));
        printf("charts: syncing repo=%s\n", repo);
        const bool ok = faculty175_charts_sync_family_repo();
        faculty175_birth_chart_t primary = {};
        printf("charts: sync %s primary=%s profiles=%d active=%d\n", ok ? "ok" : "failed",
               faculty175_charts_primary(&primary) ? primary.name : "-", faculty175_charts_profile_count(),
               faculty175_charts_active_slot());
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
        printf("charts commands:\n  charts status\n  charts repo <owner/repo|individual>\n  charts sync\n  charts seed\n  charts next\n  charts prev\n");
        fflush(stdout);
        return true;
    }
    printf("charts: unknown command\n");
    fflush(stdout);
    return true;
}
