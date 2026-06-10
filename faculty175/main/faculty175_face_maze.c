#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "faculty175_board.h"

#define STBI_NO_STDIO
#include "stb_image.h"

static const char *TAG = "maze";

extern const uint8_t _binary_maze_466_png_start[] asm("_binary_maze_466_png_start");
extern const uint8_t _binary_maze_466_png_end[] asm("_binary_maze_466_png_end");

static uint16_t rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return faculty175_display_rgb888(r, g, b);
}

static uint16_t *background_pixels(void)
{
    static uint16_t *pixels;
    static bool missing_logged;
    if (pixels != NULL) {
        return pixels;
    }

    const size_t png_len = (size_t)(_binary_maze_466_png_end - _binary_maze_466_png_start);
    if (png_len == 0) {
        return NULL;
    }

    int w = 0;
    int h = 0;
    int channels = 0;
    uint8_t *decoded = stbi_load_from_memory(_binary_maze_466_png_start,
                                             (int)png_len,
                                             &w,
                                             &h,
                                             &channels,
                                             3);
    if (decoded == NULL || w <= 0 || h <= 0) {
        if (!missing_logged) {
            missing_logged = true;
            ESP_LOGW(TAG, "maze PNG decode failed len=%u err=%s",
                     (unsigned)png_len,
                     stbi_failure_reason());
        }
        if (decoded != NULL) {
            stbi_image_free(decoded);
        }
        return NULL;
    }

    pixels = (uint16_t *)heap_caps_malloc(sizeof(uint16_t) * FACULTY175_LCD_W * FACULTY175_LCD_H,
                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (pixels == NULL) {
        pixels = (uint16_t *)malloc(sizeof(uint16_t) * FACULTY175_LCD_W * FACULTY175_LCD_H);
    }
    if (pixels == NULL) {
        stbi_image_free(decoded);
        if (!missing_logged) {
            missing_logged = true;
            ESP_LOGW(TAG, "maze PNG cache allocation failed");
        }
        return NULL;
    }

    for (int y = 0; y < FACULTY175_LCD_H; ++y) {
        const int src_y = (int)((int64_t)y * h / FACULTY175_LCD_H);
        for (int x = 0; x < FACULTY175_LCD_W; ++x) {
            const int src_x = (int)((int64_t)x * w / FACULTY175_LCD_W);
            const uint8_t *px = decoded + ((size_t)src_y * (size_t)w + (size_t)src_x) * 3u;
            pixels[(size_t)y * FACULTY175_LCD_W + (size_t)x] = rgb(px[0], px[1], px[2]);
        }
    }
    stbi_image_free(decoded);
    ESP_LOGI(TAG, "maze PNG cached %dx%d -> %dx%d", w, h, FACULTY175_LCD_W, FACULTY175_LCD_H);
    return pixels;
}

static bool draw_background(void)
{
    const uint16_t *pixels = background_pixels();
    if (pixels == NULL) {
        return false;
    }
    for (int y = 0; y < FACULTY175_LCD_H; ++y) {
        faculty175_display_draw_rgb565(pixels + (size_t)y * FACULTY175_LCD_W, 0, y, FACULTY175_LCD_W, 1);
        if ((y & 0x3f) == 0x3f) {
            vTaskDelay(1);
        }
    }
    return true;
}

void faculty175_face_maze_draw(uint32_t anim_ms)
{
    (void)anim_ms;

    if (!draw_background()) {
        faculty175_display_fill_rgb565(rgb(224, 222, 214));
    }
    faculty175_display_flush();
}
