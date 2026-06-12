#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "faculty175_board.h"
#include "faculty175_usb.h"

#define STBI_NO_STDIO
#include "stb_image.h"

static const char *TAG = "maze";
#define MAZE_STORAGE_BASE "/bust_cache"
#define MAZE_STORAGE_PARTITION "storage"
#define MAZE_STORAGE_PATH MAZE_STORAGE_BASE "/maze_466.png"
#define MAZE_SD_PATH "bust_cache/maze_360.png"

static bool s_storage_checked;
static bool s_storage_ready;

static uint16_t rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return faculty175_display_rgb888(r, g, b);
}

static bool storage_ready(void)
{
    if (s_storage_checked) {
        return s_storage_ready;
    }
    s_storage_checked = true;
    const esp_vfs_spiffs_conf_t conf = {
        .base_path = MAZE_STORAGE_BASE,
        .partition_label = MAZE_STORAGE_PARTITION,
        .max_files = 12,
        .format_if_mount_failed = false,
    };
    const esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "maze SPIFFS mount failed: %s", esp_err_to_name(err));
        return false;
    }
    s_storage_ready = true;
    return true;
}

static uint16_t *background_pixels(void)
{
    static uint16_t *pixels;
    static bool missing_logged;
    if (pixels != NULL) {
        return pixels;
    }

    if (!storage_ready()) {
        return NULL;
    }
    char path[128];
    if (!faculty175_usb_resolve_asset_path(MAZE_SD_PATH, MAZE_STORAGE_PATH, path, sizeof(path))) {
        if (!missing_logged) {
            missing_logged = true;
            ESP_LOGW(TAG, "maze PNG missing");
        }
        return NULL;
    }
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        if (!missing_logged) {
            missing_logged = true;
            ESP_LOGW(TAG, "maze PNG missing: %s", path);
        }
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    const long size = ftell(f);
    if (size <= 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    uint8_t *png = (uint8_t *)heap_caps_malloc((size_t)size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (png == NULL) {
        png = (uint8_t *)heap_caps_malloc((size_t)size, MALLOC_CAP_8BIT);
    }
    if (png == NULL) {
        fclose(f);
        return NULL;
    }
    const size_t got = fread(png, 1, (size_t)size, f);
    fclose(f);
    if (got != (size_t)size) {
        free(png);
        return NULL;
    }

    int w = 0;
    int h = 0;
    int channels = 0;
    uint8_t *decoded = stbi_load_from_memory(png,
                                             (int)size,
                                             &w,
                                             &h,
                                             &channels,
                                             3);
    free(png);
    if (decoded == NULL || w <= 0 || h <= 0) {
        if (!missing_logged) {
            missing_logged = true;
            ESP_LOGW(TAG, "maze PNG decode failed len=%u err=%s",
                     (unsigned)size,
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
    ESP_LOGI(TAG, "maze PNG cached %s %dx%d -> %dx%d", path, w, h, FACULTY175_LCD_W, FACULTY175_LCD_H);
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
