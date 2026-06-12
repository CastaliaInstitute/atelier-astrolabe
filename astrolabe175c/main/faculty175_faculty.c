#include "faculty175_faculty.h"

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

#include "faculty175_faculty_roster.h"
#include "faculty175_board.h"
#include "faculty175_log.h"
#include "faculty175_storage.h"

#define STBI_NO_STDIO
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#if __has_include("secrets.local.h")
#include "secrets.local.h"
#else
#include "secrets.example.h"
#endif

#ifndef MYNAH_FACULTY_BUST_ORIGIN
#define MYNAH_FACULTY_BUST_ORIGIN MYNAH_CASTALIA_WEB_ORIGIN
#endif

#define BUST_IMAGE_MAX_BYTES (768 * 1024)
#define BUST_PNG_MAX_DIM 1024
#define BUST_CACHE_VERSION "v5"

static const char *TAG = "faculty175_faculty";
#define HTTP_TIMEOUT_MS 20000
#define FACULTY175_FACULTY_BUST_TASK_STACK 12288
#define FACULTY175_FACULTY_PREFETCH_TASK_STACK 8192

static TaskHandle_t s_bust_task;
static TaskHandle_t s_prefetch_task;
static SemaphoreHandle_t s_bust_lock;
static char s_req_slug[64];
static char s_loaded_slug[64];
static faculty175_faculty_bust_status_t s_status = FACULTY175_FACULTY_BUST_IDLE;
static uint16_t *s_bust_pixels;
static uint8_t *s_bust_opaque;
static int s_bust_draw_w;
static int s_bust_draw_h;
static int s_bust_content_cx;
static int s_bust_content_cy;
static faculty175_faculty_ui_notify_fn s_ui_notify;
static bool s_bust_cache_ready;
static bool s_bust_cache_checked;
static volatile bool s_network_fetch_enabled;
static uint32_t s_bust_task_retry_after_ms;

static bool bust_cache_init(void);
static bool bust_cache_path(const char *slug, char *path, size_t cap);

extern const uint8_t _binary_v5_a_darwin_right_png_start[] asm("_binary_v5_a_darwin_right_png_start");
extern const uint8_t _binary_v5_a_darwin_right_png_end[] asm("_binary_v5_a_darwin_right_png_end");
extern const uint8_t _binary_v5_a_plato_right_png_start[] asm("_binary_v5_a_plato_right_png_start");
extern const uint8_t _binary_v5_a_plato_right_png_end[] asm("_binary_v5_a_plato_right_png_end");

#ifndef ASTROLABE_FACULTY_PREFETCH_ROSTER
#define ASTROLABE_FACULTY_PREFETCH_ROSTER 1
#endif

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

static void compute_bust_content_bounds(const uint8_t *opaque,
                                        int w,
                                        int h,
                                        int *out_min_x,
                                        int *out_min_y,
                                        int *out_max_x,
                                        int *out_max_y)
{
    if (out_min_x == NULL || out_min_y == NULL || out_max_x == NULL || out_max_y == NULL) {
        return;
    }
    *out_min_x = w;
    *out_min_y = h;
    *out_max_x = -1;
    *out_max_y = -1;
    if (opaque == NULL || w <= 0 || h <= 0) {
        return;
    }
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (opaque[y * w + x] < 128) {
                continue;
            }
            if (x < *out_min_x) {
                *out_min_x = x;
            }
            if (x > *out_max_x) {
                *out_max_x = x;
            }
            if (y < *out_min_y) {
                *out_min_y = y;
            }
            if (y > *out_max_y) {
                *out_max_y = y;
            }
        }
    }
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
    if (src_w <= 0 || src_h <= 0 || FACULTY175_FACULTY_BUST_DISPLAY_SCALE_PCT >= 100) {
        return true;
    }

    const int dst_w = (src_w * FACULTY175_FACULTY_BUST_DISPLAY_SCALE_PCT) / 100;
    const int dst_h = (src_h * FACULTY175_FACULTY_BUST_DISPLAY_SCALE_PCT) / 100;
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
                           FACULTY175_FACULTY_BUST_W, FACULTY175_FACULTY_BUST_H);
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
    const int n = snprintf(url, cap,
                           "%s/functions/v1/faculty-bust?handle=%s&w=%d&h=%d&q=60&resize=cover&view=right&format=png&transparent=1",
                           base, slug, FACULTY175_FACULTY_BUST_W, FACULTY175_FACULTY_BUST_H);
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

