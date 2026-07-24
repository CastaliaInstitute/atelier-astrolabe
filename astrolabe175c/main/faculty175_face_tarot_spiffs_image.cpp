#include "faculty175_face_tarot_spiffs_image.h"

#include <PNGdec.h>
#include <new>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "faculty175_storage.h"

namespace {

constexpr const char *TAG = "faculty175_tarot_spiffs";
constexpr int kCardW = 466;
constexpr int kCardH = 466;
constexpr size_t kCardPixels = static_cast<size_t>(kCardW) * static_cast<size_t>(kCardH);
constexpr size_t kMaxPngBytes = 180000;

struct DecodeCtx {
    PNG *png;
    uint16_t *pixels;
};

bool s_storage_checked = false;
bool s_storage_ready = false;
bool s_spiffs_checked = false;
bool s_spiffs_ready = false;
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
    const esp_err_t err = faculty175_storage_init();
    if (err == ESP_OK) {
        s_storage_ready = true;
        return true;
    }
    snprintf(s_error, sizeof(s_error), "fat %s", esp_err_to_name(err));
    ESP_LOGW(TAG, "media FAT init failed: %s", esp_err_to_name(err));
    return false;
}

bool packaged_storage_ready()
{
    if (s_spiffs_checked) {
        return s_spiffs_ready;
    }
    s_spiffs_checked = true;

    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/bust_cache",
        .partition_label = "storage",
        .max_files = 12,
        .format_if_mount_failed = false,
    };
    const esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
        s_spiffs_ready = true;
        return true;
    }
    ESP_LOGW(TAG, "packaged tarot storage unavailable: %s", esp_err_to_name(err));
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
    const char *base = faculty175_storage_media_base_path();
    if (base == nullptr || base[0] == '\0') {
        return false;
    }
    const int n = snprintf(out, cap, "%s/tarot/deck/466/%s", base, filename);
    return n > 0 && static_cast<size_t>(n) < cap;
}

bool build_packaged_path(int idx, const faculty175_tarot_card_t *card, char *out, size_t cap)
{
    if (card == nullptr || card->slug == nullptr || out == nullptr || cap == 0) {
        return false;
    }
    char filename[64];
    build_filename(idx, card->slug, filename, sizeof(filename));
    if (filename[0] == '\0') {
        return false;
    }
    const int n = snprintf(out, cap, "/bust_cache/tarot/deck/466/%s", filename);
    return n > 0 && static_cast<size_t>(n) < cap;
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
    if (ctx == nullptr || ctx->png == nullptr || ctx->pixels == nullptr || draw->iWidth > kCardW ||
        draw->y < 0 || draw->y >= kCardH) {
        return 0;
    }
    static uint16_t line[kCardW];
    ctx->png->getLineAsRGB565(draw, line, PNG_RGB565_LITTLE_ENDIAN, 0);
    uint16_t *dst = &ctx->pixels[static_cast<size_t>(draw->y) * kCardW];
    memcpy(dst, line, static_cast<size_t>(draw->iWidth) * sizeof(uint16_t));
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
    if (decoder->getWidth() != kCardW || decoder->getHeight() != kCardH) {
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
    ESP_LOGI(TAG, "cached card %02d from FAT media", idx);
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
    char path[128];
    uint8_t *png = nullptr;
    size_t png_len = 0;

    /* User media wins, allowing a deck to be replaced without reflashing. */
    if (storage_ready() && build_path(idx, card, path, sizeof(path))) {
        png = read_file(path, &png_len);
    }

    /* Every LunaSay build also carries the canonical circular deck. */
    if (png == nullptr) {
        if (!packaged_storage_ready() || !build_packaged_path(idx, card, path, sizeof(path))) {
            set_error("card storage");
            return false;
        }
        png = read_file(path, &png_len);
        if (png == nullptr) {
            return false;
        }
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
