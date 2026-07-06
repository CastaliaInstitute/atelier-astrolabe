#include "faculty175_quotes.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "faculty175_faculty.h"
#include "faculty175_log.h"

static const char *TAG = "faculty175_quotes";

#define QUOTES_URL "https://quotes.castalia.institute/quote-of-the-day.json"
#define QUOTES_STORAGE_BASE "/bust_cache"
#define QUOTES_CACHE_PATH QUOTES_STORAGE_BASE "/quotes-qotd.json"
#define QUOTES_MAX_BYTES 8192
#define QUOTES_IO_BUFFER_BYTES 1024
#define QUOTES_AUTO_INITIAL_DELAY_MS 42000
#define QUOTES_AUTO_POLL_MS (60 * 60 * 1000)

typedef enum {
    QUOTES_STATE_IDLE = 0,
    QUOTES_STATE_RUNNING,
    QUOTES_STATE_DONE,
    QUOTES_STATE_ERROR,
} quotes_state_t;

static SemaphoreHandle_t s_lock;
static faculty175_quote_t s_current;
static volatile quotes_state_t s_state;
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

const char *faculty175_quotes_state_name(void)
{
    switch (s_state) {
        case QUOTES_STATE_RUNNING: return "running";
        case QUOTES_STATE_DONE: return "done";
        case QUOTES_STATE_ERROR: return "error";
        case QUOTES_STATE_IDLE:
        default: return "idle";
    }
}

const char *faculty175_quotes_last(void)
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

static void fill_demo(faculty175_quote_t *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->ok = true;
    out->demo = true;
    snprintf(out->date, sizeof(out->date), "offline");
    out->index = 1;
    out->total = 1;
    snprintf(out->faculty_slug, sizeof(out->faculty_slug), "a.plato");
    snprintf(out->faculty_name, sizeof(out->faculty_name), "Plato");
    snprintf(out->quote, sizeof(out->quote), "The beginning is the most important part of the work.");
    snprintf(out->passage, sizeof(out->passage), "The Republic, Book II");
    snprintf(out->book_title, sizeof(out->book_title), "The Republic");
    snprintf(out->book_author, sizeof(out->book_author), "Plato");
}

static bool storage_ready(void)
{
    if (s_storage_checked) {
        return s_storage_ready;
    }
    s_storage_checked = true;

    esp_vfs_spiffs_conf_t conf = {
        .base_path = QUOTES_STORAGE_BASE,
        .partition_label = "storage",
        .max_files = 14,
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
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    const long n = ftell(f);
    if (n <= 0 || (size_t)n > max_bytes) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    char *buf = heap_caps_malloc((size_t)n + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buf == NULL) {
        buf = malloc((size_t)n + 1);
    }
    if (buf == NULL) {
        fclose(f);
        return NULL;
    }
    const size_t rd = fread(buf, 1, (size_t)n, f);
    fclose(f);
    if (rd != (size_t)n) {
        free(buf);
        return NULL;
    }
    buf[rd] = '\0';
    if (out_len != NULL) {
        *out_len = rd;
    }
    return buf;
}

static bool parse_quote_json(const char *body, size_t len, faculty175_quote_t *out)
{
    if (body == NULL || len == 0 || out == NULL) {
        return false;
    }
    cJSON *root = cJSON_ParseWithLength(body, len);
    if (root == NULL) {
        return false;
    }

    faculty175_quote_t q = {};
    const cJSON *quote = cJSON_GetObjectItemCaseSensitive(root, "quote");
    copy_json_string(cJSON_GetObjectItemCaseSensitive(root, "date"), q.date, sizeof(q.date));
    const cJSON *idx = cJSON_GetObjectItemCaseSensitive(root, "index");
    const cJSON *total = cJSON_GetObjectItemCaseSensitive(root, "total");
    q.index = cJSON_IsNumber(idx) ? idx->valueint : 0;
    q.total = cJSON_IsNumber(total) ? total->valueint : 0;
    copy_json_string(cJSON_GetObjectItemCaseSensitive(quote, "faculty_id"), q.faculty_slug, sizeof(q.faculty_slug));
    copy_json_string(cJSON_GetObjectItemCaseSensitive(quote, "faculty_name"), q.faculty_name, sizeof(q.faculty_name));
    copy_json_string(cJSON_GetObjectItemCaseSensitive(quote, "quote_text"), q.quote, sizeof(q.quote));
    copy_json_string(cJSON_GetObjectItemCaseSensitive(quote, "passage_label"), q.passage, sizeof(q.passage));
    copy_json_string(cJSON_GetObjectItemCaseSensitive(quote, "book_title"), q.book_title, sizeof(q.book_title));
    copy_json_string(cJSON_GetObjectItemCaseSensitive(quote, "book_author"), q.book_author, sizeof(q.book_author));
    cJSON_Delete(root);

    if (q.faculty_slug[0] == '\0' || q.quote[0] == '\0') {
        return false;
    }
    q.ok = true;
    *out = q;
    return true;
}

static bool load_cached_locked(void)
{
    if (!storage_ready()) {
        return false;
    }
    size_t len = 0;
    char *body = read_file_alloc(QUOTES_CACHE_PATH, QUOTES_MAX_BYTES, &len);
    if (body == NULL) {
        return false;
    }
    faculty175_quote_t q = {};
    const bool ok = parse_quote_json(body, len, &q);
    free(body);
    if (!ok) {
        return false;
    }
    s_current = q;
    set_last("cache %s %s", q.date, q.faculty_slug);
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
        .buffer_size = QUOTES_IO_BUFFER_BYTES,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        return ESP_ERR_NO_MEM;
    }
    esp_http_client_set_header(client, "User-Agent", "Astrolabe-Faculty175-Quotes/1");
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
            err = ESP_ERR_NO_MEM;
            set_last("open failed");
        }
    }
    if (err == ESP_OK) {
        buf = malloc(QUOTES_IO_BUFFER_BYTES);
        if (buf == NULL) {
            err = ESP_ERR_NO_MEM;
        }
    }
    while (err == ESP_OK) {
        const int n = esp_http_client_read(client, (char *)buf, QUOTES_IO_BUFFER_BYTES);
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
        vTaskDelay(pdMS_TO_TICKS(QUOTES_AUTO_INITIAL_DELAY_MS));
    }
    do {
        if (s_state != QUOTES_STATE_RUNNING) {
            s_state = QUOTES_STATE_RUNNING;
            size_t bytes = 0;
            FACULTY175_LOG_STAGE(TAG, "quotes", "fetch %s", QUOTES_URL);
            esp_err_t err = http_download_to_file(QUOTES_URL, QUOTES_CACHE_PATH, QUOTES_MAX_BYTES, &bytes);
            if (err == ESP_OK && xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) == pdTRUE) {
                if (!load_cached_locked()) {
                    err = ESP_FAIL;
                    set_last("parse failed");
                }
                xSemaphoreGive(s_lock);
            }
            if (err == ESP_OK) {
                faculty175_quote_t q = {};
                (void)faculty175_quotes_current(&q);
                if (q.faculty_slug[0] != '\0') {
                    faculty175_faculty_request_bust(q.faculty_slug);
                }
                set_last("fetched %u B", (unsigned)bytes);
            } else if (s_last[0] == '\0') {
                set_last("%s", esp_err_to_name(err));
            }
            s_state = err == ESP_OK ? QUOTES_STATE_DONE : QUOTES_STATE_ERROR;
            FACULTY175_LOG_STAGE(TAG, "quotes", "%s: %s", faculty175_quotes_state_name(), s_last);
        }
        if (!auto_loop) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(QUOTES_AUTO_POLL_MS));
    } while (true);
    vTaskDelete(NULL);
}

