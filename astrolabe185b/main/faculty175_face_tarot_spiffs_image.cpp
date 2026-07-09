#include "faculty175_face_tarot_spiffs_image.h"

#include <PNGdec.h>
#include <new>
#include <stdio.h>
#include <string.h>

#include "faculty175_usb.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_spiffs.h"

namespace {

constexpr const char *TAG = "faculty175_tarot_spiffs";
constexpr const char *kBasePath = "/bust_cache";
constexpr const char *kPartition = "storage";
constexpr int kCardW = 360;
constexpr int kCardH = 360;
constexpr int kSourceMaxW = 466;
constexpr int kSourceMaxH = 466;
constexpr size_t kCardPixels = static_cast<size_t>(kCardW) * static_cast<size_t>(kCardH);
constexpr size_t kMaxPngBytes = 180000;

struct DecodeCtx {
    PNG *png;
    uint16_t *pixels;
    int src_w;
    int src_h;
};

bool s_storage_checked = false;
bool s_storage_ready = false;
int s_cached_idx = -1;
uint16_t *s_pixels = nullptr;
char s_error[48] = {};

void set_error(const char *msg)
{
    if (msg == nullptr) {
        s_error[0] = '\0';
        return;
    }
    strlcpy(s_error, msg, sizeof(s_error));
}

bool storage_ready()
{
    if (s_storage_checked) {
        return s_storage_ready;
    }
    s_storage_checked = true;
    esp_vfs_spiffs_conf_t conf = {};
    conf.base_path = kBasePath;
    conf.partition_label = kPartition;
    conf.max_files = 12;
    conf.format_if_mount_failed = false;
    const esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
        s_storage_ready = true;
        return true;
    }
    snprintf(s_error, sizeof(s_error), "spiffs %s", esp_err_to_name(err));
    ESP_LOGW(TAG, "SPIFFS mount failed: %s", esp_err_to_name(err));
    return false;
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

bool build_path(int idx, const faculty175_tarot_card_t *card, char *out, size_t cap)
{
    if (card == nullptr || card->slug == nullptr || out == nullptr || cap == 0) {
        return false;
    }
    char filename[64];
    build_filename(idx, card->slug, filename, sizeof(filename));
    if (filename[0] == '\0') {
        return false;
    }
    char fallback[128];
    const int n = snprintf(fallback, sizeof(fallback), "%s/tarot/deck/466/%s", kBasePath, filename);
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(fallback)) {
        return false;
    }
    char sd_relative[128];
    const int sd_n = snprintf(sd_relative, sizeof(sd_relative), "bust_cache/tarot/deck/360/%s", filename);
    if (sd_n <= 0 || static_cast<size_t>(sd_n) >= sizeof(sd_relative)) {
        return false;
    }
    return faculty175_usb_resolve_asset_path(sd_relative, fallback, out, cap);
}