static bool http_stream_get_to_file(const char *url,
                                    const char *path,
                                    int *out_status,
                                    size_t *out_len,
                                    bool *out_json,
                                    char *json_buf,
                                    size_t json_cap)
{
    if (url == NULL || path == NULL) {
        return false;
    }
    if (out_status != NULL) {
        *out_status = 0;
    }
    if (out_len != NULL) {
        *out_len = 0;
    }
    if (out_json != NULL) {
        *out_json = false;
    }
    if (json_buf != NULL && json_cap > 0) {
        json_buf[0] = '\0';
    }

    int status = 0;
    size_t total = 0;
    uint8_t first[256];
    size_t first_len = 0;
    bool treat_as_json = false;

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
        return false;
    }

    esp_http_client_set_header(client, "Accept", "image/png,image/jpeg,application/json,*/*;q=0.8");
    if (strlen(MYNAH_SUPABASE_ANON_KEY) > 0) {
        esp_http_client_set_header(client, "apikey", MYNAH_SUPABASE_ANON_KEY);
        char auth[512];
        snprintf(auth, sizeof(auth), "Bearer %s", MYNAH_SUPABASE_ANON_KEY);
        esp_http_client_set_header(client, "Authorization", auth);
    }

    esp_err_t ret = esp_http_client_open(client, 0);
    if (ret != ESP_OK) {
        esp_http_client_cleanup(client);
        return false;
    }

    (void)esp_http_client_fetch_headers(client);
    status = esp_http_client_get_status_code(client);
    if (out_status != NULL) {
        *out_status = status;
    }
    if (status < 200 || status >= 300) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    while (first_len == 0) {
        const int rd = esp_http_client_read(client, (char *)first, sizeof(first));
        if (rd < 0) {
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return false;
        }
        if (rd == 0) {
            break;
        }
        first_len = (size_t)rd;
        total += first_len;
    }

    if (first_len == 0) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    size_t sig = 0;
    while (sig < first_len && (first[sig] == ' ' || first[sig] == '\n' || first[sig] == '\r' || first[sig] == '\t')) {
        ++sig;
    }
    treat_as_json = sig < first_len && first[sig] == '{';
    if (treat_as_json) {
        if (json_buf == NULL || json_cap < first_len + 1) {
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return false;
        }
        memcpy(json_buf, first, first_len);
        size_t json_len = first_len;
        while (true) {
            const int rd = esp_http_client_read(client, json_buf + json_len, (int)(json_cap - json_len - 1));
            if (rd < 0) {
                esp_http_client_close(client);
                esp_http_client_cleanup(client);
                return false;
            }
            if (rd == 0) {
                break;
            }
            json_len += (size_t)rd;
            total += (size_t)rd;
            if (json_len + 1 >= json_cap) {
                esp_http_client_close(client);
                esp_http_client_cleanup(client);
                return false;
            }
        }
        json_buf[json_len] = '\0';
        if (out_json != NULL) {
            *out_json = true;
        }
        if (out_len != NULL) {
            *out_len = json_len;
        }
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return true;
    }

    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    const bool png = first_len > 1 && first[0] == 0x89 && first[1] == 'P';
    const bool jpg = first_len > 1 && first[0] == 0xFF && first[1] == 0xD8;
    if (!png && !jpg) {
        fclose(f);
        (void)remove(path);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    if (fwrite(first, 1, first_len, f) != first_len) {
        fclose(f);
        (void)remove(path);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    uint8_t buf[1024];
    while (true) {
        const int rd = esp_http_client_read(client, (char *)buf, sizeof(buf));
        if (rd < 0) {
            fclose(f);
            (void)remove(path);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return false;
        }
        if (rd == 0) {
            break;
        }
        total += (size_t)rd;
        if (total > BUST_IMAGE_MAX_BYTES || fwrite(buf, 1, (size_t)rd, f) != (size_t)rd) {
            fclose(f);
            (void)remove(path);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return false;
        }
    }
    fclose(f);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    if (out_len != NULL) {
        *out_len = total;
    }
    return true;
}

static bool fetch_bust_url_to_flash(const char *url, const char *label, const char *slug, int depth)
{
    if (depth > 1 || url == NULL || slug == NULL || slug[0] == '\0' || !bust_cache_init()) {
        return false;
    }

    char final_path[80];
    char temp_path[96];
    char signed_json[1024];
    if (!bust_cache_path(slug, final_path, sizeof(final_path))) {
        return false;
    }
    const int temp_n = snprintf(temp_path, sizeof(temp_path), "%s.part", final_path);
    if (temp_n <= 0 || (size_t)temp_n >= sizeof(temp_path)) {
        return false;
    }

    int status = 0;
    size_t fetched_len = 0;
    bool is_json = false;
    if (!http_stream_get_to_file(url, temp_path, &status, &fetched_len, &is_json, signed_json, sizeof(signed_json))) {
        (void)remove(temp_path);
        ESP_LOGW(TAG, "bust %s GET failed status=%d url=%s", label != NULL ? label : "url", status, url);
        return false;
    }

    if (is_json) {
        char signed_url[384];
        if (!json_field(signed_json, "url", signed_url, sizeof(signed_url))) {
            return false;
        }
        return fetch_bust_url_to_flash(signed_url, "signed", slug, depth + 1);
    }

    (void)remove(final_path);
    if (rename(temp_path, final_path) != 0) {
        (void)remove(temp_path);
        return false;
    }
    ESP_LOGI(TAG, "bust %s streamed to flash %s (%u B)", label != NULL ? label : "url", slug, (unsigned)fetched_len);
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

/** Drop fringe/letterbox pixels for non-alpha busts. PNG alpha is trusted. */
static bool bust_pixel_visible(uint16_t col, uint8_t alpha)
{
    if (alpha < 128) {
        return false;
    }
    if (col == 0) {
        return false;
    }
    const uint8_t r = (uint8_t)((col >> 11) << 3);
    const uint8_t g = (uint8_t)(((col >> 5) & 0x3f) << 2);
    const uint8_t b = (uint8_t)((col & 0x1f) << 3);
    if (r < 12 && g < 12 && b < 12) {
        return false;
    }
    /* Indexed-PNG transparent palette spill (e.g. green fringe at alpha 0). */
    if (g > r + 24 && g > b + 24 && g > 72) {
        return false;
    }
    return true;
}

static bool bust_pixel_dark(uint16_t col)
{
    const uint8_t r = (uint8_t)((col >> 11) << 3);
    const uint8_t g = (uint8_t)(((col >> 5) & 0x3f) << 2);
    const uint8_t b = (uint8_t)((col & 0x1f) << 3);
    return r < 24 && g < 24 && b < 24;
}

static bool bust_pixel_green_spill(uint16_t col)
{
    const uint8_t r = (uint8_t)((col >> 11) << 3);
    const uint8_t g = (uint8_t)(((col >> 5) & 0x3f) << 2);
    const uint8_t b = (uint8_t)((col & 0x1f) << 3);
    return g > r + 24 && g > b + 24 && g > 72;
}

static uint16_t bust_rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((uint16_t)(r >> 3) << 11) | ((uint16_t)(g >> 2) << 5) | (uint16_t)(b >> 3));
}

