#include "faculty175_rocket.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "astrolabe_time.h"
#include "faculty175_log.h"

static const char *TAG = "faculty175_rocket";

#define ROCKET_URL "https://fdo.rocketlaunch.live/json/launches/next/5"
#define ROCKET_STORAGE_BASE "/bust_cache"
#define ROCKET_CACHE_PATH ROCKET_STORAGE_BASE "/rocket-next.json"
#define ROCKET_MAX_BYTES (96 * 1024)
#define ROCKET_IO_BUFFER_BYTES 2048
#define ROCKET_AUTO_INITIAL_DELAY_MS 47000
#define ROCKET_AUTO_POLL_MS (2 * 60 * 60 * 1000)
#define ROCKET_FETCH_STACK_BYTES 16384

typedef enum {
    ROCKET_STATE_IDLE = 0,
    ROCKET_STATE_RUNNING,
    ROCKET_STATE_DONE,
    ROCKET_STATE_ERROR,
} rocket_state_t;

static SemaphoreHandle_t s_lock;
EXT_RAM_BSS_ATTR static faculty175_rocket_status_t s_current;
static volatile rocket_state_t s_state;
static char s_last[128];
static bool s_storage_checked;
static bool s_storage_ready;
static bool s_auto_started;

static void set_last(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s_last, sizeof(s_last), fmt, ap);
    va_end(ap);
}

const char *faculty175_rocket_state_name(void)
{
    switch (s_state) {
        case ROCKET_STATE_RUNNING: return "running";
        case ROCKET_STATE_DONE: return "done";
        case ROCKET_STATE_ERROR: return "error";
        case ROCKET_STATE_IDLE:
        default: return "idle";
    }
}

const char *faculty175_rocket_last(void)
{
    return s_last;
}

static bool storage_ready(void)
{
    if (s_storage_checked && s_storage_ready) {
        return true;
    }
    s_storage_checked = true;
    esp_vfs_spiffs_conf_t conf = {
        .base_path = ROCKET_STORAGE_BASE,
        .partition_label = "storage",
        .max_files = 16,
        .format_if_mount_failed = false,
    };
    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err == ESP_ERR_INVALID_STATE) {
        err = ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SPIFFS mount failed: %s", esp_err_to_name(err));
        return false;
    }
    s_storage_ready = true;
    return true;
}

static void copy_json_string(const cJSON *item, char *out, size_t cap)
{
    if (out == NULL || cap == 0) {
        return;
    }
    out[0] = '\0';
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        snprintf(out, cap, "%s", item->valuestring);
    }
}

