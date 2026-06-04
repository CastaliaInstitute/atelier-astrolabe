#include "atom_faculty.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_jpeg_dec.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "faculty18_board.h"
#include "atom_log.h"

#if __has_include("secrets.local.h")
#include "secrets.local.h"
#else
#include "secrets.example.h"
#endif

#ifndef MYNAH_FACULTY_BUST_ORIGIN
#define MYNAH_FACULTY_BUST_ORIGIN MYNAH_CASTALIA_WEB_ORIGIN
#endif

#define BUST_IMAGE_MAX_BYTES (384 * 1024)
#define BUST_PNG_MAX_BYTES (192 * 1024)

static const char *TAG = "atom_faculty";
#define HTTP_TIMEOUT_MS 20000

static TaskHandle_t s_bust_task;
static SemaphoreHandle_t s_bust_lock;
static char s_req_slug[64];
static char s_loaded_slug[64];
static atom_faculty_bust_status_t s_status = ATOM_FACULTY_BUST_IDLE;
static uint16_t *s_bust_pixels;
static uint8_t *s_bust_opaque;
static int s_bust_draw_w;
static int s_bust_draw_h;
static int s_bust_content_cx;
static int s_bust_content_cy;
static bool s_bust_has_visible;
static atom_faculty_ui_notify_fn s_ui_notify;

static void compute_bust_content_center(const uint8_t *opaque, int w, int h, int *out_cx, int *out_cy)
{
    if (out_cx == NULL || out_cy == NULL || opaque == NULL || w <= 0 || h <= 0) {
        return;
    }

    int min_x = w;
    int min_y = h;
    int max_x = -1;
    int max_y = -1;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (opaque[y * w + x] == 0) {
                continue;
            }
            if (x < min_x) {
                min_x = x;
            }
            if (x > max_x) {
                max_x = x;
            }
            if (y < min_y) {
                min_y = y;
            }
            if (y > max_y) {
                max_y = y;
            }
        }
    }

    if (max_x < 0) {
        *out_cx = w / 2;
        *out_cy = h / 2;
        return;
    }

    *out_cx = (min_x + max_x) / 2;
    *out_cy = (min_y + max_y) / 2;
}

static void scale_bust_rgb565(const uint16_t *src,
                              const uint8_t *src_opaque,
                              int src_w,
                              int src_h,
                              uint16_t *dst,
                              uint8_t *dst_opaque,
                              int dst_w,
                              int dst_h)
{
    if (src == NULL || src_opaque == NULL || dst == NULL || dst_opaque == NULL || src_w <= 0 || src_h <= 0 ||
        dst_w <= 0 || dst_h <= 0) {
        return;
    }

    for (int y = 0; y < dst_h; ++y) {
        const int src_y = (y * src_h) / dst_h;
        for (int x = 0; x < dst_w; ++x) {
            const int src_x = (x * src_w) / dst_w;
            const int si = src_y * src_w + src_x;
            const int di = y * dst_w + x;
            dst[di] = src[si];
            dst_opaque[di] = src_opaque[si];
        }
    }
}