esp_err_t faculty175_quotes_init(void)
{
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
        if (s_lock == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    s_state = QUOTES_STATE_IDLE;
    set_last("idle");
    fill_demo(&s_current);
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) == pdTRUE) {
        (void)load_cached_locked();
        xSemaphoreGive(s_lock);
    }
    return ESP_OK;
}

void faculty175_quotes_start_auto_fetch_task(void)
{
    if (s_auto_started) {
        return;
    }
    s_auto_started = true;
    xTaskCreate(fetch_task, "quotes_auto", 8192, (void *)1, 3, NULL);
}

void faculty175_quotes_request_refresh(void)
{
    if (s_state == QUOTES_STATE_RUNNING) {
        return;
    }
    xTaskCreate(fetch_task, "quotes_fetch", 8192, NULL, 3, NULL);
}

bool faculty175_quotes_current(faculty175_quote_t *out)
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

bool faculty175_quotes_handle(const char *line)
{
    if (line == NULL || (strcasecmp(line, "quotes") != 0 && strncasecmp(line, "quotes ", 7) != 0)) {
        return false;
    }
    const char *sub = strchr(line, ' ');
    sub = sub != NULL ? sub + 1 : "status";
    while (*sub == ' ') {
        ++sub;
    }
    if (*sub == '\0' || strcasecmp(sub, "status") == 0) {
        faculty175_quote_t q = {};
        (void)faculty175_quotes_current(&q);
        printf("quotes: state=%s last=\"%s\" date=%s faculty=%s demo=%s\n",
               faculty175_quotes_state_name(),
               s_last,
               q.date[0] != '\0' ? q.date : "-",
               q.faculty_slug[0] != '\0' ? q.faculty_slug : "-",
               q.demo ? "yes" : "no");
        if (q.ok) {
            printf("quotes: \"%s\" — %s\n", q.quote, q.faculty_name[0] != '\0' ? q.faculty_name : q.faculty_slug);
        }
        fflush(stdout);
        return true;
    }
    if (strcasecmp(sub, "fetch") == 0) {
        faculty175_quotes_request_refresh();
        printf("quotes: fetch started\n");
        fflush(stdout);
        return true;
    }
    if (strcasecmp(sub, "help") == 0) {
        printf("quotes commands:\n");
        printf("  quotes status\n");
        printf("  quotes fetch\n");
        fflush(stdout);
        return true;
    }
    printf("quotes: unknown command (try: quotes help)\n");
    fflush(stdout);
    return true;
}