static time_t parse_iso_utc(const char *iso)
{
    if (iso == NULL || iso[0] == '\0') {
        return 0;
    }
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
    if (sscanf(iso, "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &s) < 5) {
        return 0;
    }
    struct tm u = {
        .tm_sec = s,
        .tm_min = mi,
        .tm_hour = h,
        .tm_mday = d,
        .tm_mon = mo - 1,
        .tm_year = y - 1900,
        .tm_isdst = 0,
    };
    const char *prev = getenv("TZ");
    char saved[64] = {};
    if (prev != NULL) {
        snprintf(saved, sizeof(saved), "%s", prev);
    }
    setenv("TZ", "UTC0", 1);
    tzset();
    const time_t epoch = mktime(&u);
    if (prev != NULL) {
        setenv("TZ", saved, 1);
    } else {
        unsetenv("TZ");
    }
    tzset();
    return epoch;
}

static char *read_file_alloc(const char *path, size_t max_bytes, size_t *out_len)
{
    if (out_len != NULL) {
        *out_len = 0;
    }
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    if (n <= 0 || (size_t)n > max_bytes) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    char *body = malloc((size_t)n + 1);
    if (body == NULL) {
        fclose(f);
        return NULL;
    }
    const size_t rd = fread(body, 1, (size_t)n, f);
    fclose(f);
    body[rd] = '\0';
    if (out_len != NULL) {
        *out_len = rd;
    }
    return body;
}

static bool parse_rocket_json(const char *body, size_t len, faculty175_rocket_status_t *out)
{
    if (body == NULL || len == 0 || out == NULL) {
        return false;
    }
    cJSON *root = cJSON_ParseWithLength(body, len);
    if (root == NULL) {
        return false;
    }
    const cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    if (!cJSON_IsArray(result)) {
        cJSON_Delete(root);
        return false;
    }
    faculty175_rocket_status_t st = {};
    const time_t now = time(NULL);
    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, result) {
        if (st.count >= FACULTY175_ROCKET_MAX_LAUNCHES) {
            break;
        }
        faculty175_rocket_launch_t *lv = &st.launches[st.count];
        char t0[32] = {};
        copy_json_string(cJSON_GetObjectItemCaseSensitive(item, "t0"), t0, sizeof(t0));
        time_t net = parse_iso_utc(t0);
        if (net <= 0) {
            const cJSON *sort_date = cJSON_GetObjectItemCaseSensitive(item, "sort_date");
            if (cJSON_IsString(sort_date) && sort_date->valuestring != NULL) {
                net = (time_t)strtoll(sort_date->valuestring, NULL, 10);
            }
        }
        if (net <= 0 || (now > 1700000000 && net < now - 1800)) {
            continue;
        }
        const cJSON *provider = cJSON_GetObjectItemCaseSensitive(item, "provider");
        const cJSON *vehicle = cJSON_GetObjectItemCaseSensitive(item, "vehicle");
        const cJSON *pad = cJSON_GetObjectItemCaseSensitive(item, "pad");
        const cJSON *location = cJSON_GetObjectItemCaseSensitive(pad, "location");
        const cJSON *missions = cJSON_GetObjectItemCaseSensitive(item, "missions");
        const cJSON *mission0 = cJSON_IsArray(missions) ? cJSON_GetArrayItem(missions, 0) : NULL;
        const cJSON *id = cJSON_GetObjectItemCaseSensitive(item, "id");
        if (cJSON_IsNumber(id)) {
            snprintf(lv->id, sizeof(lv->id), "%d", id->valueint);
        }
        copy_json_string(cJSON_GetObjectItemCaseSensitive(item, "name"), lv->name, sizeof(lv->name));
        if (lv->name[0] == '\0') {
            copy_json_string(cJSON_GetObjectItemCaseSensitive(mission0, "name"), lv->name, sizeof(lv->name));
        }
        copy_json_string(cJSON_GetObjectItemCaseSensitive(vehicle, "name"), lv->vehicle, sizeof(lv->vehicle));
        copy_json_string(cJSON_GetObjectItemCaseSensitive(provider, "name"), lv->provider, sizeof(lv->provider));
        copy_json_string(cJSON_GetObjectItemCaseSensitive(pad, "name"), lv->pad, sizeof(lv->pad));
        copy_json_string(cJSON_GetObjectItemCaseSensitive(location, "name"), lv->location, sizeof(lv->location));
        copy_json_string(cJSON_GetObjectItemCaseSensitive(item, "weather_summary"), lv->weather, sizeof(lv->weather));
        copy_json_string(cJSON_GetObjectItemCaseSensitive(item, "quicktext"), lv->info_url, sizeof(lv->info_url));
        lv->net_unix = (int64_t)net;
        lv->valid = lv->name[0] != '\0';
        if (lv->valid) {
            ++st.count;
        }
    }
    cJSON_Delete(root);
    st.ok = st.count > 0;
    if (!st.ok) {
        snprintf(st.error, sizeof(st.error), "no launches");
    }
    *out = st;
    return st.ok;
}

static bool load_cached_locked(void)
{
    if (!storage_ready()) {
        return false;
    }
    size_t len = 0;
    char *body = read_file_alloc(ROCKET_CACHE_PATH, ROCKET_MAX_BYTES, &len);
    if (body == NULL) {
        return false;
    }
    faculty175_rocket_status_t st = {};
    const bool ok = parse_rocket_json(body, len, &st);
    free(body);
    if (ok) {
        s_current = st;
        set_last("cache count=%d", st.count);
    }
    return ok;
}

