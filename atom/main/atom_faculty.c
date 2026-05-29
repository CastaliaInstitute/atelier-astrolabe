#include "atom_faculty.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_jpeg_dec.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "atom_board.h"

#if __has_include("secrets.local.h")
#include "secrets.local.h"
#else
#include "secrets.example.h"
#endif

static const char *TAG = "atom_faculty";
#define HTTP_TIMEOUT_MS 20000
#define BUST_JPEG_MAX_BYTES (96 * 1024)

static TaskHandle_t s_bust_task;
static SemaphoreHandle_t s_bust_lock;
static char s_req_slug[64];
static char s_loaded_slug[64];
static atom_faculty_bust_status_t s_status = ATOM_FACULTY_BUST_IDLE;
static uint16_t s_bust_pixels[ATOM_FACULTY_BUST_W * ATOM_FACULTY_BUST_H];
static int s_bust_draw_w;
static int s_bust_draw_h;
static atom_faculty_ui_notify_fn s_ui_notify;

static bool json_field(const char *body, const char *key, char *out, size_t cap)
{
    if (body == NULL || key == NULL || out == NULL || cap == 0) {
        return false;
    }
    char pattern[48];
    snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
    const char *start = strstr(body, pattern);
    if (start == NULL) {
        out[0] = '\0';
        return false;
    }
    start += strlen(pattern);
    size_t i = 0;
    while (start[i] != '\0' && start[i] != '"' && i + 1 < cap) {
        if (start[i] == '\\' && start[i + 1] != '\0') {
            out[i] = start[i + 1];
            ++i;
            ++start;
        } else {
            out[i] = start[i];
            ++i;
        }
    }
    out[i] = '\0';
    return i > 0;
}

