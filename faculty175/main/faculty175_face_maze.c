#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_spiffs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "faculty175_board.h"

#define MAZE_BG_PATH "/bust_cache/maze/maze_466.gray"

static const char *TAG = "maze";

extern const uint8_t _binary_maze_466_gray_start[] asm("_binary_maze_466_gray_start");
extern const uint8_t _binary_maze_466_gray_end[] asm("_binary_maze_466_gray_end");

__attribute__((weak)) bool faculty175_motion_pitch_roll(float *pitch_deg, float *roll_deg)
{
    (void)pitch_deg;
    (void)roll_deg;
    return false;
}

static uint16_t rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return faculty175_display_rgb888(r, g, b);
}

static bool storage_mount(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/bust_cache",
        .partition_label = "storage",
        .max_files = 12,
        .format_if_mount_failed = false,
    };
    const esp_err_t err = esp_vfs_spiffs_register(&conf);
    return err == ESP_OK || err == ESP_ERR_INVALID_STATE;
}

static bool draw_background(void)
{
    static uint16_t *row;
    static uint8_t *gray;
    static bool missing_logged;
    if (row == NULL) {
        row = (uint16_t *)malloc(sizeof(uint16_t) * FACULTY175_LCD_W);
        if (row == NULL) {
            return false;
        }
    }
    if (gray == NULL) {
        gray = (uint8_t *)malloc(FACULTY175_LCD_W);
        if (gray == NULL) {
            return false;
        }
    }

    const size_t embedded_len = (size_t)(_binary_maze_466_gray_end - _binary_maze_466_gray_start);
    const bool embedded_ok = embedded_len >= (size_t)FACULTY175_LCD_W * (size_t)FACULTY175_LCD_H;
    FILE *f = NULL;
    if (!embedded_ok && storage_mount()) {
        f = fopen(MAZE_BG_PATH, "rb");
    }
    if (f == NULL && !embedded_ok) {
        if (!missing_logged) {
            missing_logged = true;
            ESP_LOGW(TAG, "missing %s", MAZE_BG_PATH);
        }
        return false;
    }
    for (int y = 0; y < FACULTY175_LCD_H; ++y) {
        if (f != NULL && fread(gray, 1, FACULTY175_LCD_W, f) != FACULTY175_LCD_W) {
            fclose(f);
            return false;
        } else if (f == NULL) {
            memcpy(gray,
                   _binary_maze_466_gray_start + (size_t)y * (size_t)FACULTY175_LCD_W,
                   FACULTY175_LCD_W);
        }
        for (int x = 0; x < FACULTY175_LCD_W; ++x) {
            const uint8_t v = gray[x];
            row[x] = rgb(v, v, v);
        }
        faculty175_display_draw_rgb565(row, 0, y, FACULTY175_LCD_W, 1);
        if ((y & 0x3f) == 0x3f) {
            vTaskDelay(1);
        }
    }
    if (f != NULL) {
        fclose(f);
    }
    return true;
}

static void ball_target(uint32_t anim_ms, float *out_x, float *out_y)
{
    float pitch = 0.0f;
    float roll = 0.0f;
    if (faculty175_motion_pitch_roll(&pitch, &roll)) {
        *out_x = roll / 38.0f;
        *out_y = pitch / 38.0f;
    } else {
        const float t = (float)anim_ms * 0.001f;
        *out_x = 0.46f * sinf(t * 0.77f) + 0.19f * sinf(t * 1.53f + 1.4f);
        *out_y = 0.42f * cosf(t * 0.69f + 0.7f) + 0.17f * sinf(t * 1.18f);
    }
    const float d2 = (*out_x * *out_x) + (*out_y * *out_y);
    if (d2 > 1.0f) {
        const float inv = 1.0f / sqrtf(d2);
        *out_x *= inv;
        *out_y *= inv;
    }
}

void faculty175_face_maze_draw(uint32_t anim_ms)
{
    static float sx = 0.0f;
    static float sy = 0.0f;

    if (!draw_background()) {
        faculty175_display_fill_rgb565(rgb(224, 222, 214));
    }

    float tx = 0.0f;
    float ty = 0.0f;
    ball_target(anim_ms, &tx, &ty);
    sx += (tx - sx) * 0.28f;
    sy += (ty - sy) * 0.28f;

    const int cx = FACULTY175_LCD_W / 2;
    const int cy = FACULTY175_LCD_H / 2;
    const int bx = cx + (int)lrintf(sx * 150.0f);
    const int by = cy + (int)lrintf(sy * 150.0f);
    faculty175_display_fill_circle(bx + 3, by + 4, 12, rgb(48, 38, 26));
    faculty175_display_fill_circle(bx, by, 12, rgb(238, 182, 76));
    faculty175_display_fill_circle(bx - 4, by - 5, 4, rgb(255, 236, 158));
    faculty175_display_draw_circle(bx, by, 12, rgb(16, 17, 18));

    faculty175_display_flush();
}