static bool downscale_bust_for_display(uint16_t *pixels, uint8_t *opaque, int *io_w, int *io_h)
{
    if (pixels == NULL || opaque == NULL || io_w == NULL || io_h == NULL) {
        return false;
    }

    const int src_w = *io_w;
    const int src_h = *io_h;
    if (src_w <= 0 || src_h <= 0 || ATOM_FACULTY_BUST_DISPLAY_SCALE_PCT >= 100) {
        return true;
    }

    const int dst_w = (src_w * ATOM_FACULTY_BUST_DISPLAY_SCALE_PCT) / 100;
    const int dst_h = (src_h * ATOM_FACULTY_BUST_DISPLAY_SCALE_PCT) / 100;
    if (dst_w <= 0 || dst_h <= 0) {
        return false;
    }

    const size_t dst_px = (size_t)dst_w * (size_t)dst_h;
    uint16_t *scaled = heap_caps_malloc(dst_px * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    uint8_t *scaled_opaque = heap_caps_malloc(dst_px, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (scaled == NULL || scaled_opaque == NULL) {
        free(scaled);
        free(scaled_opaque);
        scaled = malloc(dst_px * sizeof(uint16_t));
        scaled_opaque = malloc(dst_px);
    }
    if (scaled == NULL || scaled_opaque == NULL) {
        free(scaled);
        free(scaled_opaque);
        return false;
    }

    scale_bust_rgb565(pixels, opaque, src_w, src_h, scaled, scaled_opaque, dst_w, dst_h);
    memcpy(pixels, scaled, dst_px * sizeof(uint16_t));
    memcpy(opaque, scaled_opaque, dst_px);
    free(scaled);
    free(scaled_opaque);

    *io_w = dst_w;
    *io_h = dst_h;
    return true;
}

static void trim_base_url(char *url)
{
    if (url == NULL) {
        return;
    }
    size_t len = strlen(url);
    while (len > 0 && url[len - 1] == '/') {
        url[--len] = '\0';
    }
}

static bool build_avatar_url(const char *slug, char *url, size_t cap)
{
    if (slug == NULL || slug[0] == '\0' || url == NULL || cap == 0 || strlen(MYNAH_CASTALIA_WEB_ORIGIN) == 0) {
        return false;
    }
    char base[160];
    strncpy(base, MYNAH_CASTALIA_WEB_ORIGIN, sizeof(base) - 1);
    base[sizeof(base) - 1] = '\0';
    trim_base_url(base);

    char path_slug[48];
    if (strncmp(slug, "a.", 2) == 0) {
        snprintf(path_slug, sizeof(path_slug), "a-%s", slug + 2);
    } else if (strncmp(slug, "a-", 2) == 0) {
        snprintf(path_slug, sizeof(path_slug), "%s", slug);
    } else {
        snprintf(path_slug, sizeof(path_slug), "a-%s", slug);
    }
    for (char *p = path_slug; *p != '\0'; ++p) {
        if (*p == '.' || *p == '_') {
            *p = '-';
        }
    }
    const int n = snprintf(url, cap, "%s/faculty/avatars/%s-sprite.png", base, path_slug);
    return n > 0 && (size_t)n < cap;
}

static bool build_castalia_bust_url(const char *slug, char *url, size_t cap)
{
    if (slug == NULL || slug[0] == '\0' || url == NULL || cap == 0 || strlen(MYNAH_FACULTY_BUST_ORIGIN) == 0) {
        return false;
    }
    char base[160];
    strncpy(base, MYNAH_FACULTY_BUST_ORIGIN, sizeof(base) - 1);
    base[sizeof(base) - 1] = '\0';
    trim_base_url(base);
    const int n = snprintf(url, cap, "%s/api/faculty-bust/?faculty=%s&w=%d&h=%d&q=60", base, slug,
                           ATOM_FACULTY_BUST_W, ATOM_FACULTY_BUST_H);
    return n > 0 && (size_t)n < cap;
}

static bool build_supabase_bust_url(const char *slug, char *url, size_t cap)
{
    if (slug == NULL || slug[0] == '\0' || url == NULL || cap == 0 || strlen(MYNAH_SUPABASE_URL) == 0) {
        return false;
    }
    char base[160];
    strncpy(base, MYNAH_SUPABASE_URL, sizeof(base) - 1);
    base[sizeof(base) - 1] = '\0';
    trim_base_url(base);
    const int n = snprintf(url, cap, "%s/functions/v1/faculty-bust?faculty=%s&w=%d&h=%d&format=png&resize=cover",
                           base, slug, ATOM_FACULTY_BUST_W, ATOM_FACULTY_BUST_H);
    return n > 0 && (size_t)n < cap;
}

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
        .buffer_size = 4096,
        .buffer_size_tx = 2048,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Accept", "image/png,image/jpeg");
    if (strlen(MYNAH_SUPABASE_ANON_KEY) > 0) {
        esp_http_client_set_header(client, "apikey", MYNAH_SUPABASE_ANON_KEY);
        char auth[512];
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
            if (cap >= BUST_IMAGE_MAX_BYTES) {
                free(response);
                esp_http_client_close(client);
                esp_http_client_cleanup(client);
                return ESP_ERR_NO_MEM;
            }
            size_t next = cap * 2;
            if (next > BUST_IMAGE_MAX_BYTES) {
                next = BUST_IMAGE_MAX_BYTES;
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

static bool fetch_bust_url(const char *url, const char *label, uint8_t **bytes, size_t *len, int depth)
{
    if (depth > 1 || url == NULL || bytes == NULL || len == NULL) {
        return false;
    }

    int status = 0;
    uint8_t *body = NULL;
    size_t body_len = 0;
    if (http_collect_get(url, &body, &body_len, &status) != ESP_OK || body == NULL || body_len == 0) {
        free(body);
        ESP_LOGW(TAG, "bust %s GET failed status=%d url=%s", label != NULL ? label : "url", status, url);
        return false;
    }

    if (body_len > 2 && body[0] == '{') {
        char signed_url[384];
        if (json_field((const char *)body, "url", signed_url, sizeof(signed_url))) {
            ESP_LOGW(TAG, "bust %s returned JSON redirect — use JPEG API, not raw storage PNG", label != NULL ? label : "url");
        } else {
            ESP_LOGW(TAG, "bust %s JSON response missing image (status=%d)", label != NULL ? label : "url", status);
        }
        free(body);
        return false;
    }

    *bytes = body;
    *len = body_len;
    ESP_LOGI(TAG, "bust %s fetched %u B", label != NULL ? label : "url", (unsigned)body_len);
    return true;
}

/** Squared RGB distance for bust background key (JPEG compression slack). */
#define BUST_CHROMA_THRESH_SQ 3600

static int color_dist_sq(uint8_t r0, uint8_t g0, uint8_t b0, uint8_t r1, uint8_t g1, uint8_t b1)
{
    const int dr = (int)r0 - (int)r1;
    const int dg = (int)g0 - (int)g1;
    const int db = (int)b0 - (int)b1;
    return dr * dr + dg * dg + db * db;
}

static bool chroma_key_match(uint8_t r, uint8_t g, uint8_t b, uint8_t kr, uint8_t kg, uint8_t kb)
{
    if (color_dist_sq(r, g, b, kr, kg, kb) <= BUST_CHROMA_THRESH_SQ) {
        return true;
    }
    if (color_dist_sq(r, g, b, 0, 0, 0) <= BUST_CHROMA_THRESH_SQ) {
        return true;
    }
    return false;
}

static void apply_rgb888_chroma_mask(uint8_t *opaque, int w, int h, const uint8_t *rgb, int rgb_stride)
{
    if (opaque == NULL || rgb == NULL || w <= 0 || h <= 0) {
        return;
    }

    uint8_t kr = 0;
    uint8_t kg = 0;
    uint8_t kb = 0;
    const int xs[4] = {0, w - 1, 0, w - 1};
    const int ys[4] = {0, 0, h - 1, h - 1};
    int sr = 0;
    int sg = 0;
    int sb = 0;
    for (int i = 0; i < 4; ++i) {
        const uint8_t *px = rgb + ((size_t)ys[i] * (size_t)w + (size_t)xs[i]) * (size_t)rgb_stride;
        sr += px[0];
        sg += px[1];
        sb += px[2];
    }
    kr = (uint8_t)(sr / 4);
    kg = (uint8_t)(sg / 4);
    kb = (uint8_t)(sb / 4);

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const uint8_t *px = rgb + ((size_t)y * (size_t)w + (size_t)x) * (size_t)rgb_stride;
            const int i = y * w + x;
            opaque[i] = chroma_key_match(px[0], px[1], px[2], kr, kg, kb) ? 0 : 255;
        }
    }
}

static bool decode_png_rgb565(const uint8_t *png, size_t png_len, uint16_t *out, uint8_t *opaque, int *out_w, int *out_h)
{
    return atom_faculty_png_decode(png, png_len, out, opaque, out_w, out_h);
}

static bool decode_jpeg_rgb565(const uint8_t *jpeg, size_t jpeg_len, uint16_t *out, uint8_t *opaque, int *out_w, int *out_h)
{
    if (jpeg == NULL || jpeg_len < 64 || out == NULL || opaque == NULL) {
        return false;
    }

    jpeg_dec_config_t config = DEFAULT_JPEG_DEC_CONFIG();
    config.output_type = JPEG_PIXEL_FORMAT_RGB888;

    jpeg_dec_handle_t jpeg_dec = NULL;
    if (jpeg_dec_open(&config, &jpeg_dec) != JPEG_ERR_OK || jpeg_dec == NULL) {
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

    const uint8_t *jpeg_base = jpeg;
    jpeg_io->inbuf = (uint8_t *)jpeg;
    jpeg_io->inbuf_len = (int)jpeg_len;
    jpeg_error_t jret = jpeg_dec_parse_header(jpeg_dec, jpeg_io, jpeg_info);
    if (jret != JPEG_ERR_OK) {
        ESP_LOGW(TAG, "JPEG header parse failed: %d", (int)jret);
        free(jpeg_io);
        free(jpeg_info);
        jpeg_dec_close(jpeg_dec);
        return false;
    }

    const int consumed = jpeg_io->inbuf_len - jpeg_io->inbuf_remain;
    jpeg_io->inbuf = (uint8_t *)(jpeg_base + consumed);
    jpeg_io->inbuf_len = jpeg_io->inbuf_remain;

    const int decoded_w = jpeg_info->width;
    const int decoded_h = jpeg_info->height;
    if (decoded_w <= 0 || decoded_h <= 0 || decoded_w > ATOM_FACULTY_BUST_W || decoded_h > ATOM_FACULTY_BUST_H) {
        ESP_LOGW(TAG, "JPEG size out of range: %dx%d", decoded_w, decoded_h);
        free(jpeg_io);
        free(jpeg_info);
        jpeg_dec_close(jpeg_dec);
        return false;
    }

    const size_t native_px = (size_t)decoded_w * (size_t)decoded_h;
    uint8_t *native = heap_caps_aligned_alloc(16, native_px * 3, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (native == NULL) {
        native = heap_caps_aligned_alloc(16, native_px * 3, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (native == NULL) {
        free(jpeg_io);
        free(jpeg_info);
        jpeg_dec_close(jpeg_dec);
        return false;
    }

    int outbuf_len = 0;
    if (jpeg_dec_get_outbuf_len(jpeg_dec, &outbuf_len) != JPEG_ERR_OK || outbuf_len <= 0) {
        heap_caps_free(native);
        free(jpeg_io);
        free(jpeg_info);
        jpeg_dec_close(jpeg_dec);
        return false;
    }

    jpeg_io->outbuf = native;
    jpeg_io->out_size = outbuf_len;
    jret = jpeg_dec_process(jpeg_dec, jpeg_io);
    jpeg_dec_close(jpeg_dec);
    free(jpeg_io);
    free(jpeg_info);
    if (jret != JPEG_ERR_OK) {
        ESP_LOGW(TAG, "JPEG decode failed: %d", (int)jret);
        heap_caps_free(native);
        return false;
    }

    const size_t bust_px = (size_t)ATOM_FACULTY_BUST_W * (size_t)ATOM_FACULTY_BUST_H;
    uint8_t *bust_rgb = heap_caps_malloc(bust_px * 3, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (bust_rgb == NULL) {
        bust_rgb = malloc(bust_px * 3);
    }
    if (bust_rgb == NULL) {
        heap_caps_free(native);
        return false;
    }

    for (int y = 0; y < ATOM_FACULTY_BUST_H; ++y) {
        const int sy = (ATOM_FACULTY_BUST_H == 1) ? 0 : (y * (decoded_h - 1)) / (ATOM_FACULTY_BUST_H - 1);
        for (int x = 0; x < ATOM_FACULTY_BUST_W; ++x) {
            const int sx = (ATOM_FACULTY_BUST_W == 1) ? 0 : (x * (decoded_w - 1)) / (ATOM_FACULTY_BUST_W - 1);
            const uint8_t *px = native + ((size_t)sy * (size_t)decoded_w + (size_t)sx) * 3;
            const size_t di = ((size_t)y * (size_t)ATOM_FACULTY_BUST_W + (size_t)x) * 3;
            bust_rgb[di + 0] = px[0];
            bust_rgb[di + 1] = px[1];
            bust_rgb[di + 2] = px[2];
        }
    }
    heap_caps_free(native);

    apply_rgb888_chroma_mask(opaque, ATOM_FACULTY_BUST_W, ATOM_FACULTY_BUST_H, bust_rgb, 3);

    for (int i = 0; i < ATOM_FACULTY_BUST_W * ATOM_FACULTY_BUST_H; ++i) {
        const uint8_t *px = bust_rgb + (size_t)i * 3;
        out[i] = faculty18_display_rgb888(px[0], px[1], px[2]);
    }
    heap_caps_free(bust_rgb);

    if (out_w != NULL) {
        *out_w = ATOM_FACULTY_BUST_W;
    }
    if (out_h != NULL) {
        *out_h = ATOM_FACULTY_BUST_H;
    }
    return true;
}

static bool decode_bust_rgb565(const uint8_t *bytes, size_t len, uint16_t *out, uint8_t *opaque, int *out_w, int *out_h)
{
    if (bytes == NULL || len < 8 || out == NULL || opaque == NULL) {
        return false;
    }
    if (bytes[0] == 0x89 && bytes[1] == 'P') {
        if (len > BUST_PNG_MAX_BYTES) {
            ESP_LOGW(TAG, "PNG too large for device decode (%u B)", (unsigned)len);
            return false;
        }
        return decode_png_rgb565(bytes, len, out, opaque, out_w, out_h);
    }
    if (bytes[0] == 0xFF && bytes[1] == 0xD8) {
        return decode_jpeg_rgb565(bytes, len, out, opaque, out_w, out_h);
    }
    ESP_LOGW(TAG, "unknown bust image format (first bytes %02x %02x)", bytes[0], bytes[1]);
    return false;
}

static bool fetch_bust_bytes(const char *slug, uint8_t **bytes, size_t *len, const char **source_out)
{
    char url[256];

    if (build_supabase_bust_url(slug, url, sizeof(url)) && fetch_bust_url(url, "supabase", bytes, len, 0)) {
        if (source_out != NULL) {
            *source_out = "supabase";
        }
        return true;
    }
    if (build_castalia_bust_url(slug, url, sizeof(url)) && fetch_bust_url(url, "castalia", bytes, len, 0)) {
        if (source_out != NULL) {
            *source_out = "castalia";
        }
        return true;
    }
    if (build_avatar_url(slug, url, sizeof(url)) && fetch_bust_url(url, "avatar", bytes, len, 0)) {
        if (source_out != NULL) {
            *source_out = "avatar";
        }
        return true;
    }
    return false;
}

static bool fetch_and_decode_bust(const char *slug)
{
    if (slug == NULL || slug[0] == '\0') {
        return false;
    }

    uint8_t *image = NULL;
    size_t image_len = 0;
    const char *source = NULL;
    if (!fetch_bust_bytes(slug, &image, &image_len, &source)) {
        return false;
    }

    uint16_t *scratch = s_bust_pixels;
    uint8_t *opaque = s_bust_opaque;
    if (scratch == NULL || opaque == NULL) {
        free(image);
        return false;
    }

    int draw_w = 0;
    int draw_h = 0;
    const size_t fetched_len = image_len;
    const bool ok = decode_bust_rgb565(image, image_len, scratch, opaque, &draw_w, &draw_h);
    free(image);
    if (!ok || draw_w <= 0 || draw_h <= 0) {
        return false;
    }

    if (!downscale_bust_for_display(scratch, opaque, &draw_w, &draw_h)) {
        ESP_LOGW(TAG, "bust downscale skipped — using decoded size %dx%d", draw_w, draw_h);
    }

    int content_cx = 0;
    int content_cy = 0;
    compute_bust_content_center(opaque, draw_w, draw_h, &content_cx, &content_cy);

    bool has_visible = false;
    for (size_t i = 0, n = (size_t)draw_w * (size_t)draw_h; i < n; ++i) {
        if (opaque[i] != 0) {
            has_visible = true;
            break;
        }
    }

    if (xSemaphoreTake(s_bust_lock, pdMS_TO_TICKS(2000)) == pdTRUE) {
        s_bust_draw_w = draw_w;
        s_bust_draw_h = draw_h;
        s_bust_content_cx = content_cx;
        s_bust_content_cy = content_cy;
        s_bust_has_visible = has_visible;
        strncpy(s_loaded_slug, slug, sizeof(s_loaded_slug) - 1);
        s_loaded_slug[sizeof(s_loaded_slug) - 1] = '\0';
        s_status = ATOM_FACULTY_BUST_READY;
        xSemaphoreGive(s_bust_lock);
    }
    ESP_LOGI(TAG, "bust ready %s via %s (%dx%d, %u B)", slug, source != NULL ? source : "?", draw_w, draw_h,
             (unsigned)fetched_len);
    ATOM_LOG_STAGE(TAG, "faculty", "bust ready %s via %s (%dx%d)", slug, source != NULL ? source : "?", draw_w, draw_h);
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

        ATOM_LOG_STAGE(TAG, "faculty", "bust loading %s", slug);

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
            ATOM_LOG_STAGE_W(TAG, "faculty", "bust fetch failed for %s", slug);
        }

        if (s_ui_notify != NULL) {
            s_ui_notify();
        }
    }
}

esp_err_t atom_faculty_init(void)
{
    const size_t px_count = (size_t)ATOM_FACULTY_BUST_W * (size_t)ATOM_FACULTY_BUST_H;
    s_bust_pixels = heap_caps_malloc(px_count * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_bust_opaque = heap_caps_malloc(px_count, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_bust_pixels == NULL || s_bust_opaque == NULL) {
        free(s_bust_pixels);
        free(s_bust_opaque);
        s_bust_pixels = NULL;
        s_bust_opaque = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_bust_lock = xSemaphoreCreateMutex();
    if (s_bust_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }
    s_loaded_slug[0] = '\0';
    s_req_slug[0] = '\0';
    s_bust_draw_w = 0;
    s_bust_draw_h = 0;
    s_bust_has_visible = false;
    if (xTaskCreate(bust_worker_task, "fac_bust", 32768, NULL, 3, &s_bust_task) != pdPASS) {
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

bool atom_faculty_bust_draw_size(int *w, int *h)
{
    if (w == NULL || h == NULL || s_bust_lock == NULL) {
        return false;
    }
    bool ok = false;
    if (xSemaphoreTake(s_bust_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (s_status == ATOM_FACULTY_BUST_READY && s_bust_draw_w > 0 && s_bust_draw_h > 0) {
            *w = s_bust_draw_w;
            *h = s_bust_draw_h;
            ok = true;
        }
        xSemaphoreGive(s_bust_lock);
    }
    return ok;
}

bool atom_faculty_bust_content_center(int *cx, int *cy)
{
    if (cx == NULL || cy == NULL || s_bust_lock == NULL) {
        return false;
    }
    bool ok = false;
    if (xSemaphoreTake(s_bust_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (s_status == ATOM_FACULTY_BUST_READY && s_bust_draw_w > 0 && s_bust_draw_h > 0) {
            *cx = s_bust_content_cx;
            *cy = s_bust_content_cy;
            ok = true;
        }
        xSemaphoreGive(s_bust_lock);
    }
    return ok;
}

void atom_faculty_bust_blit_origin(int area_x,
                                   int area_y,
                                   int area_w,
                                   int area_h,
                                   bool panel_rotated_ccw,
                                   int *out_x,
                                   int *out_y)
{
    if (out_x == NULL || out_y == NULL || area_w <= 0 || area_h <= 0) {
        return;
    }

    int draw_w = 0;
    int draw_h = 0;
    if (!atom_faculty_bust_draw_size(&draw_w, &draw_h)) {
        *out_x = area_x;
        *out_y = area_y;
        return;
    }

    int bcx = draw_w / 2;
    int bcy = draw_h / 2;
    (void)atom_faculty_bust_content_center(&bcx, &bcy);

    const int panel_cx = area_w / 2;
    const int panel_cy = area_h / 2;
    if (panel_rotated_ccw) {
        /* panel px = fb y; panel py = area_w - 1 - fb x — center opaque content on panel */
        *out_x = area_x + (area_w - 1 - panel_cy - bcx);
        *out_y = area_y + (panel_cx - bcy) + ATOM_FACULTY_BUST_DISPLAY_Y_NUDGE;
    } else {
        *out_x = area_x + (panel_cx - bcx);
        *out_y = area_y + (panel_cy - bcy) + ATOM_FACULTY_BUST_DISPLAY_Y_NUDGE;
    }
}

bool atom_faculty_draw_bust(int x, int y)
{
    if (s_bust_lock == NULL || s_bust_pixels == NULL || s_bust_opaque == NULL) {
        return false;
    }
    bool drew = false;
    if (xSemaphoreTake(s_bust_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (s_status == ATOM_FACULTY_BUST_READY && s_bust_has_visible && s_bust_draw_w > 0 && s_bust_draw_h > 0) {
            const int pixels =
                faculty18_display_blit_rgb565_masked(s_bust_pixels, s_bust_opaque, x, y, s_bust_draw_w, s_bust_draw_h);
            if (pixels > 0) {
                ESP_LOGI(TAG, "bust blit %dx%d at (%d,%d) px=%d", s_bust_draw_w, s_bust_draw_h, x, y, pixels);
                drew = true;
            } else {
                ESP_LOGW(TAG, "bust ready but blit clipped (%d,%d)", x, y);
            }
        }
        xSemaphoreGive(s_bust_lock);
    }
    return drew;
}
