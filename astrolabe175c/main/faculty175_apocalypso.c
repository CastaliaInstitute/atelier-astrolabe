#include "faculty175_apocalypso.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

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

#include "faculty175_log.h"

static const char *TAG = "faculty175_apocalypso";

#define APOCALYPSO_URL "https://apocalypso.castalia.institute/api/ticker.json"
#define APOCALYPSO_STORAGE_BASE "/bust_cache"
#define APOCALYPSO_CACHE_PATH APOCALYPSO_STORAGE_BASE "/apocalypso-ticker.json"
#define APOCALYPSO_MAX_BYTES (16 * 1024)
#define APOCALYPSO_IO_BUFFER_BYTES 1024
#define APOCALYPSO_AUTO_INITIAL_DELAY_MS 51000
#define APOCALYPSO_AUTO_POLL_MS (30 * 60 * 1000)
#define APOCALYPSO_FETCH_STACK_BYTES 12288

typedef enum {
    APOCALYPSO_STATE_IDLE = 0,
    APOCALYPSO_STATE_RUNNING,
    APOCALYPSO_STATE_DONE,
    APOCALYPSO_STATE_ERROR,
} apocalypso_state_t;

static const char *const k_axis_ids[FACULTY175_APOCALYPSO_AXIS_COUNT] = {
    "APOC.Biblical", "APOC.Nuclear", "APOC.Bio",       "APOC.AI",
    "APOC.Cyber",    "APOC.Infra",   "APOC.Market",   "APOC.State",
    "APOC.Epistemic","APOC.Climate", "APOC.Biosphere","APOC.Solar",
};

static SemaphoreHandle_t s_lock;
EXT_RAM_BSS_ATTR static faculty175_apocalypso_status_t s_current;
static volatile apocalypso_state_t s_state;
static char s_last[96];
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

const char *faculty175_apocalypso_state_name(void)
{
    switch (s_state) {
        case APOCALYPSO_STATE_RUNNING: return "running";
        case APOCALYPSO_STATE_DONE: return "done";
        case APOCALYPSO_STATE_ERROR: return "error";
        case APOCALYPSO_STATE_IDLE:
        default: return "idle";
    }
}