static esp_err_t http_collect_get(const char *url, uint8_t **out_body, size_t *out_len, int *out_status)
{
    if (out_body != NULL) {
        *out_body = NULL;
    }
    if (out_len != NULL) {
        *out_len = 0;
    }
    if (out_status != NULL) {
        *out_status = 0;
    }

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Accept", "image/jpeg,application/json,*/*;q=0.8");
    if (strlen(MYNAH_SUPABASE_ANON_KEY) > 0) {
        esp_http_client_set_header(client, "apikey", MYNAH_SUPABASE_ANON_KEY);
        char auth[192];
        snprintf(auth, sizeof(auth), "Bearer %s", MYNAH_SUPABASE_ANON_KEY);
        esp_http_client_set_header(client, "Authorization", auth);
    }

    esp_err_t ret = esp_http_client_open(client, 0);
    if (ret != ESP_OK) {
        esp_http_client_cleanup(client);
        return ret;
    }

    (void)esp_http_client_fetch_headers(client);
    const int status = esp_http_client_get_status_code(client);
    if (out_status != NULL) {
        *out_status = status;
    }
    if (status < 200 || status >= 300) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    size_t cap = 16 * 1024;
    uint8_t *response = heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (response == NULL) {
        response = malloc(cap);
    }
    if (response == NULL) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_NO_MEM;
    }

    size_t total = 0;
    while (true) {
        if (total == cap) {
            if (cap >= BUST_JPEG_MAX_BYTES) {
                free(response);
                esp_http_client_close(client);
                esp_http_client_cleanup(client);
                return ESP_ERR_NO_MEM;
            }
            size_t next = cap * 2;
            if (next > BUST_JPEG_MAX_BYTES) {
                next = BUST_JPEG_MAX_BYTES;
            }
            uint8_t *grown = heap_caps_realloc(response, next, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (grown == NULL) {
                grown = realloc(response, next);
            }
            if (grown == NULL) {
                free(response);
                esp_http_client_close(client);
                esp_http_client_cleanup(client);
                return ESP_ERR_NO_MEM;
            }
            response = grown;
            cap = next;
        }
        const int read = esp_http_client_read(client, (char *)response + total, (int)(cap - total));
        if (read < 0) {
            free(response);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return ESP_FAIL;
        }
        if (read == 0) {
            break;
        }
        total += (size_t)read;
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    if (out_body != NULL) {
        *out_body = response;
    } else {
        free(response);
    }
    if (out_len != NULL) {
        *out_len = total;
    }
    return ESP_OK;
}

static bool fetch_bust_url(const char *url, uint8_t **bytes, size_t *len, int depth)
{
    if (depth > 1 || url == NULL || bytes == NULL || len == NULL) {
        return false;
    }

    int status = 0;
    uint8_t *body = NULL;
    size_t body_len = 0;
    if (http_collect_get(url, &body, &body_len, &status) != ESP_OK || body == NULL || body_len == 0) {
        free(body);
        ESP_LOGW(TAG, "faculty-bust GET failed status=%d url=%s", status, url);
        return false;
    }

    if (body_len > 2 && body[0] == '{') {
        char *json = realloc(body, body_len + 1);
        if (json == NULL) {
            free(body);
            return false;
        }
        body = (uint8_t *)json;
        body[body_len] = '\0';
        char signed_url[384];
        if (!json_field((const char *)body, "url", signed_url, sizeof(signed_url))) {
            free(body);
            return false;
        }
        free(body);
        return fetch_bust_url(signed_url, bytes, len, depth + 1);
    }

    *bytes = body;
    *len = body_len;
    return true;
}

static bool decode_jpeg_rgb565(const uint8_t *jpeg, size_t jpeg_len, uint16_t *out, int *out_w, int *out_h)
{
    if (jpeg == NULL || jpeg_len < 64 || out == NULL) {
        return false;
    }

    jpeg_dec_config_t config = {
        .output_type = JPEG_RAW_TYPE_RGB565_BE,
        .rotate = JPEG_ROTATE_0D,
    };
    jpeg_dec_handle_t jpeg_dec = jpeg_dec_open(&config);
    if (jpeg_dec == NULL) {
        return false;
    }

    jpeg_dec_io_t *jpeg_io = heap_caps_calloc(1, sizeof(jpeg_dec_io_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    jpeg_dec_header_info_t *jpeg_info =
        heap_caps_aligned_alloc(16, sizeof(jpeg_dec_header_info_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (jpeg_io == NULL || jpeg_info == NULL) {
        free(jpeg_io);
        free(jpeg_info);
        jpeg_dec_close(jpeg_dec);
        return false;
    }

    jpeg_io->inbuf = (uint8_t *)jpeg;
    jpeg_io->inbuf_len = (int)jpeg_len;
    esp_err_t ret = jpeg_dec_parse_header(jpeg_dec, jpeg_io, jpeg_info);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "JPEG header parse failed: %s", esp_err_to_name(ret));
        free(jpeg_io);
        free(jpeg_info);
        jpeg_dec_close(jpeg_dec);
        return false;
    }

    const int consumed = jpeg_io->inbuf_len - jpeg_io->inbuf_remain;
    jpeg_io->inbuf = (uint8_t *)jpeg + consumed;
    jpeg_io->inbuf_len = jpeg_io->inbuf_remain;
    jpeg_io->outbuf = (uint8_t *)out;

    ret = jpeg_dec_process(jpeg_dec, jpeg_io);
    const int decoded_w = jpeg_info->width;
    const int decoded_h = jpeg_info->height;
    jpeg_dec_close(jpeg_dec);
    free(jpeg_io);
    free(jpeg_info);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "JPEG decode failed: %s", esp_err_to_name(ret));
        return false;
    }

    if (out_w != NULL) {
        *out_w = decoded_w;
    }
    if (out_h != NULL) {
        *out_h = decoded_h;
    }
    return true;
}

static bool fetch_and_decode_bust(const char *slug)
{
    if (slug == NULL || slug[0] == '\0' || strlen(MYNAH_SUPABASE_URL) == 0) {
        return false;
    }

    char url[256];
    snprintf(url, sizeof(url), "%s/functions/v1/faculty-bust?faculty=%s&w=%d&h=%d&q=%d", MYNAH_SUPABASE_URL, slug,
             ATOM_FACULTY_BUST_W, ATOM_FACULTY_BUST_H, 60);

    uint8_t *jpeg = NULL;
    size_t jpeg_len = 0;
    if (!fetch_bust_url(url, &jpeg, &jpeg_len, 0)) {
        return false;
    }

    uint16_t *scratch =
        heap_caps_malloc(ATOM_FACULTY_BUST_W * ATOM_FACULTY_BUST_H * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (scratch == NULL) {
        scratch = malloc(ATOM_FACULTY_BUST_W * ATOM_FACULTY_BUST_H * sizeof(uint16_t));
    }
    if (scratch == NULL) {
        free(jpeg);
        return false;
    }

    int draw_w = 0;
    int draw_h = 0;
    const size_t fetched_len = jpeg_len;
    const bool ok = decode_jpeg_rgb565(jpeg, jpeg_len, scratch, &draw_w, &draw_h);
    free(jpeg);
    if (!ok || draw_w <= 0 || draw_h <= 0) {
        free(scratch);
        return false;
    }

    if (xSemaphoreTake(s_bust_lock, pdMS_TO_TICKS(2000)) == pdTRUE) {
        memcpy(s_bust_pixels, scratch, (size_t)draw_w * (size_t)draw_h * sizeof(uint16_t));
        s_bust_draw_w = draw_w;
        s_bust_draw_h = draw_h;
        strncpy(s_loaded_slug, slug, sizeof(s_loaded_slug) - 1);
        s_loaded_slug[sizeof(s_loaded_slug) - 1] = '\0';
        s_status = ATOM_FACULTY_BUST_READY;
        xSemaphoreGive(s_bust_lock);
    }
    free(scratch);
    ESP_LOGI(TAG, "bust ready %s (%dx%d, %u B jpeg)", slug, draw_w, draw_h, (unsigned)fetched_len);
    return true;
}

static void bust_worker_task(void *arg)
{
    (void)arg;
    char slug[64];
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        strncpy(slug, s_req_slug, sizeof(slug) - 1);
        slug[sizeof(slug) - 1] = '\0';
        if (slug[0] == '\0') {
            continue;
        }

        if (xSemaphoreTake(s_bust_lock, pdMS_TO_TICKS(1000)) == pdTRUE) {
            if (s_status == ATOM_FACULTY_BUST_READY && strcmp(s_loaded_slug, slug) == 0) {
                xSemaphoreGive(s_bust_lock);
                continue;
            }
            s_status = ATOM_FACULTY_BUST_LOADING;
            xSemaphoreGive(s_bust_lock);
        }

        if (s_ui_notify != NULL) {
            s_ui_notify();
        }

        const bool ok = fetch_and_decode_bust(slug);
        if (!ok) {
            if (xSemaphoreTake(s_bust_lock, pdMS_TO_TICKS(1000)) == pdTRUE) {
                s_status = ATOM_FACULTY_BUST_ERROR;
                xSemaphoreGive(s_bust_lock);
            }
            ESP_LOGW(TAG, "bust fetch failed for %s", slug);
        }

        if (s_ui_notify != NULL) {
            s_ui_notify();
        }
    }
}

esp_err_t atom_faculty_init(void)
{
    s_bust_lock = xSemaphoreCreateMutex();
    if (s_bust_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }
    s_loaded_slug[0] = '\0';
    s_req_slug[0] = '\0';
    s_bust_draw_w = 0;
    s_bust_draw_h = 0;
    if (xTaskCreate(bust_worker_task, "fac_bust", 8192, NULL, 3, &s_bust_task) != pdPASS) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

void atom_faculty_set_ui_notify(atom_faculty_ui_notify_fn fn)
{
    s_ui_notify = fn;
}

void atom_faculty_request_bust(const char *slug)
{
    if (slug == NULL || slug[0] == '\0' || s_bust_task == NULL) {
        return;
    }
    if (xSemaphoreTake(s_bust_lock, pdMS_TO_TICKS(200)) == pdTRUE) {
        if (s_status == ATOM_FACULTY_BUST_LOADING && strcmp(s_req_slug, slug) == 0) {
            xSemaphoreGive(s_bust_lock);
            return;
        }
        if (s_status == ATOM_FACULTY_BUST_READY && strcmp(s_loaded_slug, slug) == 0) {
            xSemaphoreGive(s_bust_lock);
            return;
        }
        strncpy(s_req_slug, slug, sizeof(s_req_slug) - 1);
        s_req_slug[sizeof(s_req_slug) - 1] = '\0';
        xSemaphoreGive(s_bust_lock);
    }
    xTaskNotifyGive(s_bust_task);
}

atom_faculty_bust_status_t atom_faculty_bust_status(void)
{
    atom_faculty_bust_status_t status = ATOM_FACULTY_BUST_IDLE;
    if (xSemaphoreTake(s_bust_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        status = s_status;
        xSemaphoreGive(s_bust_lock);
    }
    return status;
}

const char *atom_faculty_loaded_slug(void)
{
    return s_loaded_slug;
}

bool atom_faculty_draw_bust(int x, int y)
{
    bool drew = false;
    if (xSemaphoreTake(s_bust_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (s_status == ATOM_FACULTY_BUST_READY && s_bust_draw_w > 0 && s_bust_draw_h > 0) {
            atom_display_draw_rgb565(s_bust_pixels, x, y, s_bust_draw_w, s_bust_draw_h);
            drew = true;
        }
        xSemaphoreGive(s_bust_lock);
    }
    return drew;
}