static void bust_finalize_opacity(uint16_t *pixels, uint8_t *opaque, int w, int h)
{
    if (pixels == NULL || opaque == NULL || w <= 0 || h <= 0) {
        return;
    }
    const int n = w * h;
    int kept = 0;
    int alpha_visible = 0;
    for (int i = 0; i < n; ++i) {
        if (opaque[i] >= 128) {
            alpha_visible++;
        }
        if (bust_pixel_visible(pixels[i], opaque[i])) {
            kept++;
        }
    }

    const bool tint_dark_alpha = kept == 0 && alpha_visible > 0 && alpha_visible <= (n * 9) / 10;
    const uint16_t ink = bust_rgb565(224, 232, 240);
    for (int i = 0; i < n; ++i) {
        if (bust_pixel_visible(pixels[i], opaque[i])) {
            continue;
        }
        if (tint_dark_alpha && opaque[i] >= 128 && !bust_pixel_green_spill(pixels[i]) && bust_pixel_dark(pixels[i])) {
            pixels[i] = ink;
            opaque[i] = 255;
            continue;
        }
        opaque[i] = 0;
    }

    if (!tint_dark_alpha) {
        return;
    }

    ESP_LOGI(TAG, "bust dark alpha restored (%d alpha px)", alpha_visible);
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
    if (png == NULL || png_len < 8 || out == NULL || opaque == NULL || png_len > (size_t)INT_MAX) {
        return false;
    }

    int decoded_w = 0;
    int decoded_h = 0;
    int comp = 0;
    uint8_t *rgba = stbi_load_from_memory(png, (int)png_len, &decoded_w, &decoded_h, &comp, 4);
    if (rgba == NULL) {
        ESP_LOGW(TAG, "STB PNG decode failed: %s", stbi_failure_reason());
        return faculty175_faculty_png_decode(png, png_len, out, opaque, out_w, out_h);
    }
    if (decoded_w <= 0 || decoded_h <= 0 || decoded_w > BUST_PNG_MAX_DIM || decoded_h > BUST_PNG_MAX_DIM) {
        ESP_LOGW(TAG, "PNG size out of range: %dx%d", decoded_w, decoded_h);
        stbi_image_free(rgba);
        return false;
    }

    bool any_transparent = false;
    bool any_soft_alpha = false;
    for (int y = 0; y < FACULTY175_FACULTY_BUST_H; ++y) {
        const int sy = (FACULTY175_FACULTY_BUST_H == 1) ? 0 : (y * (decoded_h - 1)) / (FACULTY175_FACULTY_BUST_H - 1);
        for (int x = 0; x < FACULTY175_FACULTY_BUST_W; ++x) {
            const int sx = (FACULTY175_FACULTY_BUST_W == 1) ? 0 : (x * (decoded_w - 1)) / (FACULTY175_FACULTY_BUST_W - 1);
            const uint8_t *px = rgba + ((size_t)sy * (size_t)decoded_w + (size_t)sx) * 4;
            const size_t i = (size_t)y * (size_t)FACULTY175_FACULTY_BUST_W + (size_t)x;
            opaque[i] = px[3];
            any_transparent = any_transparent || px[3] < 250;
            any_soft_alpha = any_soft_alpha || (px[3] > 0 && px[3] < 250);
            out[i] = faculty175_display_rgb888(px[0], px[1], px[2]);
        }
    }
    stbi_image_free(rgba);

    if (!any_transparent) {
        ESP_LOGW(TAG, "PNG bust has no alpha; keeping opaque image (Castalia should return transparent PNG)");
    } else if (!any_soft_alpha) {
        ESP_LOGI(TAG, "PNG bust alpha mask preserved");
    } else {
        ESP_LOGI(TAG, "PNG bust soft alpha preserved");
    }

    if (out_w != NULL) {
        *out_w = FACULTY175_FACULTY_BUST_W;
    }
    if (out_h != NULL) {
        *out_h = FACULTY175_FACULTY_BUST_H;
    }
    (void)comp;
    return true;
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
    if (decoded_w <= 0 || decoded_h <= 0 || decoded_w > BUST_PNG_MAX_DIM || decoded_h > BUST_PNG_MAX_DIM) {
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

    const size_t bust_px = (size_t)FACULTY175_FACULTY_BUST_W * (size_t)FACULTY175_FACULTY_BUST_H;
    uint8_t *bust_rgb = heap_caps_malloc(bust_px * 3, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (bust_rgb == NULL) {
        bust_rgb = malloc(bust_px * 3);
    }
    if (bust_rgb == NULL) {
        heap_caps_free(native);
        return false;
    }

    for (int y = 0; y < FACULTY175_FACULTY_BUST_H; ++y) {
        const int sy = (FACULTY175_FACULTY_BUST_H == 1) ? 0 : (y * (decoded_h - 1)) / (FACULTY175_FACULTY_BUST_H - 1);
        for (int x = 0; x < FACULTY175_FACULTY_BUST_W; ++x) {
            const int sx = (FACULTY175_FACULTY_BUST_W == 1) ? 0 : (x * (decoded_w - 1)) / (FACULTY175_FACULTY_BUST_W - 1);
            const uint8_t *px = native + ((size_t)sy * (size_t)decoded_w + (size_t)sx) * 3;
            const size_t di = ((size_t)y * (size_t)FACULTY175_FACULTY_BUST_W + (size_t)x) * 3;
            bust_rgb[di + 0] = px[0];
            bust_rgb[di + 1] = px[1];
            bust_rgb[di + 2] = px[2];
        }
    }
    heap_caps_free(native);

    apply_rgb888_chroma_mask(opaque, FACULTY175_FACULTY_BUST_W, FACULTY175_FACULTY_BUST_H, bust_rgb, 3);

    for (int i = 0; i < FACULTY175_FACULTY_BUST_W * FACULTY175_FACULTY_BUST_H; ++i) {
        const uint8_t *px = bust_rgb + (size_t)i * 3;
        out[i] = faculty175_display_rgb888(px[0], px[1], px[2]);
    }
    heap_caps_free(bust_rgb);

    if (out_w != NULL) {
        *out_w = FACULTY175_FACULTY_BUST_W;
    }
    if (out_h != NULL) {
        *out_h = FACULTY175_FACULTY_BUST_H;
    }
    return true;
}

static bool decode_bust_rgb565(const uint8_t *bytes, size_t len, uint16_t *out, uint8_t *opaque, int *out_w, int *out_h)
{
    if (bytes == NULL || len < 8 || out == NULL || opaque == NULL) {
        return false;
    }
    if (bytes[0] == 0x89 && bytes[1] == 'P') {
        return decode_png_rgb565(bytes, len, out, opaque, out_w, out_h);
    }
    if (bytes[0] == 0xFF && bytes[1] == 0xD8) {
        return decode_jpeg_rgb565(bytes, len, out, opaque, out_w, out_h);
    }
    ESP_LOGW(TAG, "unknown bust image format (first bytes %02x %02x)", bytes[0], bytes[1]);
    return false;
}

static bool bust_bytes_are_png(const uint8_t *bytes, size_t len)
{
    return bytes != NULL && len >= 8 && bytes[0] == 0x89 && bytes[1] == 'P';
}

static bool bust_cache_init(void)
{
    if (s_bust_cache_checked) {
        return s_bust_cache_ready;
    }
    s_bust_cache_checked = true;
    const esp_err_t err = faculty175_storage_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "bust FAT init failed: %s", esp_err_to_name(err));
        return false;
    }
    s_bust_cache_ready = true;
    ESP_LOGI(TAG, "bust media FAT ready");
    return true;
}