static esp_err_t http_download_to_memory(const char *url, size_t max_bytes, char **out_body, size_t *out_bytes)
{
    if (url == NULL || max_bytes == 0 || out_body == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_body = NULL;
    if (out_bytes != NULL) {
        *out_bytes = 0;
    }
    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = 20000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .keep_alive_enable = false,
        .buffer_size = ROCKET_IO_BUFFER_BYTES,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        return ESP_ERR_NO_MEM;
    }
    esp_http_client_set_header(client, "User-Agent", "Astrolabe-Faculty175-Rocket/1");
    esp_http_client_set_header(client, "Accept", "application/json");
    uint8_t *buf = NULL;
    char *body = NULL;
    size_t total = 0;
    esp_err_t err = esp_http_client_open(client, 0);
    if (err == ESP_OK) {
        (void)esp_http_client_fetch_headers(client);
        const int status = esp_http_client_get_status_code(client);
        if (status != 200) {
            err = ESP_FAIL;
            set_last("HTTP %d", status);
        }
    }
    if (err == ESP_OK) {
        buf = malloc(ROCKET_IO_BUFFER_BYTES);
        body = malloc(max_bytes + 1);
        if (buf == NULL || body == NULL) {
            err = ESP_ERR_NO_MEM;
        }
    }
    while (err == ESP_OK) {
        const int n = esp_http_client_read(client, (char *)buf, ROCKET_IO_BUFFER_BYTES);
        if (n < 0) {
            err = ESP_FAIL;
            set_last("read failed");
            break;
        }
        if (n == 0) {
            break;
        }
        if (total + (size_t)n > max_bytes) {
            err = ESP_FAIL;
            set_last("response too large");
            break;
        }
        memcpy(body + total, buf, (size_t)n);
        total += (size_t)n;
    }
    free(buf);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    if (err != ESP_OK) {
        free(body);
    } else if (out_bytes != NULL) {
        body[total] = '\0';
        *out_body = body;
        *out_bytes = total;
    }
    return err;
}

static void fetch_task(void *arg)
{
    const bool auto_loop = arg != NULL;
    if (auto_loop) {
        vTaskDelay(pdMS_TO_TICKS(ROCKET_AUTO_INITIAL_DELAY_MS));
    }
    do {
        if (s_state != ROCKET_STATE_RUNNING) {
            s_state = ROCKET_STATE_RUNNING;
            size_t bytes = 0;
            char *body = NULL;
            FACULTY175_LOG_STAGE(TAG, "rocket", "fetch %s", ROCKET_URL);
            esp_err_t err = http_download_to_memory(ROCKET_URL, ROCKET_MAX_BYTES, &body, &bytes);
            if (err == ESP_OK && xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) == pdTRUE) {
                faculty175_rocket_status_t st = {};
                if (parse_rocket_json(body, bytes, &st)) {
                    s_current = st;
                } else {
                    err = ESP_FAIL;
                    set_last("parse failed");
                }
                xSemaphoreGive(s_lock);
            }
            free(body);
            if (err == ESP_OK) {
                set_last("fetched %u B count=%d", (unsigned)bytes, s_current.count);
            } else if (s_last[0] == '\0') {
                set_last("%s", esp_err_to_name(err));
            }
            s_state = err == ESP_OK ? ROCKET_STATE_DONE : ROCKET_STATE_ERROR;
            FACULTY175_LOG_STAGE(TAG, "rocket", "%s: %s", faculty175_rocket_state_name(), s_last);
        }
        if (!auto_loop) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(ROCKET_AUTO_POLL_MS));
    } while (true);
    vTaskDelete(NULL);
}

esp_err_t faculty175_rocket_init(void)
{
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
        if (s_lock == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    s_state = ROCKET_STATE_IDLE;
    set_last("idle");
    memset(&s_current, 0, sizeof(s_current));
    snprintf(s_current.error, sizeof(s_current.error), "waiting");
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) == pdTRUE) {
        (void)load_cached_locked();
        xSemaphoreGive(s_lock);
    }
    return ESP_OK;
}

