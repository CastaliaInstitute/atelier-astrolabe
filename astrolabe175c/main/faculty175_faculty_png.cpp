#include "faculty175_faculty.h"

#include <PNGdec.h>
#include <new>
#include <string.h>

#include "faculty175_board.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "faculty175_faculty_png";
#define BUST_PNG_MAX_LINE_W 512

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

static uint8_t png_alpha_at(const uint8_t *mask, int x)
{
    if (mask == NULL || x < 0) {
        return 255;
    }
    const uint8_t bit = (uint8_t)(0x80u >> (x & 7));
    return (mask[x >> 3] & bit) ? 255 : 0;
}

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

/** Center-crop to square before scaling (matches faculty-bust resize=cover). */
static void png_bust_cover_square(int *crop_x, int *crop_y, int *crop_w, int *crop_h)
{
    if (*crop_w <= 0 || *crop_h <= 0 || *crop_w == *crop_h) {
        return;
    }
    const int side = (*crop_w < *crop_h) ? *crop_w : *crop_h;
    *crop_x += (*crop_w - side) / 2;
    *crop_y += (*crop_h - side) / 2;
    *crop_w = side;
    *crop_h = side;
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

    if (pDraw->iWidth <= 0 || pDraw->iWidth > BUST_PNG_MAX_LINE_W) {
        return 0;
    }

    const int sy = pDraw->y - ctx->crop_y;
    /* Span of destination rows this source row covers. Filling the whole span
       (not a single row) prevents black gap lines when upscaling. */
    int dst_y0 = (ctx->crop_h <= 0) ? 0 : (sy * ctx->out_h) / ctx->crop_h;
    int dst_y1 = (ctx->crop_h <= 0) ? ctx->out_h : ((sy + 1) * ctx->out_h) / ctx->crop_h;
    if (dst_y1 <= dst_y0) {
        dst_y1 = dst_y0 + 1;
    }
    if (dst_y0 < 0) {
        dst_y0 = 0;
    }
    if (dst_y1 > ctx->out_h) {
        dst_y1 = ctx->out_h;
    }
    if (dst_y0 >= ctx->out_h) {
        return 1;
    }

    static uint16_t line[BUST_PNG_MAX_LINE_W];
    static uint8_t alpha_mask[(BUST_PNG_MAX_LINE_W + 7) / 8];
    /* The framebuffer stores logical RGB565; the board flush path byte-swaps for the panel wire format. */
    ctx->png->getLineAsRGB565(pDraw, line, PNG_RGB565_LITTLE_ENDIAN, faculty175_display_bkgd_u32());
    ctx->png->getAlphaMask(pDraw, alpha_mask, 64);

    static uint16_t scaled[1024];
    static uint8_t scaled_a[1024];
    for (int dx = 0; dx < ctx->out_w; ++dx) {
        const int sx = (ctx->out_w <= 1) ? 0 : (dx * (ctx->crop_w - 1)) / (ctx->out_w - 1);
        int src_x = ctx->crop_x + sx;
        if (src_x < 0) {
            src_x = 0;
        } else if (src_x >= pDraw->iWidth) {
            src_x = pDraw->iWidth - 1;
        }
        scaled[dx] = faculty175_display_fb_from_logical565(line[src_x]);
        scaled_a[dx] = png_alpha_at(alpha_mask, src_x);
    }

    for (int dst_y = dst_y0; dst_y < dst_y1; ++dst_y) {
        uint16_t *row = &ctx->out[dst_y * ctx->out_w];
        uint8_t *orow = &ctx->opaque[dst_y * ctx->out_w];
        for (int dx = 0; dx < ctx->out_w; ++dx) {
            row[dx] = scaled[dx];
            orow[dx] = scaled_a[dx];
        }
    }
    return 1;
}

extern "C" bool faculty175_faculty_png_decode(const uint8_t *png_bytes,
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
    if (src_w <= 0 || src_h <= 0 || src_w > BUST_PNG_MAX_LINE_W) {
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
    png_bust_cover_square(&crop_x, &crop_y, &crop_w, &crop_h);

    PngBustCtx ctx = {};
    ctx.png = decoder;
    ctx.out = out;
    ctx.opaque = opaque;
    ctx.out_w = FACULTY175_FACULTY_BUST_W;
    ctx.out_h = FACULTY175_FACULTY_BUST_H;
    ctx.crop_x = crop_x;
    ctx.crop_y = crop_y;
    ctx.crop_w = crop_w;
    ctx.crop_h = crop_h;

    memset(out, 0, (size_t)FACULTY175_FACULTY_BUST_W * (size_t)FACULTY175_FACULTY_BUST_H * sizeof(uint16_t));
    memset(opaque, 0, (size_t)FACULTY175_FACULTY_BUST_W * (size_t)FACULTY175_FACULTY_BUST_H);

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
        ESP_LOGI(TAG, "PNG bust decoded %dx%d crop %dx%d@%d,%d -> %dx%d", src_w, src_h, crop_w, crop_h, crop_x,
                 crop_y, FACULTY175_FACULTY_BUST_W, FACULTY175_FACULTY_BUST_H);
    }

    if (out_w != NULL) {
        *out_w = FACULTY175_FACULTY_BUST_W;
    }
    if (out_h != NULL) {
        *out_h = FACULTY175_FACULTY_BUST_H;
    }
    return true;
}