static bool bust_cache_path(const char *slug, char *path, size_t cap)
{
    if (slug == NULL || slug[0] == '\0' || path == NULL || cap == 0) {
        return false;
    }
    const char *base = faculty175_storage_media_base_path();
    if (base == NULL || base[0] == '\0') {
        return false;
    }
    const int n = snprintf(path, cap, "%s/bust_cache/%s-%s-right.png", base, BUST_CACHE_VERSION, slug);
    return n > 0 && (size_t)n < cap;
}

static void bust_cache_remove(const char *slug)
{
    char path[80];
    if (!bust_cache_path(slug, path, sizeof(path)) || !bust_cache_init()) {
        return;
    }
    if (remove(path) == 0) {
        ESP_LOGI(TAG, "bust flash evicted %s", slug);
    }
}

static bool bust_load_from_flash(const char *slug, uint8_t **bytes, size_t *len)
{
    if (bytes == NULL || len == NULL || slug == NULL || slug[0] == '\0') {
        return false;
    }
    *bytes = NULL;
    *len = 0;

    char path[80];
    if (!bust_cache_path(slug, path, sizeof(path)) || !bust_cache_init()) {
        return false;
    }

    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return false;
    }
    fseek(f, 0, SEEK_END);
    const long sz = ftell(f);
    if (sz <= 0 || (size_t)sz > BUST_IMAGE_MAX_BYTES) {
        fclose(f);
        return false;
    }
    fseek(f, 0, SEEK_SET);

    uint8_t *buf = heap_caps_malloc((size_t)sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buf == NULL) {
        buf = malloc((size_t)sz);
    }
    if (buf == NULL) {
        fclose(f);
        return false;
    }
    const size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (rd != (size_t)sz) {
        free(buf);
        return false;
    }
    const bool png = buf[0] == 0x89 && buf[1] == 'P';
    const bool jpg = buf[0] == 0xFF && buf[1] == 0xD8;
    if (!png && !jpg) {
        free(buf);
        (void)remove(path);
        return false;
    }

    *bytes = buf;
    *len = (size_t)sz;
    ESP_LOGI(TAG, "bust flash hit %s (%u B)", slug, (unsigned)*len);
    return true;
}

