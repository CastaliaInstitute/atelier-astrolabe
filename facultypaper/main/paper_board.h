#pragma once

#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"
#include "paper_memory.h"

#define PAPER_LCD_W 400
#define PAPER_LCD_H 600
#define PAPER_AUDIO_RATE 16000

typedef enum {
    PAPER_UI_BOOT = 0,
    PAPER_UI_WIFI,
    PAPER_UI_LISTEN,
    PAPER_UI_CAPTURE,
    PAPER_UI_THINK,
    PAPER_UI_SPEAK,
    PAPER_UI_ERROR,
} paper_ui_state_t;

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t paper_board_init(void);
bool paper_board_audio_ready(void);
/** M5PM1 power/IO controller initialized for EPD/SD/power rails. */
bool paper_board_pi4ioe_ok(void);
/** Raw M5PM1 wake-source flags latched during board init (0 when unavailable). */
uint8_t paper_board_pm1_wake_source(void);
/** True when M5PM1 reported power-button wake during board init. */
bool paper_board_power_button_wake(void);
/** Peak abs sample from boot mic probe (0 = likely no capture). */
int32_t paper_board_mic_probe_peak(void);
void paper_board_set_backlight(uint8_t percent);
bool paper_board_sd_ready(void);
const char *paper_capture_mount_path(void);
const char *paper_capture_file_path(void);
const char *paper_capture_partition_label(void);
bool paper_capture_skip_spiffs_mount(void);

esp_err_t paper_audio_read(int16_t *samples, size_t sample_count, size_t *out_read, uint32_t timeout_ms);
esp_err_t paper_audio_write_pcm(const int16_t *samples, size_t sample_count, uint32_t timeout_ms);
esp_err_t paper_audio_set_sample_rate(uint32_t hz);
void paper_audio_set_speaker_mute(bool mute);
esp_err_t paper_audio_raw_probe(int32_t *peak, size_t *samples, size_t *nonzero, uint32_t timeout_ms);
esp_err_t paper_audio_codec_reg(int reg, int *value);

void paper_display_fill_rgb565(uint16_t color);
void paper_display_fill_rect(int x, int y, int w, int h, uint16_t color);
void paper_display_draw_rgb565(const uint16_t *pixels, int x, int y, int w, int h);
/** Logical RGB565 (no panel invert) - for PNGdec / asset compare. */
uint16_t paper_display_pack_rgb888(uint8_t r, uint8_t g, uint8_t b);
/** Convert logical RGB565 asset pixels to framebuffer format. */
uint16_t paper_display_fb_from_logical565(uint16_t logical565);
/** Pack 8-bit RGB into framebuffer RGB565 (panel invert applied). */
uint16_t paper_display_rgb888(uint8_t r, uint8_t g, uint8_t b);

/** PNGdec background key color (00BBGGRR) matching the UI fill (black). */
uint32_t paper_display_bkgd_u32(void);

/** Blit RGB565 pixels; skip indices where opaque[i] == 0 (transparent). */
void paper_display_blit_rgb565_masked(const uint16_t *pixels, const uint8_t *opaque, int x, int y, int w, int h);
void paper_display_draw_status(paper_ui_state_t state,
                            const char *faculty_name,
                            const char *detail,
                            uint32_t anim_ms,
                            const uint8_t *waveform,
                            size_t waveform_len,
                            const paper_memory_message_t *messages,
                            size_t message_count,
                            size_t scroll_offset);
void paper_display_draw_conversation(const paper_memory_message_t *messages,
                                     size_t message_count,
                                     size_t scroll_offset);

/** 24-bit BMP size for the current framebuffer. */
size_t paper_display_bmp_size(void);

/** Write a bottom-up 24-bit BMP to `out`. Applies panel color invert so PC colors match the LCD. */
int paper_display_write_bmp(FILE *out);

bool paper_button_pressed(void);
bool paper_button_just_pressed(void);
/** Raw active-low button mask: bit0=A(GPIO9), bit1=B(GPIO10), bit2=C(GPIO1). */
uint8_t paper_button_debug_mask(void);
bool paper_button_a_just_pressed(void);
bool paper_button_b_just_pressed(void);
bool paper_button_c_just_pressed(void);
bool paper_button_up_pressed(void);
bool paper_button_down_pressed(void);
bool paper_button_up_just_pressed(void);
bool paper_button_down_just_pressed(void);

#ifdef __cplusplus
}
#endif
