#include "faculty175_almanac.h"

#include <ctype.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

#include "faculty175_log.h"

static const char *TAG = "faculty175_almanac";

#define ALMANAC_NVS_NS "almanac"
#define ALMANAC_NVS_URL "url"
#define ALMANAC_NVS_VERSION "version"
#define ALMANAC_DEFAULT_MANIFEST_URL "https://almanac.castalia.institute/manifest.json"
#define ALMANAC_URL_MAX 256
#define ALMANAC_LAST_MAX 160
#define ALMANAC_MANIFEST_MAX_BYTES (24 * 1024)
#define ALMANAC_DATASET_MAX_BYTES (384 * 1024)
#define ALMANAC_IO_BUFFER_BYTES 2048
#define ALMANAC_STORAGE_BASE "/bust_cache"
#define ALMANAC_MANIFEST_CACHE_PATH ALMANAC_STORAGE_BASE "/alm-man.json"
#define ALMANAC_AUTO_INITIAL_DELAY_MS 35000
#define ALMANAC_AUTO_POLL_MS (6 * 60 * 60 * 1000)

typedef enum {
    ALMANAC_STATE_IDLE = 0,
    ALMANAC_STATE_RUNNING,
    ALMANAC_STATE_DONE,
    ALMANAC_STATE_ERROR,
} almanac_state_t;

static volatile almanac_state_t s_state;
static char s_manifest_url[ALMANAC_URL_MAX];
static char s_last[ALMANAC_LAST_MAX];
static bool s_auto_started;
static bool s_daily_cache_valid;
static char s_daily_date[24];
static char s_daily_season[40];
static char s_daily_moon[40];
static char s_daily_sun[32];
static char s_daily_event[64];
static char s_daily_planting[72];
static char s_daily_prompt[144];
static char s_daily_phenology_subject[64];
static char s_daily_phenology_action[96];
static char s_daily_phenology_habitat[64];
static char s_daily_phenology_prompt[160];
static char s_daily_phenology_image[96];

static void set_last(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s_last, sizeof(s_last), fmt, ap);
    va_end(ap);
}

static const char *state_name(void)
{
    switch (s_state) {
        case ALMANAC_STATE_RUNNING:
            return "running";
        case ALMANAC_STATE_DONE:
            return "done";
        case ALMANAC_STATE_ERROR:
            return "error";
        case ALMANAC_STATE_IDLE:
        default:
            return "idle";
    }
}

static bool is_http_url(const char *url)
{
    return url != NULL && (strncmp(url, "https://", 8) == 0 || strncmp(url, "http://", 7) == 0);
}

static bool safe_cache_name(const char *name)
{
    if (name == NULL || name[0] == '\0' || strlen(name) > 64) {
        return false;
    }
    for (const char *p = name; *p != '\0'; ++p) {
        const unsigned char ch = (unsigned char)*p;
        if (!isalnum(ch) && *p != '-' && *p != '_' && *p != '.') {
            return false;
        }
    }
    return true;
}

static esp_err_t load_url_from_nvs(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(ALMANAC_NVS_NS, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        strlcpy(s_manifest_url, ALMANAC_DEFAULT_MANIFEST_URL, sizeof(s_manifest_url));
        return ESP_OK;
    }
    size_t len = sizeof(s_manifest_url);
    err = nvs_get_str(nvs, ALMANAC_NVS_URL, s_manifest_url, &len);
    nvs_close(nvs);
    if (err == ESP_ERR_NVS_NOT_FOUND || !is_http_url(s_manifest_url)) {
        strlcpy(s_manifest_url, ALMANAC_DEFAULT_MANIFEST_URL, sizeof(s_manifest_url));
        return ESP_OK;
    }
    return err;
}