static bool bust_load_from_embedded(const char *slug, const uint8_t **bytes, size_t *len, const char **source_out)
{
    if (slug == NULL || bytes == NULL || len == NULL) {
        return false;
    }
    const uint8_t *start = NULL;
    const uint8_t *end = NULL;
    const char *source = NULL;

    if (strcmp(slug, "a.plato") == 0 || strcmp(slug, "plato") == 0) {
        start = _binary_v5_a_plato_right_png_start;
        end = _binary_v5_a_plato_right_png_end;
        source = "embedded-plato";
    } else {
        start = _binary_v5_a_darwin_right_png_start;
        end = _binary_v5_a_darwin_right_png_end;
        source = strcmp(slug, "a.darwin") == 0 || strcmp(slug, "darwin") == 0 ? "embedded-darwin" : "embedded-darwin-fallback";
    }

    if (start == NULL || end == NULL || end <= start) {
        return false;
    }
    *bytes = start;
    *len = (size_t)(end - start);
    if (source_out != NULL) {
        *source_out = source;
    }
    return true;
}

#if ASTROLABE_FACULTY_PREFETCH_ROSTER
static bool bust_flash_cached(const char *slug)
{
    char path[80];
    if (!bust_cache_path(slug, path, sizeof(path)) || !bust_cache_init()) {
        return false;
    }
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return false;
    }
    fseek(f, 0, SEEK_END);
    const long sz = ftell(f);
    fclose(f);
    return sz > 0 && (size_t)sz <= BUST_IMAGE_MAX_BYTES;
}
#endif

static bool fetch_supabase_storage_right_bust(const char *slug)
{
    if (slug == NULL || slug[0] == '\0' || strlen(MYNAH_SUPABASE_URL) == 0) {
        return false;
    }

    char base[160];
    strncpy(base, MYNAH_SUPABASE_URL, sizeof(base) - 1);
    base[sizeof(base) - 1] = '\0';
    trim_base_url(base);

    static const char *kLeaves[] = {"bust.png", "bust.webp", "bust.jpg", "bust.jpeg"};
    char url[320];
    char object_path[96];
    const char *slug_candidates[4];
    int slug_n = 0;
    slug_candidates[slug_n++] = slug;
    if (strncmp(slug, "a.", 2) == 0 && slug_n < 4) {
        slug_candidates[slug_n++] = slug + 2;
    }

    for (int si = 0; si < slug_n; ++si) {
        for (size_t li = 0; li < sizeof(kLeaves) / sizeof(kLeaves[0]); ++li) {
            if (snprintf(object_path, sizeof(object_path), "%s/%s", slug_candidates[si], kLeaves[li]) <= 0) {
                continue;
            }
            if (snprintf(url, sizeof(url), "%s/storage/v1/object/public/busts/%s", base, object_path) <= 0) {
                continue;
            }
            if (fetch_bust_url_to_flash(url, "storage-right", slug, 0)) {
                return true;
            }
        }
    }
    return false;
}

