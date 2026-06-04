#include "paper_faculty.h"

#include <PNGdec.h>
#include <new>
#include <string.h>

#include "paper_board.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "paper_faculty_png";

struct PngBustCtx {
    PNG *png;
    uint16_t *out;
    uint8_t *opaque;
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

    const int src_row = pDraw->y - ctx->crop_y;
    int dst_y0 = (ctx->crop_h <= 1) ? 0 : (src_row * ctx->out_h) / ctx->crop_h;
    int dst_y1 = (ctx->crop_h <= 1) ? ctx->out_h : ((src_row + 1) * ctx->out_h) / ctx->crop_h;
    if (dst_y1 <= dst_y0) {
        dst_y1 = dst_y0 + 1;
    }
    if (dst_y0 < 0) {
        dst_y0 = 0;
    }
    if (dst_y1 > ctx->out_h) {
        dst_y1 = ctx->out_h;
    }
    static uint16_t line[1024];
    if (pDraw->iWidth > (int)(sizeof(line) / sizeof(line[0]))) {
        return 0;
    }

    const uint32_t bkgd_u32 = paper_display_bkgd_u32();
    ctx->png->getLineAsRGB565(pDraw, line, PNG_RGB565_LITTLE_ENDIAN, bkgd_u32);

    for (int dx = 0; dx < ctx->out_w; ++dx) {
        const int sx =
            (ctx->out_w <= 1) ? 0 : (dx * (ctx->crop_w - 1)) / (ctx->out_w - 1);
        const int src_x = ctx->crop_x + sx;
        if (src_x < 0 || src_x >= pDraw->iWidth) {
            continue;
        }
        const uint16_t px = line[src_x];
        const uint16_t fb_px = paper_display_fb_from_logical565(px);
        for (int dst_y = dst_y0; dst_y < dst_y1; ++dst_y) {
            const int out_i = dst_y * ctx->out_w + dx;
            ctx->out[out_i] = fb_px;
            ctx->opaque[out_i] = 255;
        }
    }
    return 1;
}

extern "C" bool paper_faculty_png_decode(const uint8_t *png_bytes,
                                        size_t png_len,
                                        uint16_t *out,
                                        uint8_t *opaque,
                                        int *out_w,
                                        int *out_h)
{
    if (png_bytes == NULL || png_len < 8 || out == NULL || opaque == NULL) {
        return false;
    }

    void *mem = heap_caps_malloc(sizeof(PNG), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (mem == NULL) {
        mem = heap_caps_malloc(sizeof(PNG), MALLOC_CAP_8BIT);
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
    if (src_w <= 0 || src_h <= 0) {
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
    ctx.out_w = PAPER_FACULTY_BUST_W;
    ctx.out_h = PAPER_FACULTY_BUST_H;
    ctx.crop_x = crop_x;
    ctx.crop_y = crop_y;
    ctx.crop_w = crop_w;
    ctx.crop_h = crop_h;

    memset(out, 0, (size_t)PAPER_FACULTY_BUST_W * (size_t)PAPER_FACULTY_BUST_H * sizeof(uint16_t));
    memset(opaque, 0, (size_t)PAPER_FACULTY_BUST_W * (size_t)PAPER_FACULTY_BUST_H);

    const int rc = decoder->decode(&ctx, 0);
    const int err = decoder->getLastError();
    decoder->close();
    decoder->~PNG();
    heap_caps_free(decoder);
    if (rc != PNG_SUCCESS) {
        ESP_LOGW(TAG, "PNG decode failed err=%d (%dx%d)", err, src_w, src_h);
        return false;
    }

    ESP_LOGI(TAG, "PNG bust decoded %dx%d -> %dx%d", src_w, src_h, PAPER_FACULTY_BUST_W, PAPER_FACULTY_BUST_H);

    if (out_w != NULL) {
        *out_w = PAPER_FACULTY_BUST_W;
    }
    if (out_h != NULL) {
        *out_h = PAPER_FACULTY_BUST_H;
    }
    return true;
}