static esp_err_t save_url_to_nvs(const char *url)
{
    if (!is_http_url(url) || strlen(url) >= ALMANAC_URL_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(ALMANAC_NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_str(nvs, ALMANAC_NVS_URL, url);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    if (err == ESP_OK) {
        strlcpy(s_manifest_url, url, sizeof(s_manifest_url));
    }
    return err;
}

static void save_version_to_nvs(const char *version)
{
    if (version == NULL || version[0] == '\0') {
        return;
    }
    nvs_handle_t nvs;
    if (nvs_open(ALMANAC_NVS_NS, NVS_READWRITE, &nvs) == ESP_OK) {
        (void)nvs_set_str(nvs, ALMANAC_NVS_VERSION, version);
        (void)nvs_commit(nvs);
        nvs_close(nvs);
    }
}

static void load_version_from_nvs(char *out, size_t cap)
{
    if (out == NULL || cap == 0) {
        return;
    }
    out[0] = '\0';
    nvs_handle_t nvs;
    if (nvs_open(ALMANAC_NVS_NS, NVS_READONLY, &nvs) == ESP_OK) {
        size_t len = cap;
        (void)nvs_get_str(nvs, ALMANAC_NVS_VERSION, out, &len);
        nvs_close(nvs);
    }
}

static esp_err_t http_download_to_file(const char *url, const char *path, size_t max_bytes, size_t *out_bytes)
{
    if (!is_http_url(url) || path == NULL || max_bytes == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = 15000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .keep_alive_enable = false,
        .buffer_size = ALMANAC_IO_BUFFER_BYTES,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_http_client_set_header(client, "User-Agent", "Astrolabe-Faculty175-Almanac/1");
    esp_http_client_set_header(client, "Accept", "application/json,*/*;q=0.8");

    FILE *f = NULL;
    esp_err_t err = esp_http_client_open(client, 0);
    int status = 0;
    size_t total = 0;
    if (err == ESP_OK) {
        (void)esp_http_client_fetch_headers(client);
        status = esp_http_client_get_status_code(client);
        if (status != 200) {
            err = ESP_FAIL;
            set_last("HTTP %d %s", status, url);
        }
    }
    if (err == ESP_OK) {
        f = fopen(path, "wb");
        if (f == NULL) {
            err = ESP_ERR_NO_MEM;
            set_last("open failed %s", path);
        }
    }
    uint8_t *buf = NULL;
    if (err == ESP_OK) {
        buf = malloc(ALMANAC_IO_BUFFER_BYTES);
        if (buf == NULL) {
            err = ESP_ERR_NO_MEM;
        }
    }
    while (err == ESP_OK) {
        const int n = esp_http_client_read(client, (char *)buf, ALMANAC_IO_BUFFER_BYTES);
        if (n < 0) {
            err = ESP_FAIL;
            set_last("read failed %s", url);
            break;
        }
        if (n == 0) {
            break;
        }
        total += (size_t)n;
        if (total > max_bytes) {
            err = ESP_ERR_INVALID_SIZE;
            set_last("too large %s", url);
            break;
        }
        if (fwrite(buf, 1, (size_t)n, f) != (size_t)n) {
            err = ESP_FAIL;
            set_last("write failed %s", path);
            break;
        }
    }

    free(buf);
    if (f != NULL) {
        fclose(f);
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        remove(path);
    } else if (out_bytes != NULL) {
        *out_bytes = total;
    }
    return err;
}

static char *read_file_alloc(const char *path, size_t max_bytes, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    const long n = ftell(f);
    if (n < 0 || (size_t)n > max_bytes) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    char *body = malloc((size_t)n + 1u);
    if (body == NULL) {
        fclose(f);
        return NULL;
    }
    const size_t read = fread(body, 1, (size_t)n, f);
    fclose(f);
    body[read] = '\0';
    if (out_len != NULL) {
        *out_len = read;
    }
    return body;
}

static bool resolve_dataset_url(const char *base_url, const char *path, char *out, size_t cap)
{
    if (path == NULL || out == NULL || cap == 0) {
        return false;
    }
    if (is_http_url(path)) {
        return snprintf(out, cap, "%s", path) > 0 && strlen(out) < cap;
    }
    const char *base = is_http_url(base_url) ? base_url : s_manifest_url;
    const char *slash = strrchr(base, '/');
    const size_t base_len = slash != NULL ? (size_t)(slash + 1 - base) : strlen(base);
    if (base_len + strlen(path) + 1 > cap) {
        return false;
    }
    memcpy(out, base, base_len);
    strcpy(out + base_len, path);
    return true;
}

static esp_err_t fetch_manifest_and_datasets(void)
{
    size_t manifest_bytes = 0;
    esp_err_t err = http_download_to_file(s_manifest_url,
                                          ALMANAC_MANIFEST_CACHE_PATH,
                                          ALMANAC_MANIFEST_MAX_BYTES,
                                          &manifest_bytes);
    if (err != ESP_OK) {
        return err;
    }

    size_t body_len = 0;
    char *body = read_file_alloc(ALMANAC_MANIFEST_CACHE_PATH, ALMANAC_MANIFEST_MAX_BYTES, &body_len);
    if (body == NULL) {
        set_last("manifest cache read failed");
        return ESP_FAIL;
    }

    cJSON *root = cJSON_ParseWithLength(body, body_len);
    free(body);
    if (root == NULL) {
        set_last("manifest json invalid");
        return ESP_ERR_INVALID_RESPONSE;
    }

    const cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "version");
    const cJSON *base_url = cJSON_GetObjectItemCaseSensitive(root, "base_url");
    const cJSON *datasets = cJSON_GetObjectItemCaseSensitive(root, "datasets");
    if (!cJSON_IsArray(datasets)) {
        cJSON_Delete(root);
        set_last("manifest missing datasets");
        return ESP_ERR_INVALID_ARG;
    }

    int count = 0;
    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, datasets) {
        const cJSON *id = cJSON_GetObjectItemCaseSensitive(item, "id");
        const cJSON *path = cJSON_GetObjectItemCaseSensitive(item, "path");
        const cJSON *cache_path = cJSON_GetObjectItemCaseSensitive(item, "cache_path");
        if (!cJSON_IsString(id) || !cJSON_IsString(path) || !cJSON_IsString(cache_path) ||
            !safe_cache_name(cache_path->valuestring)) {
            cJSON_Delete(root);
            set_last("manifest invalid dataset");
            return ESP_ERR_INVALID_ARG;
        }

        char url[ALMANAC_URL_MAX];
        if (!resolve_dataset_url(cJSON_IsString(base_url) ? base_url->valuestring : NULL,
                                 path->valuestring,
                                 url,
                                 sizeof(url))) {
            cJSON_Delete(root);
            set_last("dataset url too long");
            return ESP_ERR_INVALID_SIZE;
        }

        char local[96];
        if (snprintf(local, sizeof(local), ALMANAC_STORAGE_BASE "/%s", cache_path->valuestring) <= 0 ||
            strlen(local) >= sizeof(local)) {
            cJSON_Delete(root);
            set_last("dataset cache path too long");
            return ESP_ERR_INVALID_SIZE;
        }

        size_t bytes = 0;
        err = http_download_to_file(url, local, ALMANAC_DATASET_MAX_BYTES, &bytes);
        if (err != ESP_OK) {
            cJSON_Delete(root);
            return err;
        }
        FACULTY175_LOG_STAGE(TAG, "almanac", "dataset %s %u B", id->valuestring, (unsigned)bytes);
        ++count;
    }

    if (cJSON_IsString(version)) {
        save_version_to_nvs(version->valuestring);
    }
    s_daily_cache_valid = false;
    set_last("ok version=%s datasets=%d manifest=%uB",
             cJSON_IsString(version) ? version->valuestring : "-",
             count,
             (unsigned)manifest_bytes);
    cJSON_Delete(root);
    return ESP_OK;
}

static void fetch_task(void *arg)
{
    const bool auto_loop = arg != NULL;
    if (auto_loop) {
        vTaskDelay(pdMS_TO_TICKS(ALMANAC_AUTO_INITIAL_DELAY_MS));
    }
    do {
        if (s_state != ALMANAC_STATE_RUNNING) {
            s_state = ALMANAC_STATE_RUNNING;
            FACULTY175_LOG_STAGE(TAG, "almanac", "fetch %s", s_manifest_url);
            const esp_err_t err = fetch_manifest_and_datasets();
            s_state = err == ESP_OK ? ALMANAC_STATE_DONE : ALMANAC_STATE_ERROR;
            if (err != ESP_OK && s_last[0] == '\0') {
                set_last("%s", esp_err_to_name(err));
            }
            FACULTY175_LOG_STAGE(TAG, "almanac", "%s: %s", state_name(), s_last);
        }
        if (!auto_loop) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(ALMANAC_AUTO_POLL_MS));
    } while (true);
    vTaskDelete(NULL);
}