static bool fetch_bust_to_flash_network(const char *slug, const char **source_out)
{
    char url[256] = {0};

    /* Resized edge PNG (~450 KiB) before raw storage objects (often >1 MiB). */
    if (build_supabase_bust_url(slug, url, sizeof(url)) && fetch_bust_url_to_flash(url, "supabase", slug, 0)) {
        if (source_out != NULL) {
            *source_out = "supabase";
        }
        return true;
    }
    if (fetch_supabase_storage_right_bust(slug)) {
        if (source_out != NULL) {
            *source_out = "storage-right";
        }
        return true;
    }
    if (build_castalia_bust_url(slug, url, sizeof(url)) && fetch_bust_url_to_flash(url, "castalia", slug, 0)) {
        if (source_out != NULL) {
            *source_out = "castalia";
        }
        return true;
    }
    if (build_avatar_url(slug, url, sizeof(url)) && fetch_bust_url_to_flash(url, "avatar", slug, 0)) {
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
    const uint8_t *embedded_image = NULL;
    size_t image_len = 0;
    const char *source = NULL;
    bool image_owned = false;
    const bool from_flash = bust_load_from_flash(slug, &image, &image_len);
    if (from_flash) {
        source = "flash";
        image_owned = true;
    } else if (!s_network_fetch_enabled) {
        if (!bust_load_from_embedded(slug, &embedded_image, &image_len, &source)) {
            FACULTY175_LOG_STAGE_W(TAG, "faculty", "bust network deferred for %s", slug);
            return false;
        }
    } else if (!fetch_bust_to_flash_network(slug, &source) || !bust_load_from_flash(slug, &image, &image_len)) {
        if (!bust_load_from_embedded(slug, &embedded_image, &image_len, &source)) {
            return false;
        }
    } else {
        image_owned = true;
    }
    const uint8_t *image_bytes = image_owned ? image : embedded_image;

    uint16_t *scratch = s_bust_pixels;
    uint8_t *opaque = s_bust_opaque;
    if (scratch == NULL || opaque == NULL) {
        if (image_owned) {
            free(image);
        }
        return false;
    }

    int draw_w = 0;
    int draw_h = 0;
    const size_t fetched_len = image_len;
    const bool alpha_bust = bust_bytes_are_png(image_bytes, image_len);
    const bool ok = decode_bust_rgb565(image_bytes, image_len, scratch, opaque, &draw_w, &draw_h);
    if (image_owned) {
        free(image);
    }
    if (!ok || draw_w <= 0 || draw_h <= 0) {
        return false;
    }

    if (!alpha_bust) {
        bust_finalize_opacity(scratch, opaque, draw_w, draw_h);
    }

    if (!downscale_bust_for_display(scratch, opaque, &draw_w, &draw_h)) {
        ESP_LOGW(TAG, "bust downscale skipped — using decoded size %dx%d", draw_w, draw_h);
    }

    int content_cx = 0;
    int content_cy = 0;
    compute_bust_content_center(opaque, draw_w, draw_h, &content_cx, &content_cy);
    int min_x = 0;
    int min_y = 0;
    int max_x = 0;
    int max_y = 0;
    compute_bust_content_bounds(opaque, draw_w, draw_h, &min_x, &min_y, &max_x, &max_y);

    if (xSemaphoreTake(s_bust_lock, pdMS_TO_TICKS(2000)) == pdTRUE) {
        s_bust_draw_w = draw_w;
        s_bust_draw_h = draw_h;
        s_bust_content_cx = content_cx;
        s_bust_content_cy = content_cy;
        strncpy(s_loaded_slug, slug, sizeof(s_loaded_slug) - 1);
        s_loaded_slug[sizeof(s_loaded_slug) - 1] = '\0';
        s_status = FACULTY175_FACULTY_BUST_READY;
        xSemaphoreGive(s_bust_lock);
    }
    ESP_LOGI(TAG, "bust ready %s via %s (%dx%d, %u B)", slug, source != NULL ? source : "?", draw_w, draw_h,
             (unsigned)fetched_len);
    if (max_x >= 0) {
        ESP_LOGI(TAG, "bust opaque bbox %s %dx%d @ %d,%d center=%d,%d",
                 slug,
                 max_x - min_x + 1,
                 max_y - min_y + 1,
                 min_x,
                 min_y,
                 content_cx,
                 content_cy);
    } else {
        ESP_LOGW(TAG, "bust opaque bbox empty %s", slug);
    }
    FACULTY175_LOG_STAGE(TAG, "faculty", "bust ready %s via %s (%dx%d)", slug, source != NULL ? source : "?", draw_w, draw_h);
    return true;
}

static void bust_worker_task(void *arg)
{
    (void)arg;
    char slug[64];
    while (true) {
        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000)) == 0) {
            if (xSemaphoreTake(s_bust_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
                s_bust_task = NULL;
                xSemaphoreGive(s_bust_lock);
            }
            break;
        }
        if (xSemaphoreTake(s_bust_lock, pdMS_TO_TICKS(1000)) == pdTRUE) {
            strncpy(slug, s_req_slug, sizeof(slug) - 1);
            slug[sizeof(slug) - 1] = '\0';
            if (slug[0] == '\0') {
                xSemaphoreGive(s_bust_lock);
                continue;
            }
            if (s_status == FACULTY175_FACULTY_BUST_READY && strcmp(s_loaded_slug, slug) == 0) {
                xSemaphoreGive(s_bust_lock);
                continue;
            }
            s_status = FACULTY175_FACULTY_BUST_LOADING;
            xSemaphoreGive(s_bust_lock);
        } else {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        FACULTY175_LOG_STAGE(TAG, "faculty", "bust loading %s", slug);

        if (s_ui_notify != NULL) {
            s_ui_notify();
        }

        const bool ok = fetch_and_decode_bust(slug);
        if (!ok) {
            if (xSemaphoreTake(s_bust_lock, pdMS_TO_TICKS(1000)) == pdTRUE) {
                s_status = FACULTY175_FACULTY_BUST_ERROR;
                xSemaphoreGive(s_bust_lock);
            }
            ESP_LOGW(TAG, "bust fetch failed for %s", slug);
            FACULTY175_LOG_STAGE_W(TAG, "faculty", "bust fetch failed for %s", slug);
        }

        if (s_ui_notify != NULL) {
            s_ui_notify();
        }
    }
    vTaskDelete(NULL);
}