uint8_t *read_file(const char *path, size_t *out_len)
{
    *out_len = 0;
    FILE *f = fopen(path, "rb");
    if (f == nullptr) {
        set_error("card missing");
        return nullptr;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        set_error("seek");
        return nullptr;
    }
    const long len = ftell(f);
    if (len <= 8 || static_cast<size_t>(len) > kMaxPngBytes) {
        fclose(f);
        set_error("png size");
        return nullptr;
    }
    rewind(f);

    auto *bytes = static_cast<uint8_t *>(heap_caps_malloc(static_cast<size_t>(len), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (bytes == nullptr) {
        bytes = static_cast<uint8_t *>(heap_caps_malloc(static_cast<size_t>(len), MALLOC_CAP_8BIT));
    }
    if (bytes == nullptr) {
        fclose(f);
        set_error("png alloc");
        return nullptr;
    }
    const size_t got = fread(bytes, 1, static_cast<size_t>(len), f);
    fclose(f);
    if (got != static_cast<size_t>(len)) {
        heap_caps_free(bytes);
        set_error("short read");
        return nullptr;
    }
    *out_len = got;
    return bytes;
}

int png_draw(PNGDRAW *draw)
{
    auto *ctx = static_cast<DecodeCtx *>(draw != nullptr ? draw->pUser : nullptr);
    if (ctx == nullptr || ctx->png == nullptr || ctx->pixels == nullptr || ctx->src_w <= 0 || ctx->src_h <= 0 ||
        draw->iWidth > kSourceMaxW || draw->y < 0 || draw->y >= ctx->src_h) {
        return 0;
    }
    static uint16_t line[kSourceMaxW];
    ctx->png->getLineAsRGB565(draw, line, PNG_RGB565_LITTLE_ENDIAN, 0);
    const int dst_y0 = (draw->y * kCardH) / ctx->src_h;
    int dst_y1 = ((draw->y + 1) * kCardH) / ctx->src_h - 1;
    if (dst_y1 < dst_y0) {
        dst_y1 = dst_y0;
    }
    for (int dst_y = dst_y0; dst_y <= dst_y1 && dst_y < kCardH; ++dst_y) {
        uint16_t *dst = &ctx->pixels[static_cast<size_t>(dst_y) * kCardW];
        for (int x = 0; x < kCardW; ++x) {
            const int src_x = (x * ctx->src_w) / kCardW;
            dst[x] = line[src_x];
        }
    }
    return 1;
}

bool decode_png(uint8_t *png_bytes, size_t png_len, int idx)
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
    if (decoder->openRAM(png_bytes, static_cast<int>(png_len), png_draw) != PNG_SUCCESS) {
        decoder->~PNG();
        heap_caps_free(decoder);
        set_error("png open");
        return false;
    }
    const int src_w = decoder->getWidth();
    const int src_h = decoder->getHeight();
    if (!((src_w == kCardW && src_h == kCardH) || (src_w == 466 && src_h == 466))) {
        decoder->close();
        decoder->~PNG();
        heap_caps_free(decoder);
        set_error("card dim");
        return false;
    }

    auto *pixels = static_cast<uint16_t *>(heap_caps_malloc(kCardPixels * sizeof(uint16_t),
                                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (pixels == nullptr) {
        pixels = static_cast<uint16_t *>(heap_caps_malloc(kCardPixels * sizeof(uint16_t), MALLOC_CAP_8BIT));
    }
    if (pixels == nullptr) {
        decoder->close();
        decoder->~PNG();
        heap_caps_free(decoder);
        set_error("pixel alloc");
        return false;
    }
    memset(pixels, 0, kCardPixels * sizeof(uint16_t));

    DecodeCtx ctx = {};
    ctx.png = decoder;
    ctx.pixels = pixels;
    ctx.src_w = src_w;
    ctx.src_h = src_h;
    const int rc = decoder->decode(&ctx, 0);
    decoder->close();
    decoder->~PNG();
    heap_caps_free(decoder);
    if (rc != PNG_SUCCESS) {
        heap_caps_free(pixels);
        set_error("png decode");
        return false;
    }

    heap_caps_free(s_pixels);
    s_pixels = pixels;
    s_cached_idx = idx;
    set_error(nullptr);
    ESP_LOGI(TAG, "cached card %02d from asset %dx%d -> %dx%d", idx, src_w, src_h, kCardW, kCardH);
    return true;
}

}  // namespace

extern "C" bool faculty175_tarot_spiffs_image_get(int idx,
                                                  const faculty175_tarot_card_t *card,
                                                  const uint16_t **pixels,
                                                  int *w,
                                                  int *h)
{
    if (pixels == nullptr || w == nullptr || h == nullptr || idx < 0 || idx >= FACULTY175_TAROT_CARD_COUNT) {
        return false;
    }
    *pixels = nullptr;
    *w = 0;
    *h = 0;
    if (s_cached_idx == idx && s_pixels != nullptr) {
        *pixels = s_pixels;
        *w = kCardW;
        *h = kCardH;
        return true;
    }
    if (!storage_ready()) {
        return false;
    }

    char path[128];
    if (!build_path(idx, card, path, sizeof(path))) {
        set_error("path");
        return false;
    }
    size_t png_len = 0;
    uint8_t *png = read_file(path, &png_len);
    if (png == nullptr) {
        return false;
    }
    const bool ok = decode_png(png, png_len, idx);
    heap_caps_free(png);
    if (!ok) {
        return false;
    }
    *pixels = s_pixels;
    *w = kCardW;
    *h = kCardH;
    return true;
}

extern "C" const char *faculty175_tarot_spiffs_image_error(void)
{
    return s_error;
}