esp_err_t faculty175_almanac_init(void)
{
    s_state = ALMANAC_STATE_IDLE;
    set_last("idle");
    return load_url_from_nvs();
}

void faculty175_almanac_start_auto_fetch_task(void)
{
    if (s_auto_started) {
        return;
    }
    s_auto_started = true;
    xTaskCreate(fetch_task, "almanac_auto", 8192, (void *)1, 3, NULL);
}

bool faculty175_almanac_active(void)
{
    return s_state == ALMANAC_STATE_RUNNING;
}

const char *faculty175_almanac_state_name(void)
{
    return state_name();
}

const char *faculty175_almanac_last(void)
{
    return s_last;
}

const char *faculty175_almanac_manifest_url(void)
{
    return s_manifest_url;
}

static void copy_json_string(const cJSON *item, char *out, size_t cap)
{
    if (out == NULL || cap == 0) {
        return;
    }
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        strlcpy(out, item->valuestring, cap);
    } else {
        out[0] = '\0';
    }
}

bool faculty175_almanac_cached_daily_ex(char *date,
                                        size_t date_cap,
                                        char *season,
                                        size_t season_cap,
                                        char *moon,
                                        size_t moon_cap,
                                        char *sun,
                                        size_t sun_cap,
                                        char *event,
                                        size_t event_cap,
                                        char *planting,
                                        size_t planting_cap,
                                        char *prompt,
                                        size_t prompt_cap)
{
    if (date != NULL && date_cap > 0) {
        date[0] = '\0';
    }
    if (season != NULL && season_cap > 0) {
        season[0] = '\0';
    }
    if (moon != NULL && moon_cap > 0) {
        moon[0] = '\0';
    }
    if (prompt != NULL && prompt_cap > 0) {
        prompt[0] = '\0';
    }
    if (sun != NULL && sun_cap > 0) {
        sun[0] = '\0';
    }
    if (event != NULL && event_cap > 0) {
        event[0] = '\0';
    }
    if (planting != NULL && planting_cap > 0) {
        planting[0] = '\0';
    }

    if (s_daily_cache_valid) {
        if (date != NULL && date_cap > 0) {
            strlcpy(date, s_daily_date, date_cap);
        }
        if (season != NULL && season_cap > 0) {
            strlcpy(season, s_daily_season, season_cap);
        }
        if (moon != NULL && moon_cap > 0) {
            strlcpy(moon, s_daily_moon, moon_cap);
        }
        if (sun != NULL && sun_cap > 0) {
            strlcpy(sun, s_daily_sun, sun_cap);
        }
        if (event != NULL && event_cap > 0) {
            strlcpy(event, s_daily_event, event_cap);
        }
        if (planting != NULL && planting_cap > 0) {
            strlcpy(planting, s_daily_planting, planting_cap);
        }
        if (prompt != NULL && prompt_cap > 0) {
            strlcpy(prompt, s_daily_prompt, prompt_cap);
        }
        return s_daily_date[0] != '\0';
    }

    size_t len = 0;
    char *body = read_file_alloc(ALMANAC_STORAGE_BASE "/alm-daily.json", ALMANAC_DATASET_MAX_BYTES, &len);
    if (body == NULL) {
        return false;
    }

    cJSON *root = cJSON_ParseWithLength(body, len);
    free(body);
    if (root == NULL) {
        return false;
    }

    const cJSON *entries = cJSON_GetObjectItemCaseSensitive(root, "entries");
    if (!cJSON_IsArray(entries) || cJSON_GetArraySize(entries) == 0) {
        cJSON_Delete(root);
        return false;
    }

    char today[16] = {0};
    const time_t now = time(NULL);
    if (now > 1700000000) {
        struct tm tm_now;
        gmtime_r(&now, &tm_now);
        snprintf(today, sizeof(today), "%04d-%02d-%02d", tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday);
    }

    const cJSON *chosen = cJSON_GetArrayItem(entries, 0);
    const cJSON *item = NULL;
    if (today[0] != '\0') {
        cJSON_ArrayForEach(item, entries) {
            const cJSON *entry_date = cJSON_GetObjectItemCaseSensitive(item, "date");
            if (cJSON_IsString(entry_date) && strcmp(entry_date->valuestring, today) == 0) {
                chosen = item;
                break;
            }
        }
    }

    copy_json_string(cJSON_GetObjectItemCaseSensitive(chosen, "date"), s_daily_date, sizeof(s_daily_date));
    copy_json_string(cJSON_GetObjectItemCaseSensitive(chosen, "season"), s_daily_season, sizeof(s_daily_season));
    copy_json_string(cJSON_GetObjectItemCaseSensitive(chosen, "moon_phase"), s_daily_moon, sizeof(s_daily_moon));
    const cJSON *sun_obj = cJSON_GetObjectItemCaseSensitive(chosen, "sun");
    copy_json_string(cJSON_GetObjectItemCaseSensitive(sun_obj, "sign"), s_daily_sun, sizeof(s_daily_sun));
    copy_json_string(cJSON_GetObjectItemCaseSensitive(chosen, "seasonal_marker"), s_daily_event, sizeof(s_daily_event));
    if (s_daily_event[0] == '\0') {
        copy_json_string(cJSON_GetObjectItemCaseSensitive(chosen, "astronomy_event"), s_daily_event, sizeof(s_daily_event));
    }
    if (s_daily_event[0] == '\0') {
        copy_json_string(cJSON_GetObjectItemCaseSensitive(chosen, "observance"), s_daily_event, sizeof(s_daily_event));
    }
    copy_json_string(cJSON_GetObjectItemCaseSensitive(chosen, "planting"), s_daily_planting, sizeof(s_daily_planting));
    copy_json_string(cJSON_GetObjectItemCaseSensitive(chosen, "prompt"), s_daily_prompt, sizeof(s_daily_prompt));
    const cJSON *phenology = cJSON_GetObjectItemCaseSensitive(chosen, "phenology");
    copy_json_string(cJSON_GetObjectItemCaseSensitive(phenology, "subject"),
                     s_daily_phenology_subject,
                     sizeof(s_daily_phenology_subject));
    if (s_daily_phenology_subject[0] == '\0') {
        copy_json_string(cJSON_GetObjectItemCaseSensitive(phenology, "plant"),
                         s_daily_phenology_subject,
                         sizeof(s_daily_phenology_subject));
    }
    if (s_daily_phenology_subject[0] == '\0') {
        copy_json_string(cJSON_GetObjectItemCaseSensitive(phenology, "animal"),
                         s_daily_phenology_subject,
                         sizeof(s_daily_phenology_subject));
    }
    copy_json_string(cJSON_GetObjectItemCaseSensitive(phenology, "action"),
                     s_daily_phenology_action,
                     sizeof(s_daily_phenology_action));
    copy_json_string(cJSON_GetObjectItemCaseSensitive(phenology, "habitat"),
                     s_daily_phenology_habitat,
                     sizeof(s_daily_phenology_habitat));
    copy_json_string(cJSON_GetObjectItemCaseSensitive(phenology, "prompt"),
                     s_daily_phenology_prompt,
                     sizeof(s_daily_phenology_prompt));
    copy_json_string(cJSON_GetObjectItemCaseSensitive(phenology, "image_cache_path"),
                     s_daily_phenology_image,
                     sizeof(s_daily_phenology_image));
    if (s_daily_phenology_image[0] == '\0') {
        copy_json_string(cJSON_GetObjectItemCaseSensitive(phenology, "rgb565_cache_path"),
                         s_daily_phenology_image,
                         sizeof(s_daily_phenology_image));
    }
    if (s_daily_phenology_subject[0] == '\0') {
        strlcpy(s_daily_phenology_subject, s_daily_season[0] != '\0' ? s_daily_season : "local phenology",
                sizeof(s_daily_phenology_subject));
    }
    if (s_daily_phenology_action[0] == '\0') {
        strlcpy(s_daily_phenology_action, s_daily_planting[0] != '\0' ? s_daily_planting : "seasonal change",
                sizeof(s_daily_phenology_action));
    }
    if (s_daily_phenology_prompt[0] == '\0') {
        strlcpy(s_daily_phenology_prompt, s_daily_prompt, sizeof(s_daily_phenology_prompt));
    }
    cJSON_Delete(root);
    s_daily_cache_valid = s_daily_date[0] != '\0';
    if (!s_daily_cache_valid) {
        return false;
    }
    return faculty175_almanac_cached_daily_ex(date,
                                             date_cap,
                                             season,
                                             season_cap,
                                             moon,
                                             moon_cap,
                                             sun,
                                             sun_cap,
                                             event,
                                             event_cap,
                                             planting,
                                             planting_cap,
                                             prompt,
                                             prompt_cap);
}