#if ASTROLABE_FACULTY_PREFETCH_ROSTER
static void prefetch_roster_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(4000));

    if (!bust_cache_init()) {
        s_prefetch_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    const int count = faculty175_faculty_roster_count();
    ESP_LOGI(TAG, "roster prefetch start (%d faculty)", count);
    for (int i = 0; i < count; ++i) {
        faculty175_faculty_roster_entry_t entry = {};
        if (!faculty175_faculty_roster_get(i, &entry)) {
            continue;
        }
        if (bust_flash_cached(entry.slug)) {
            ESP_LOGI(TAG, "roster prefetch cached %s", entry.slug);
            continue;
        }
        const char *source = NULL;
        if (fetch_bust_to_flash_network(entry.slug, &source)) {
            ESP_LOGI(TAG, "roster prefetch saved %s via %s",
                     entry.slug,
                     source != NULL ? source : "?");
        } else {
            ESP_LOGW(TAG, "roster prefetch miss %s", entry.slug);
        }
        vTaskDelay(pdMS_TO_TICKS(300));
    }
    ESP_LOGI(TAG, "roster prefetch done");
    s_prefetch_task = NULL;
    vTaskDelete(NULL);
}
#endif

void faculty175_faculty_prefetch_roster(void)
{
#if ASTROLABE_FACULTY_PREFETCH_ROSTER
    if (s_prefetch_task == NULL) {
        (void)xTaskCreate(prefetch_roster_task,
                          "fac_prefetch",
                          FACULTY175_FACULTY_PREFETCH_TASK_STACK,
                          NULL,
                          2,
                          &s_prefetch_task);
    }
#endif
}

TaskHandle_t faculty175_faculty_bust_task_handle(void)
{
    return s_bust_task;
}

TaskHandle_t faculty175_faculty_prefetch_task_handle(void)
{
    return s_prefetch_task;
}

static bool bust_worker_ensure(void)
{
    if (s_bust_task != NULL) {
        return true;
    }
    const uint32_t now_ms = faculty175_log_ms();
    if (s_bust_task_retry_after_ms != 0 && now_ms < s_bust_task_retry_after_ms) {
        return false;
    }
    if (xTaskCreate(bust_worker_task, "fac_bust", FACULTY175_FACULTY_BUST_TASK_STACK, NULL, 3, &s_bust_task) != pdPASS) {
        s_bust_task = NULL;
        s_bust_task_retry_after_ms = now_ms + 5000;
        ESP_LOGW(TAG, "bust worker task create failed");
        return false;
    }
    s_bust_task_retry_after_ms = 0;
    return true;
}

esp_err_t faculty175_faculty_init(void)
{
    const size_t px_count = (size_t)FACULTY175_FACULTY_BUST_W * (size_t)FACULTY175_FACULTY_BUST_H;
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
    (void)bust_cache_init();
    if (!bust_worker_ensure()) {
        ESP_LOGW(TAG, "bust worker reserve failed during init; will retry on demand");
    }
    return ESP_OK;
}

void faculty175_faculty_set_ui_notify(faculty175_faculty_ui_notify_fn fn)
{
    s_ui_notify = fn;
}

void faculty175_faculty_set_network_fetch_enabled(bool enabled)
{
    s_network_fetch_enabled = enabled;
}

void faculty175_faculty_request_bust(const char *slug)
{
    if (slug == NULL || slug[0] == '\0' || s_bust_lock == NULL) {
        return;
    }
    if (xSemaphoreTake(s_bust_lock, pdMS_TO_TICKS(200)) == pdTRUE) {
        if (s_status == FACULTY175_FACULTY_BUST_LOADING && strcmp(s_req_slug, slug) == 0) {
            xSemaphoreGive(s_bust_lock);
            return;
        }
        if (s_status == FACULTY175_FACULTY_BUST_READY && strcmp(s_loaded_slug, slug) == 0) {
            xSemaphoreGive(s_bust_lock);
            return;
        }
        strncpy(s_req_slug, slug, sizeof(s_req_slug) - 1);
        s_req_slug[sizeof(s_req_slug) - 1] = '\0';
        xSemaphoreGive(s_bust_lock);
    }
    if (bust_worker_ensure()) {
        xTaskNotifyGive(s_bust_task);
    }
}

