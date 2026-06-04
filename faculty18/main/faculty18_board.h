#pragma once

#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"

/** Waveshare ESP32-S3-Touch-AMOLED-1.8 visible area (logical framebuffer). */
#define FACULTY18_LCD_W 368
#define FACULTY18_LCD_H 448
#define FACULTY18_AUDIO_RATE 16000

typedef enum {
    FACULTY18_UI_BOOT = 0,
    FACULTY18_UI_WIFI,
    FACULTY18_UI_LISTEN,
    FACULTY18_UI_CAPTURE,
    FACULTY18_UI_THINK,
    FACULTY18_UI_SPEAK,
    FACULTY18_UI_ERROR,
} faculty18_ui_state_t;

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t faculty18_board_init(void);
bool faculty18_board_audio_ready(void);
/** TCA9554 powered the AMOLED rail during init. */
bool faculty18_board_pi4ioe_ok(void);
/** Peak abs sample from boot mic probe (0 = likely no capture). */
int32_t faculty18_board_mic_probe_peak(void);
void faculty18_board_set_backlight(uint8_t percent);

esp_err_t faculty18_audio_read(int16_t *samples, size_t sample_count, size_t *out_read, uint32_t timeout_ms);
esp_err_t faculty18_audio_write_pcm(const int16_t *samples, size_t sample_count, uint32_t timeout_ms);
esp_err_t faculty18_audio_set_sample_rate(uint32_t hz);
void faculty18_audio_set_speaker_mute(bool mute);

void faculty18_display_fill_rgb565(uint16_t color);
void faculty18_display_fill_rect(int x, int y, int w, int h, uint16_t color);
void faculty18_display_draw_rgb565(const uint16_t *pixels, int x, int y, int w, int h);
/** Logical RGB565 — for PNGdec / asset compare. */
uint16_t faculty18_display_pack_rgb888(uint8_t r, uint8_t g, uint8_t b);
/** Convert logical RGB565 asset pixels to framebuffer format. */
uint16_t faculty18_display_fb_from_logical565(uint16_t logical565);
/** Pack 8-bit RGB into framebuffer RGB565. */
uint16_t faculty18_display_rgb888(uint8_t r, uint8_t g, uint8_t b);

/** PNGdec background key color (00BBGGRR) matching the UI fill (black). */
uint32_t faculty18_display_bkgd_u32(void);

/** Blit RGB565 pixels; skip indices where opaque[i] == 0 (transparent). Returns pixels written. */
int faculty18_display_blit_rgb565_masked(const uint16_t *pixels,
                                          const uint8_t *opaque,
                                          int x,
                                          int y,
                                          int w,
                                          int h);
void faculty18_display_draw_status(faculty18_ui_state_t state,
                                   const char *faculty_name,
                                   const char *detail);

/** 24-bit BMP size for the current framebuffer. */
size_t faculty18_display_bmp_size(void);

/** Write a bottom-up 24-bit BMP to `out` (matches panel rotation + color wire). */
int faculty18_display_write_bmp(FILE *out);

bool faculty18_button_pressed(void);
bool faculty18_button_just_pressed(void);

#ifdef __cplusplus
}
#endif