const char *faculty175_apocalypso_last(void)
{
    return s_last;
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

static void fill_demo(faculty175_apocalypso_status_t *out)
{
    if (out == NULL) {
        return;
    }
    static const float demo[FACULTY175_APOCALYPSO_AXIS_COUNT] = {
        0.38f, 0.24f, 0.12f, 0.41f, 0.28f, 0.19f, 0.35f, 0.31f, 0.44f, 0.272f, 0.52f, 0.08f,
    };
    memset(out, 0, sizeof(*out));
    out->ok = true;
    out->demo = true;
    snprintf(out->updated_at, sizeof(out->updated_at), "offline");
    for (int i = 0; i < FACULTY175_APOCALYPSO_AXIS_COUNT; ++i) {
        out->value[i] = demo[i];
        snprintf(out->quality[i], sizeof(out->quality[i]), "demo");
    }
}

static bool storage_ready(void)
{
    if (s_storage_checked) {
        return s_storage_ready;
    }
    s_storage_checked = true;
    esp_vfs_spiffs_conf_t conf = {
        .base_path = APOCALYPSO_STORAGE_BASE,
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
    const size_t got = fread(body, 1, (size_t)n, f);
    fclose(f);
    if (got != (size_t)n) {
        free(body);
        return NULL;
    }
    body[got] = '\0';
    if (out_len != NULL) {
        *out_len = got;
    }
    return body;
}

static bool parse_ticker_json(const char *body, size_t len, faculty175_apocalypso_status_t *out)
{
    if (body == NULL || len == 0 || out == NULL) {
        return false;
    }
    cJSON *root = cJSON_ParseWithLength(body, len);
    if (root == NULL) {
        return false;
    }
    const cJSON *symbols = cJSON_GetObjectItemCaseSensitive(root, "symbols");
    if (!cJSON_IsArray(symbols)) {
        cJSON_Delete(root);
        return false;
    }

    faculty175_apocalypso_status_t st = {};
    copy_json_string(cJSON_GetObjectItemCaseSensitive(root, "updated_at"), st.updated_at, sizeof(st.updated_at));
    for (int axis = 0; axis < FACULTY175_APOCALYPSO_AXIS_COUNT; ++axis) {
        const cJSON *found = NULL;
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, symbols) {
            const cJSON *id = cJSON_GetObjectItemCaseSensitive(item, "id");
            if (cJSON_IsString(id) && id->valuestring != NULL && strcmp(id->valuestring, k_axis_ids[axis]) == 0) {
                found = item;
                break;
            }
        }
        if (found == NULL) {
            cJSON_Delete(root);
            return false;
        }
        const cJSON *value = cJSON_GetObjectItemCaseSensitive(found, "value");
        if (!cJSON_IsNumber(value)) {
            cJSON_Delete(root);
            return false;
        }
        float v = (float)value->valuedouble;
        if (v < 0.0f) {
            v = 0.0f;
        } else if (v > 1.0f) {
            v = 1.0f;
        }
        st.value[axis] = v;
        copy_json_string(cJSON_GetObjectItemCaseSensitive(found, "dataQuality"),
                         st.quality[axis],
                         sizeof(st.quality[axis]));
    }
    cJSON_Delete(root);
    st.ok = true;
    st.demo = false;
    if (st.updated_at[0] == '\0') {
        snprintf(st.updated_at, sizeof(st.updated_at), "live");
    }
    *out = st;
    return true;
}

static bool load_cached_locked(void)
{
    if (!storage_ready()) {
        return false;
    }
    size_t len = 0;
    char *body = read_file_alloc(APOCALYPSO_CACHE_PATH, APOCALYPSO_MAX_BYTES, &len);
    if (body == NULL) {
        return false;
    }
    faculty175_apocalypso_status_t st = {};
    const bool ok = parse_ticker_json(body, len, &st);
    free(body);
    if (!ok) {
        return false;
    }
    s_current = st;
    set_last("cache %s", st.updated_at);
    return true;
}

static esp_err_t http_download_to_file(const char *url, const char *path, size_t max_bytes, size_t *out_bytes)
{
    if (url == NULL || path == NULL || max_bytes == 0 || !storage_ready()) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = 15000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .keep_alive_enable = false,
        .buffer_size = APOCALYPSO_IO_BUFFER_BYTES,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        return ESP_ERR_NO_MEM;
    }
    esp_http_client_set_header(client, "User-Agent", "Astrolabe-Faculty175-Apocalypso/1");
    esp_http_client_set_header(client, "Accept", "application/json");

    FILE *f = NULL;
    uint8_t *buf = NULL;
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
        f = fopen(path, "wb");
        if (f == NULL) {
            err = ESP_FAIL;
            set_last("cache open failed");
        }
    }
    if (err == ESP_OK) {
        buf = malloc(APOCALYPSO_IO_BUFFER_BYTES);
        if (buf == NULL) {
            err = ESP_ERR_NO_MEM;
        }
    }
    while (err == ESP_OK) {
        const int n = esp_http_client_read(client, (char *)buf, APOCALYPSO_IO_BUFFER_BYTES);
        if (n < 0) {
            err = ESP_FAIL;
            set_last("read failed");
            break;
        }
        if (n == 0) {
            break;
        }
        total += (size_t)n;
        if (total > max_bytes) {
            err = ESP_ERR_INVALID_SIZE;
            set_last("too large");
            break;
        }
        if (fwrite(buf, 1, (size_t)n, f) != (size_t)n) {
            err = ESP_FAIL;
            set_last("write failed");
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

static void fetch_task(void *arg)
{
    const bool auto_loop = arg != NULL;
    if (auto_loop) {
        vTaskDelay(pdMS_TO_TICKS(APOCALYPSO_AUTO_INITIAL_DELAY_MS));
    }
    do {
        if (s_state != APOCALYPSO_STATE_RUNNING) {
            s_state = APOCALYPSO_STATE_RUNNING;
            size_t bytes = 0;
            FACULTY175_LOG_STAGE(TAG, "apocalypso", "fetch %s", APOCALYPSO_URL);
            esp_err_t err = http_download_to_file(APOCALYPSO_URL, APOCALYPSO_CACHE_PATH, APOCALYPSO_MAX_BYTES, &bytes);
            if (err == ESP_OK && xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) == pdTRUE) {
                if (!load_cached_locked()) {
                    err = ESP_FAIL;
                    set_last("parse failed");
                }
                xSemaphoreGive(s_lock);
            }
            if (err == ESP_OK) {
                set_last("fetched %u B", (unsigned)bytes);
            } else if (s_last[0] == '\0') {
                set_last("%s", esp_err_to_name(err));
            }
            s_state = err == ESP_OK ? APOCALYPSO_STATE_DONE : APOCALYPSO_STATE_ERROR;
            FACULTY175_LOG_STAGE(TAG, "apocalypso", "%s: %s", faculty175_apocalypso_state_name(), s_last);
        }
        if (!auto_loop) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(APOCALYPSO_AUTO_POLL_MS));
    } while (true);
    vTaskDelete(NULL);
}

esp_err_t faculty175_apocalypso_init(void)
{
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
        if (s_lock == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    s_state = APOCALYPSO_STATE_IDLE;
    set_last("idle");
    fill_demo(&s_current);
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) == pdTRUE) {
        (void)load_cached_locked();
        xSemaphoreGive(s_lock);
    }
    return ESP_OK;
}

void faculty175_apocalypso_start_auto_fetch_task(void)
{
    if (s_auto_started) {
        return;
    }
    s_auto_started = true;
    BaseType_t ok = xTaskCreateWithCaps(fetch_task,
                                        "apoc_auto",
                                        APOCALYPSO_FETCH_STACK_BYTES,
                                        (void *)1,
                                        3,
                                        NULL,
                                        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (ok != pdPASS) {
        s_auto_started = false;
        FACULTY175_LOG_STAGE_E(TAG, "apocalypso", "auto task start failed");
    }
}

void faculty175_apocalypso_request_refresh(void)
{
    if (s_state == APOCALYPSO_STATE_RUNNING) {
        return;
    }
    BaseType_t ok = xTaskCreateWithCaps(fetch_task,
                                        "apoc_fetch",
                                        APOCALYPSO_FETCH_STACK_BYTES,
                                        NULL,
                                        3,
                                        NULL,
                                        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (ok != pdPASS) {
        set_last("task start failed");
        s_state = APOCALYPSO_STATE_ERROR;
        FACULTY175_LOG_STAGE_E(TAG, "apocalypso", "fetch task start failed");
    }
}

bool faculty175_apocalypso_current(faculty175_apocalypso_status_t *out)
{
    if (out == NULL || s_lock == NULL) {
        return false;
    }
    bool ok = false;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        *out = s_current;
        ok = s_current.ok;
        xSemaphoreGive(s_lock);
    }
    return ok;
}
