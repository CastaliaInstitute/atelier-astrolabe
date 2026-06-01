#pragma once

#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"

#define ATOM_LCD_W 128
#define ATOM_LCD_H 128
#define ATOM_AUDIO_RATE 16000

typedef enum {
    ATOM_UI_BOOT = 0,
    ATOM_UI_WIFI,
    ATOM_UI_LISTEN,
    ATOM_UI_CAPTURE,
    ATOM_UI_THINK,
    ATOM_UI_SPEAK,
    ATOM_UI_ERROR,
} atom_ui_state_t;

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t atom_board_init(void);
bool atom_board_audio_ready(void);
/** PI4IOE on Voice Base powered the ES8311 path during init. */
bool atom_board_pi4ioe_ok(void);
/** Peak abs sample from boot mic probe (0 = likely no capture). */
int32_t atom_board_mic_probe_peak(void);
void atom_board_set_backlight(uint8_t percent);

esp_err_t atom_audio_read(int16_t *samples, size_t sample_count, size_t *out_read, uint32_t timeout_ms);
esp_err_t atom_audio_write_pcm(const int16_t *samples, size_t sample_count, uint32_t timeout_ms);
esp_err_t atom_audio_set_sample_rate(uint32_t hz);
void atom_audio_set_speaker_mute(bool mute);

void atom_display_fill_rgb565(uint16_t color);
void atom_display_fill_rect(int x, int y, int w, int h, uint16_t color);
void atom_display_draw_rgb565(const uint16_t *pixels, int x, int y, int w, int h);
/** Logical RGB565 (no panel invert) — for PNGdec / asset compare. */
uint16_t atom_display_pack_rgb888(uint8_t r, uint8_t g, uint8_t b);
/** Convert logical RGB565 asset pixels to framebuffer format. */
uint16_t atom_display_fb_from_logical565(uint16_t logical565);
/** Pack 8-bit RGB into framebuffer RGB565 (panel invert applied). */
uint16_t atom_display_rgb888(uint8_t r, uint8_t g, uint8_t b);

/** PNGdec background key color (00BBGGRR) matching the UI fill (black). */
uint32_t atom_display_bkgd_u32(void);

/** Blit RGB565 pixels; skip indices where opaque[i] == 0 (transparent). */
void atom_display_blit_rgb565_masked(const uint16_t *pixels, const uint8_t *opaque, int x, int y, int w, int h);
void atom_display_draw_status(atom_ui_state_t state,
                            const char *faculty_name,
                            const char *detail,
                            uint32_t anim_ms,
                            const uint8_t *waveform,
                            size_t waveform_len);

/** 24-bit BMP size for the current framebuffer (128×128). */
size_t atom_display_bmp_size(void);

/** Write a bottom-up 24-bit BMP to `out`. Applies panel color invert so PC colors match the LCD. */
int atom_display_write_bmp(FILE *out);

bool atom_button_pressed(void);
bool atom_button_just_pressed(void);

#ifdef __cplusplus
}
#endif