void faculty175_rocket_start_auto_fetch_task(void)
{
    if (s_auto_started) {
        return;
    }
    s_auto_started = true;
    BaseType_t ok = xTaskCreateWithCaps(fetch_task, "rocket_auto", ROCKET_FETCH_STACK_BYTES,
                                        (void *)1, 3, NULL, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (ok != pdPASS) {
        s_auto_started = false;
        FACULTY175_LOG_STAGE_E(TAG, "rocket", "auto task start failed");
    }
}

void faculty175_rocket_request_refresh(void)
{
    if (s_state != ROCKET_STATE_RUNNING) {
        BaseType_t ok = xTaskCreateWithCaps(fetch_task, "rocket_fetch", ROCKET_FETCH_STACK_BYTES,
                                            NULL, 3, NULL, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (ok != pdPASS) {
            set_last("task start failed");
            s_state = ROCKET_STATE_ERROR;
            FACULTY175_LOG_STAGE_E(TAG, "rocket", "fetch task start failed");
        }
    }
}

bool faculty175_rocket_current(faculty175_rocket_status_t *out)
{
    if (out == NULL || s_lock == NULL) {
        return false;
    }
    bool ok = false;
    bool request_fetch = false;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (!s_current.ok && s_state != ROCKET_STATE_RUNNING) {
            (void)load_cached_locked();
        }
        request_fetch = !s_current.ok && s_state == ROCKET_STATE_IDLE;
        *out = s_current;
        ok = s_current.ok;
        xSemaphoreGive(s_lock);
    }
    if (request_fetch) {
        faculty175_rocket_request_refresh();
    }
    return ok;
}

void faculty175_rocket_format_countdown(int64_t launch_unix, char *out, size_t cap)
{
    if (out == NULL || cap == 0) {
        return;
    }
    const int64_t now = astrolabe_time_valid() ? (int64_t)astrolabe_time_now() : (int64_t)time(NULL);
    int64_t delta = now - launch_unix;
    const bool after = delta >= 0;
    if (!after) {
        delta = -delta;
    }
    const int64_t days = delta / 86400;
    delta %= 86400;
    const int h = (int)(delta / 3600);
    const int m = (int)((delta % 3600) / 60);
    const int s = (int)(delta % 60);
    if (days > 0) {
        snprintf(out, cap, after ? "T+%lldd %02d:%02d:%02d" : "T-%lldd %02d:%02d:%02d",
                 (long long)days, h, m, s);
    } else if (h > 0) {
        snprintf(out, cap, after ? "T+%02d:%02d:%02d" : "T-%02d:%02d:%02d", h, m, s);
    } else {
        snprintf(out, cap, after ? "T+%02d:%02d" : "T-%02d:%02d", m, s);
    }
}

void faculty175_rocket_format_local(int64_t launch_unix, char *out, size_t cap)
{
    if (out == NULL || cap == 0) {
        return;
    }
    struct tm tm_local = {};
    const time_t t = (time_t)launch_unix;
    localtime_r(&t, &tm_local);
    snprintf(out, cap, "%02d/%02d %02d:%02d", tm_local.tm_mon + 1, tm_local.tm_mday, tm_local.tm_hour, tm_local.tm_min);
}

bool faculty175_rocket_handle(const char *line)
{
    if (line == NULL || (strcasecmp(line, "rocket") != 0 && strncasecmp(line, "rocket ", 7) != 0)) {
        return false;
    }
    const char *sub = strchr(line, ' ');
    sub = sub != NULL ? sub + 1 : "status";
    while (*sub == ' ') {
        ++sub;
    }
    if (*sub == '\0' || strcasecmp(sub, "status") == 0) {
        faculty175_rocket_status_t st = {};
        (void)faculty175_rocket_current(&st);
        printf("rocket: state=%s last=\"%s\" count=%d ok=%s\n",
               faculty175_rocket_state_name(), s_last, st.count, st.ok ? "yes" : "no");
        if (st.count > 0) {
            char cd[32];
            faculty175_rocket_format_countdown(st.launches[0].net_unix, cd, sizeof(cd));
            printf("rocket: next=%s %s %s %s\n", cd, st.launches[0].vehicle, st.launches[0].name, st.launches[0].pad);
        }
        fflush(stdout);
        return true;
    }
    if (strcasecmp(sub, "fetch") == 0) {
        faculty175_rocket_request_refresh();
        printf("rocket: fetch started\n");
        fflush(stdout);
        return true;
    }
    printf("rocket commands:\n  rocket status\n  rocket fetch\n");
    fflush(stdout);
    return true;
}