bool faculty175_almanac_cached_daily(char *date,
                                     size_t date_cap,
                                     char *season,
                                     size_t season_cap,
                                     char *moon,
                                     size_t moon_cap,
                                     char *prompt,
                                     size_t prompt_cap)
{
    return faculty175_almanac_cached_daily_ex(date, date_cap, season, season_cap, moon, moon_cap, NULL, 0, NULL, 0,
                                             NULL, 0, prompt, prompt_cap);
}

bool faculty175_almanac_cached_phenology(char *date,
                                         size_t date_cap,
                                         char *subject,
                                         size_t subject_cap,
                                         char *action,
                                         size_t action_cap,
                                         char *habitat,
                                         size_t habitat_cap,
                                         char *prompt,
                                         size_t prompt_cap,
                                         char *image_path,
                                         size_t image_path_cap)
{
    char season[40];
    char moon[40];
    char sun[32];
    char event[64];
    char planting[72];
    char daily_prompt[144];
    const bool ok = faculty175_almanac_cached_daily_ex(date,
                                                       date_cap,
                                                       season,
                                                       sizeof(season),
                                                       moon,
                                                       sizeof(moon),
                                                       sun,
                                                       sizeof(sun),
                                                       event,
                                                       sizeof(event),
                                                       planting,
                                                       sizeof(planting),
                                                       daily_prompt,
                                                       sizeof(daily_prompt));
    if (!ok) {
        if (subject != NULL && subject_cap > 0) {
            subject[0] = '\0';
        }
        if (action != NULL && action_cap > 0) {
            action[0] = '\0';
        }
        if (habitat != NULL && habitat_cap > 0) {
            habitat[0] = '\0';
        }
        if (prompt != NULL && prompt_cap > 0) {
            prompt[0] = '\0';
        }
        if (image_path != NULL && image_path_cap > 0) {
            image_path[0] = '\0';
        }
        return false;
    }
    if (subject != NULL && subject_cap > 0) {
        strlcpy(subject,
                s_daily_phenology_subject[0] != '\0' ? s_daily_phenology_subject : season,
                subject_cap);
    }
    if (action != NULL && action_cap > 0) {
        strlcpy(action,
                s_daily_phenology_action[0] != '\0' ? s_daily_phenology_action : planting,
                action_cap);
    }
    if (habitat != NULL && habitat_cap > 0) {
        strlcpy(habitat,
                s_daily_phenology_habitat[0] != '\0' ? s_daily_phenology_habitat : moon,
                habitat_cap);
    }
    if (prompt != NULL && prompt_cap > 0) {
        strlcpy(prompt,
                s_daily_phenology_prompt[0] != '\0' ? s_daily_phenology_prompt : daily_prompt,
                prompt_cap);
    }
    if (image_path != NULL && image_path_cap > 0) {
        if (s_daily_phenology_image[0] != '\0') {
            snprintf(image_path, image_path_cap, ALMANAC_STORAGE_BASE "/%s", s_daily_phenology_image);
        } else {
            strlcpy(image_path, ALMANAC_STORAGE_BASE "/alm-phenology.rgb565", image_path_cap);
        }
    }
    return true;
}

