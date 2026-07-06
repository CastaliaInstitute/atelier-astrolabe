#include "faculty175_face_tarot_image.h"

#include <PNGdec.h>
#include <new>
#include <stdio.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "faculty175_board.h"

namespace {

constexpr const char *TAG = "faculty175_tarot_image";
constexpr const char *kAssetBaseUrl = "http://tarot.castalia.institute/assets/deck/466";
constexpr size_t kMaxPngBytes = 560000;
constexpr int kMaxLineW = FACULTY175_LCD_W;
constexpr size_t kPixels = static_cast<size_t>(FACULTY175_LCD_W) * static_cast<size_t>(FACULTY175_LCD_H);

struct DecodeCtx {
    PNG *png;
    uint16_t *pixels;
    uint8_t *alpha;
};

SemaphoreHandle_t s_mux = nullptr;
TaskHandle_t s_task = nullptr;
volatile bool s_busy = false;
int s_request_idx = -1;
char s_request_slug[40] = {};
int s_cached_idx = -1;
uint16_t *s_pixels = nullptr;
uint8_t *s_alpha = nullptr;
char s_error[40] = {};

void set_error(const char *msg)
{
    if (msg == nullptr) {
        s_error[0] = '\0';
        return;
    }
    strlcpy(s_error, msg, sizeof(s_error));
}

bool mux_take(uint32_t ms)
{
    if (s_mux == nullptr) {
        s_mux = xSemaphoreCreateMutex();
    }
    return s_mux != nullptr && xSemaphoreTake(s_mux, pdMS_TO_TICKS(ms)) == pdTRUE;
}

void mux_give()
{
    if (s_mux != nullptr) {
        xSemaphoreGive(s_mux);
    }
}

void free_cached_locked()
{
    heap_caps_free(s_pixels);
    heap_caps_free(s_alpha);
    s_pixels = nullptr;
    s_alpha = nullptr;
    s_cached_idx = -1;
}

void build_filename(int idx, const char *slug, char *out, size_t cap)
{
    if (out == nullptr || cap == 0) {
        return;
    }
    out[0] = '\0';
    if (idx < 22) {
        snprintf(out, cap, "major-%02d-%s.png", idx, slug);
        return;
    }
    const char *suit = "wands";
    int rank = 1;
    if (idx < 36) {
        suit = "cups";
        rank = idx - 21;
    } else if (idx < 50) {
        suit = "pentacles";
        rank = idx - 35;
    } else if (idx < 64) {
        suit = "swords";
        rank = idx - 49;
    } else {
        suit = "wands";
        rank = idx - 63;
    }
    snprintf(out, cap, "%s-%02d-%s.png", suit, rank, slug);
}

bool build_url(int idx, const char *slug, char *out, size_t cap)
{
    char filename[64];
    build_filename(idx, slug, filename, sizeof(filename));
    if (filename[0] == '\0') {
        return false;
    }
    const int n = snprintf(out, cap, "%s/%s", kAssetBaseUrl, filename);
    return n > 0 && static_cast<size_t>(n) < cap;
}

uint8_t alpha_at(const uint8_t *mask, int x)
{
    if (mask == nullptr || x < 0) {
        return 255;
    }
    return (mask[x >> 3] & (0x80u >> (x & 7))) ? 255 : 0;
}

int png_draw(PNGDRAW *draw)
{
    auto *ctx = static_cast<DecodeCtx *>(draw ? draw->pUser : nullptr);
    if (ctx == nullptr || ctx->png == nullptr || ctx->pixels == nullptr || ctx->alpha == nullptr || draw->iWidth > kMaxLineW ||
        draw->y < 0 || draw->y >= FACULTY175_LCD_H) {
        return 0;
    }

    static uint16_t line[kMaxLineW];
    static uint8_t alpha_mask[(kMaxLineW + 7) / 8];
    ctx->png->getLineAsRGB565(draw, line, PNG_RGB565_LITTLE_ENDIAN, faculty175_display_bkgd_u32());
    const bool has_alpha = ctx->png->getAlphaMask(draw, alpha_mask, 8) != 0;
    uint16_t *dst = &ctx->pixels[static_cast<size_t>(draw->y) * FACULTY175_LCD_W];
    uint8_t *adst = &ctx->alpha[static_cast<size_t>(draw->y) * FACULTY175_LCD_W];
    for (int x = 0; x < draw->iWidth && x < FACULTY175_LCD_W; ++x) {
        dst[x] = faculty175_display_fb_from_logical565(line[x]);
        adst[x] = has_alpha ? alpha_at(alpha_mask, x) : 255;
    }
    return 1;
}

bool download_png(const char *url, uint8_t **out, size_t *out_len)
{
    *out = nullptr;
    *out_len = 0;
    esp_http_client_config_t cfg = {};
    cfg.url = url;
    cfg.timeout_ms = 30000;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.keep_alive_enable = false;
    cfg.buffer_size = 4096;
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == nullptr) {
        set_error("http alloc");
        return false;
    }
    esp_http_client_set_header(client, "User-Agent", "Astrolabe-Faculty175-Tarot/1");
    esp_http_client_set_header(client, "Accept", "image/png,image/*;q=0.8,*/*;q=0.1");

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        set_error("http open");
        esp_http_client_cleanup(client);
        return false;
    }
    const int64_t header_len = esp_http_client_fetch_headers(client);
    const int status = esp_http_client_get_status_code(client);
    if (status != 200 || header_len <= 0 || static_cast<size_t>(header_len) > kMaxPngBytes) {
        set_error("HTTP image");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    auto *buf = static_cast<uint8_t *>(heap_caps_malloc(static_cast<size_t>(header_len), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (buf == nullptr) {
        buf = static_cast<uint8_t *>(heap_caps_malloc(static_cast<size_t>(header_len), MALLOC_CAP_8BIT));
    }
    if (buf == nullptr) {
        set_error("png alloc");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    size_t total = 0;
    while (total < static_cast<size_t>(header_len)) {
        const int n = esp_http_client_read(client, reinterpret_cast<char *>(buf + total),
                                           static_cast<int>(static_cast<size_t>(header_len) - total));
        if (n < 0) {
            set_error("png read");
            heap_caps_free(buf);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return false;
        }
        if (n == 0) {
            break;
        }
        total += static_cast<size_t>(n);
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    if (total < 8 || total != static_cast<size_t>(header_len)) {
        set_error("short png");
        heap_caps_free(buf);
        return false;
    }
    *out = buf;
    *out_len = total;
    return true;
}

bool decode_png(uint8_t *png, size_t png_len, int idx)
{
    void *mem = heap_caps_malloc(sizeof(PNG), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (mem == nullptr) {
        mem = heap_caps_malloc(sizeof(PNG), MALLOC_CAP_8BIT);
    }
    if (mem == nullptr) {
        set_error("decoder alloc");
        return false;
    }
    PNG *decoder = new (mem) PNG();
    if (decoder->openRAM(png, static_cast<int>(png_len), png_draw) != PNG_SUCCESS) {
        set_error("png open");
        decoder->~PNG();
        heap_caps_free(decoder);
        return false;
    }
    const int w = decoder->getWidth();
    const int h = decoder->getHeight();
    if (w != FACULTY175_LCD_W || h != FACULTY175_LCD_H) {
        set_error("png size");
        decoder->close();
        decoder->~PNG();
        heap_caps_free(decoder);
        return false;
    }
    auto *pixels = static_cast<uint16_t *>(heap_caps_malloc(kPixels * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    auto *alpha = static_cast<uint8_t *>(heap_caps_malloc(kPixels, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (pixels == nullptr || alpha == nullptr) {
        heap_caps_free(pixels);
        heap_caps_free(alpha);
        set_error("image alloc");
        decoder->close();
        decoder->~PNG();
        heap_caps_free(decoder);
        return false;
    }
    memset(pixels, 0, kPixels * sizeof(uint16_t));
    memset(alpha, 0, kPixels);

    DecodeCtx ctx = {};
    ctx.png = decoder;
    ctx.pixels = pixels;
    ctx.alpha = alpha;
    const int rc = decoder->decode(&ctx, 0);
    decoder->close();
    decoder->~PNG();
    heap_caps_free(decoder);
    if (rc != PNG_SUCCESS) {
        heap_caps_free(pixels);
        heap_caps_free(alpha);
        set_error("png decode");
        return false;
    }

    if (!mux_take(3000)) {
        heap_caps_free(pixels);
        heap_caps_free(alpha);
        set_error("image lock");
        return false;
    }
    free_cached_locked();
    s_pixels = pixels;
    s_alpha = alpha;
    s_cached_idx = idx;
    mux_give();
    set_error(nullptr);
    return true;
}

void fetch_task(void *)
{
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        const int idx = s_request_idx;
        char slug[sizeof(s_request_slug)];
        strlcpy(slug, s_request_slug, sizeof(slug));
        char url[160];
        bool ok = build_url(idx, slug, url, sizeof(url));
        uint8_t *png = nullptr;
        size_t png_len = 0;
        if (ok) {
            ok = download_png(url, &png, &png_len);
        }
        if (ok) {
            ok = decode_png(png, png_len, idx);
        }
        heap_caps_free(png);
        ESP_LOGI(TAG, "%s %02d %s", ok ? "cached" : "failed", idx, s_error[0] ? s_error : slug);
        s_busy = false;
    }
}

void ensure_task()
{
    if (s_task != nullptr) {
        return;
    }
    if (xTaskCreate(fetch_task, "tarot_img", 12288, nullptr, 2, &s_task) != pdPASS) {
        s_task = nullptr;
        set_error("task alloc");
    }
}

}  // namespace

extern "C" void faculty175_tarot_image_request(int idx, const faculty175_tarot_card_t *card)
{
    if (card == nullptr || idx < 0 || idx >= FACULTY175_TAROT_CARD_COUNT || s_cached_idx == idx || s_busy) {
        return;
    }
    ensure_task();
    if (s_task == nullptr) {
        return;
    }
    s_request_idx = idx;
    strlcpy(s_request_slug, card->slug, sizeof(s_request_slug));
    s_busy = true;
    xTaskNotify(s_task, 1, eSetBits);
}

extern "C" bool faculty175_tarot_image_draw_cached(int idx)
{
    if (!mux_take(20)) {
        return false;
    }
    if (idx != s_cached_idx || s_pixels == nullptr || s_alpha == nullptr) {
        mux_give();
        return false;
    }
    faculty175_display_blit_rgb565_masked(s_pixels, s_alpha, 0, 0, FACULTY175_LCD_W, FACULTY175_LCD_H);
    mux_give();
    return true;
}

extern "C" bool faculty175_tarot_image_busy(void)
{
    return s_busy;
}

extern "C" const char *faculty175_tarot_image_error(void)
{
    return s_error;
}
