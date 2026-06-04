#include "atom_faculty.h"

#include <PNGdec.h>
#include <new>
#include <string.h>

#include "faculty18_board.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "atom_faculty_png";
#define BUST_PNG_MAX_LINE_W 512

struct PngBustCtx {
    PNG *png;
    uint16_t *out;
    uint8_t *opaque;
    int src_w;
    int src_h;
    int out_w;
    int out_h;
    int crop_x;
    int crop_y;
    int crop_w;
    int crop_h;
};

static void png_sprite_crop(int src_w, int src_h, int *crop_x, int *crop_y, int *crop_w, int *crop_h)
{
    *crop_x = 0;
    *crop_y = 0;
    *crop_w = src_w;
    *crop_h = src_h;
    if (src_w == 128 && src_h == 128) {
        *crop_x = 0;
        *crop_y = 64;
        *crop_w = 64;
        *crop_h = 64;
    }
}

static int png_bust_draw(PNGDRAW *pDraw)
{
    auto *ctx = static_cast<PngBustCtx *>(pDraw->pUser);
    if (ctx == NULL || ctx->png == NULL || ctx->out == NULL || ctx->opaque == NULL) {
        return 0;
    }
    if (pDraw->y < ctx->crop_y || pDraw->y >= ctx->crop_y + ctx->crop_h) {
        return 1;
    }

    const int dst_y = (ctx->out_h <= 1 || ctx->crop_h <= 1)
                          ? 0
                          : ((pDraw->y - ctx->crop_y) * (ctx->out_h - 1)) / (ctx->crop_h - 1);
    if (dst_y < 0 || dst_y >= ctx->out_h) {
        return 1;
    }
    if (pDraw->iWidth <= 0 || pDraw->iWidth > BUST_PNG_MAX_LINE_W) {
        return 0;
    }

    static uint16_t line[BUST_PNG_MAX_LINE_W];
    static uint8_t alpha[BUST_PNG_MAX_LINE_W];
    ctx->png->getLineAsRGB565(pDraw, line, PNG_RGB565_BIG_ENDIAN, 0x000000);
    ctx->png->getAlphaMask(pDraw, alpha, 128);

    for (int dx = 0; dx < ctx->out_w; ++dx) {
        const int sx = (ctx->out_w <= 1) ? 0 : (dx * (ctx->crop_w - 1)) / (ctx->out_w - 1);
        const int src_x = ctx->crop_x + sx;
        if (src_x < 0 || src_x >= pDraw->iWidth) {
            continue;
        }
        const int out_i = dst_y * ctx->out_w + dx;
        ctx->out[out_i] = faculty18_display_fb_from_logical565(line[src_x]);
        ctx->opaque[out_i] = alpha[src_x];
    }
    return 1;
}

extern "C" bool atom_faculty_png_decode(const uint8_t *png_bytes,
                                        size_t png_len,
                                        uint16_t *out,
                                        uint8_t *opaque,
                                        int *out_w,
                                        int *out_h)
{
    if (png_bytes == NULL || png_len < 8 || out == NULL || opaque == NULL) {
        return false;
    }

    void *mem = heap_caps_malloc(sizeof(PNG), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (mem == NULL) {
        mem = heap_caps_malloc(sizeof(PNG), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (mem == NULL) {
        ESP_LOGE(TAG, "PNG decoder alloc failed");
        return false;
    }
    PNG *decoder = new (mem) PNG();

    if (decoder->openRAM(const_cast<uint8_t *>(png_bytes), static_cast<int>(png_len), png_bust_draw) != PNG_SUCCESS) {
        ESP_LOGW(TAG, "PNG openRAM failed err=%d", decoder->getLastError());
        decoder->~PNG();
        heap_caps_free(decoder);
        return false;
    }

    const int src_w = decoder->getWidth();
    const int src_h = decoder->getHeight();
    if (src_w <= 0 || src_h <= 0 || src_w > BUST_PNG_MAX_LINE_W ||
        src_w > ATOM_FACULTY_BUST_W || src_h > ATOM_FACULTY_BUST_H) {
        ESP_LOGW(TAG, "PNG dimensions out of range %dx%d", src_w, src_h);
        decoder->close();
        decoder->~PNG();
        heap_caps_free(decoder);
        return false;
    }

    int crop_x = 0;
    int crop_y = 0;
    int crop_w = 0;
    int crop_h = 0;
    png_sprite_crop(src_w, src_h, &crop_x, &crop_y, &crop_w, &crop_h);

    PngBustCtx ctx = {};
    ctx.png = decoder;
    ctx.out = out;
    ctx.opaque = opaque;
    ctx.src_w = src_w;
    ctx.src_h = src_h;
    ctx.out_w = ATOM_FACULTY_BUST_W;
    ctx.out_h = ATOM_FACULTY_BUST_H;
    ctx.crop_x = crop_x;
    ctx.crop_y = crop_y;
    ctx.crop_w = crop_w;
    ctx.crop_h = crop_h;

    memset(out, 0, (size_t)ATOM_FACULTY_BUST_W * (size_t)ATOM_FACULTY_BUST_H * sizeof(uint16_t));
    memset(opaque, 0, (size_t)ATOM_FACULTY_BUST_W * (size_t)ATOM_FACULTY_BUST_H);

    const int rc = decoder->decode(&ctx, 0);
    const int err = decoder->getLastError();
    decoder->close();
    decoder->~PNG();
    heap_caps_free(decoder);
    if (rc != PNG_SUCCESS) {
        ESP_LOGW(TAG, "PNG decode failed err=%d (%dx%d)", err, src_w, src_h);
        return false;
    }

    if (crop_w == 64 && crop_h == 64 && src_w == 128) {
        ESP_LOGI(TAG, "PNG avatar sprite cropped to 64x64 cell (lower-left bust)");
    } else {
        ESP_LOGI(TAG, "PNG bust decoded %dx%d -> %dx%d (alpha)", src_w, src_h, ATOM_FACULTY_BUST_W,
                 ATOM_FACULTY_BUST_H);
    }

    if (out_w != NULL) {
        *out_w = ATOM_FACULTY_BUST_W;
    }
    if (out_h != NULL) {
        *out_h = ATOM_FACULTY_BUST_H;
    }
    return true;
}