bool faculty175_almanac_handle(const char *line)
{
    if (line == NULL || (strcasecmp(line, "almanac") != 0 && strncasecmp(line, "almanac ", 8) != 0)) {
        return false;
    }

    const char *sub = strchr(line, ' ');
    sub = sub != NULL ? sub + 1 : "status";
    while (*sub == ' ') {
        ++sub;
    }

    if (*sub == '\0' || strcasecmp(sub, "status") == 0) {
        char version[48];
        load_version_from_nvs(version, sizeof(version));
        printf("almanac: state=%s version=%s url=%s last=\"%s\"\n",
               state_name(),
               version[0] != '\0' ? version : "-",
               s_manifest_url,
               s_last);
        fflush(stdout);
        return true;
    }

    if (strcasecmp(sub, "help") == 0) {
        printf("almanac commands:\n");
        printf("  almanac status\n");
        printf("  almanac fetch\n");
        printf("  almanac url <manifest-url>\n");
        printf("  almanac manifest\n");
        fflush(stdout);
        return true;
    }

    if (strcasecmp(sub, "fetch") == 0) {
        if (s_state == ALMANAC_STATE_RUNNING) {
            printf("almanac: fetch already running\n");
        } else if (xTaskCreate(fetch_task, "almanac_fetch", 8192, NULL, 3, NULL) == pdPASS) {
            printf("almanac: fetch started\n");
        } else {
            printf("almanac: fetch start failed\n");
        }
        fflush(stdout);
        return true;
    }

    if (strncasecmp(sub, "url ", 4) == 0) {
        const char *url = sub + 4;
        while (*url == ' ') {
            ++url;
        }
        const esp_err_t err = save_url_to_nvs(url);
        printf("almanac: url %s\n", esp_err_to_name(err));
        fflush(stdout);
        return true;
    }

    if (strcasecmp(sub, "manifest") == 0) {
        size_t len = 0;
        char *body = read_file_alloc(ALMANAC_MANIFEST_CACHE_PATH, ALMANAC_MANIFEST_MAX_BYTES, &len);
        if (body == NULL) {
            printf("almanac: no cached manifest\n");
        } else {
            printf("almanac: manifest BEGIN bytes=%u\n", (unsigned)len);
            fwrite(body, 1, len, stdout);
            printf("\nalmanac: manifest END\n");
            free(body);
        }
        fflush(stdout);
        return true;
    }

    printf("almanac: unknown command (try: almanac help)\n");
    fflush(stdout);
    return true;
}