void faculty175_faculty_request_bust_download(const char *slug)
{
    if (slug == NULL || slug[0] == '\0' || s_bust_lock == NULL) {
        return;
    }
    bust_cache_remove(slug);
    if (xSemaphoreTake(s_bust_lock, pdMS_TO_TICKS(200)) == pdTRUE) {
        if (strcmp(s_loaded_slug, slug) == 0) {
            s_loaded_slug[0] = '\0';
            s_bust_draw_w = 0;
            s_bust_draw_h = 0;
        }
        strncpy(s_req_slug, slug, sizeof(s_req_slug) - 1);
        s_req_slug[sizeof(s_req_slug) - 1] = '\0';
        s_status = FACULTY175_FACULTY_BUST_IDLE;
        xSemaphoreGive(s_bust_lock);
    }
    if (bust_worker_ensure()) {
        xTaskNotifyGive(s_bust_task);
    }
}

faculty175_faculty_bust_status_t faculty175_faculty_bust_status(void)
{
    faculty175_faculty_bust_status_t status = FACULTY175_FACULTY_BUST_IDLE;
    if (xSemaphoreTake(s_bust_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        status = s_status;
        xSemaphoreGive(s_bust_lock);
    }
    return status;
}

const char *faculty175_faculty_loaded_slug(void)
{
    return s_loaded_slug;
}

bool faculty175_faculty_bust_content_center(int *cx, int *cy)
{
    if (cx == NULL || cy == NULL || s_bust_lock == NULL) {
        return false;
    }
    bool ok = false;
    if (xSemaphoreTake(s_bust_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (s_status == FACULTY175_FACULTY_BUST_READY && s_bust_draw_w > 0 && s_bust_draw_h > 0) {
            *cx = s_bust_content_cx;
            *cy = s_bust_content_cy;
            ok = true;
        }
        xSemaphoreGive(s_bust_lock);
    }
    return ok;
}

void faculty175_faculty_bust_blit_origin(int area_x,
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

    int bcx = area_w / 2;
    int bcy = area_h / 2;
    if (!faculty175_faculty_bust_content_center(&bcx, &bcy)) {
        *out_x = area_x;
        *out_y = area_y;
        return;
    }

    const int panel_cx = area_w / 2;
    const int panel_cy = area_h / 2;
    if (panel_rotated_ccw) {
        /* panel px = fb y + oy; panel py = area_w - 1 - (fb x + ox) */
        *out_x = area_x + (area_w - 1 - panel_cy - bcx);
        *out_y = area_y + (panel_cx - bcy) + FACULTY175_FACULTY_BUST_DISPLAY_Y_NUDGE;
    } else {
        *out_x = area_x + (panel_cx - bcx);
        *out_y = area_y + (panel_cy - bcy) + FACULTY175_FACULTY_BUST_DISPLAY_Y_NUDGE;
    }
}

bool faculty175_faculty_draw_bust(int x, int y)
{
    if (s_bust_lock == NULL || s_bust_pixels == NULL || s_bust_opaque == NULL) {
        return false;
    }
    bool drew = false;
    if (xSemaphoreTake(s_bust_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (s_status == FACULTY175_FACULTY_BUST_READY && s_bust_draw_w > 0 && s_bust_draw_h > 0) {
            faculty175_display_blit_rgb565_masked(s_bust_pixels, s_bust_opaque, x, y, s_bust_draw_w, s_bust_draw_h);
            drew = true;
        }
        xSemaphoreGive(s_bust_lock);
    }
    return drew;
}

bool faculty175_faculty_copy_bust_argb8888(uint8_t *out_bgra,
                                           size_t out_cap,
                                           int *out_w,
                                           int *out_h,
                                           char *slug_out,
                                           size_t slug_cap)
{
    if (out_bgra == NULL || out_w == NULL || out_h == NULL || s_bust_lock == NULL ||
        s_bust_pixels == NULL || s_bust_opaque == NULL) {
        return false;
    }

    bool copied = false;
    if (xSemaphoreTake(s_bust_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (s_status == FACULTY175_FACULTY_BUST_READY && s_bust_draw_w > 0 && s_bust_draw_h > 0) {
            const size_t px_count = (size_t)s_bust_draw_w * (size_t)s_bust_draw_h;
            if (out_cap >= px_count * 4u) {
                for (size_t i = 0; i < px_count; ++i) {
                    const uint16_t p = s_bust_pixels[i];
                    const uint8_t r = (uint8_t)((((p >> 11) & 0x1fu) * 255u) / 31u);
                    const uint8_t g = (uint8_t)((((p >> 5) & 0x3fu) * 255u) / 63u);
                    const uint8_t b = (uint8_t)(((p & 0x1fu) * 255u) / 31u);
                    out_bgra[i * 4u + 0u] = b;
                    out_bgra[i * 4u + 1u] = g;
                    out_bgra[i * 4u + 2u] = r;
                    out_bgra[i * 4u + 3u] = s_bust_opaque[i] >= 128 ? 255 : 0;
                }
                *out_w = s_bust_draw_w;
                *out_h = s_bust_draw_h;
                if (slug_out != NULL && slug_cap > 0) {
                    strncpy(slug_out, s_loaded_slug, slug_cap - 1u);
                    slug_out[slug_cap - 1u] = '\0';
                }
                copied = true;
            }
        }
        xSemaphoreGive(s_bust_lock);
    }
    return copied;
}
