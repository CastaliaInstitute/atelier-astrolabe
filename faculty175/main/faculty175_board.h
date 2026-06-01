#pragma once

#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

/** Waveshare ESP32-S3-Touch-AMOLED-1.75C round visible area (466×466). */
#define FACULTY175_LCD_W 466
#define FACULTY175_LCD_H 466
#define FACULTY175_AUDIO_RATE 16000

typedef enum {
    FACULTY175_UI_BOOT = 0,
    FACULTY175_UI_WIFI,
    FACULTY175_UI_LISTEN,
    FACULTY175_UI_CAPTURE,
    FACULTY175_UI_THINK,
    FACULTY175_UI_SPEAK,
    FACULTY175_UI_ERROR,
} faculty175_ui_state_t;

#ifdef __cplusplus
extern "C" {
#endif

/** Shared I2C bus for AXP2101, ES7210, ES8311. */
i2c_master_bus_handle_t faculty175_i2c_bus(void);

esp_err_t faculty175_board_init(void);
bool faculty175_board_audio_ready(void);
/** 1.75C has no TCA9554 — always false (QA compat). */
bool faculty175_board_pi4ioe_ok(void);
/** Peak abs sample from boot mic probe (0 = likely no capture). */
int32_t faculty175_board_mic_probe_peak(void);
void faculty175_board_set_backlight(uint8_t percent);

esp_err_t faculty175_audio_read(int16_t *samples, size_t sample_count, size_t *out_read, uint32_t timeout_ms);
esp_err_t faculty175_audio_write_pcm(const int16_t *samples, size_t sample_count, uint32_t timeout_ms);
esp_err_t faculty175_audio_set_sample_rate(uint32_t hz);
void faculty175_audio_set_speaker_mute(bool mute);

void faculty175_display_fill_rgb565(uint16_t color);
void faculty175_display_fill_rect(int x, int y, int w, int h, uint16_t color);
void faculty175_display_draw_rgb565(const uint16_t *pixels, int x, int y, int w, int h);
uint16_t faculty175_display_pack_rgb888(uint8_t r, uint8_t g, uint8_t b);
uint16_t faculty175_display_fb_from_logical565(uint16_t logical565);
uint16_t faculty175_display_rgb888(uint8_t r, uint8_t g, uint8_t b);
uint32_t faculty175_display_bkgd_u32(void);
void faculty175_display_blit_rgb565_masked(const uint16_t *pixels,
                                           const uint8_t *opaque,
                                           int x,
                                           int y,
                                           int w,
                                           int h);
void faculty175_display_draw_status(faculty175_ui_state_t state,
                                    const char *faculty_name,
                                    const char *detail,
                                    uint32_t anim_ms,
                                    const uint8_t *waveform,
                                    const uint8_t *waveform_stream,
                                    size_t waveform_len);
size_t faculty175_display_bmp_size(void);
int faculty175_display_write_bmp(FILE *out);

bool faculty175_button_pressed(void);
bool faculty175_button_just_pressed(void);

#ifdef __cplusplus
}
#endif
