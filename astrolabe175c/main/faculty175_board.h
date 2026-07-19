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

typedef struct {
    bool enabled;
    uint32_t noise_rms;
    uint32_t last_rms;
    uint32_t last_gain_q8;
    uint32_t frames;
} faculty175_audio_noise_status_t;

#ifdef __cplusplus
extern "C" {
#endif

/** Shared I2C bus for AXP2101, ES7210, ES8311. */
i2c_master_bus_handle_t faculty175_i2c_bus(void);
bool faculty175_i2c_probe(uint8_t addr_7bit);
esp_err_t faculty175_i2c_write(uint8_t addr_7bit, const uint8_t *data, size_t len);
esp_err_t faculty175_i2c_write_read(uint8_t addr_7bit,
                                    const uint8_t *wr,
                                    size_t wr_len,
                                    uint8_t *rd,
                                    size_t rd_len);

esp_err_t faculty175_board_init(void);
bool faculty175_board_audio_ready(void);
/** 1.75C has no TCA9554 — always false (QA compat). */
bool faculty175_board_pi4ioe_ok(void);
/** Peak abs sample from boot mic probe (0 = likely no capture). */
int32_t faculty175_board_mic_probe_peak(void);
void faculty175_board_set_backlight(uint8_t percent);
void faculty175_board_display_on(bool on);
/** Put the AMOLED in sleep-in and hold its shared display/touch reset low. */
esp_err_t faculty175_display_prepare_deep_sleep(void);

esp_err_t faculty175_audio_read(int16_t *samples, size_t sample_count, size_t *out_read, uint32_t timeout_ms);
esp_err_t faculty175_audio_prepare_capture(uint32_t timeout_ms);
esp_err_t faculty175_audio_read_tdm_raw(int16_t *samples,
                                        size_t frame_count,
                                        size_t *out_frames,
                                        uint32_t timeout_ms);
esp_err_t faculty175_audio_write_pcm(const int16_t *samples, size_t sample_count, uint32_t timeout_ms);
esp_err_t faculty175_audio_reset_speaker(uint32_t timeout_ms);
/** Reopen the ES7210 route and restart I2S RX after speaker playback. */
esp_err_t faculty175_audio_reset_capture(uint32_t timeout_ms);
esp_err_t faculty175_audio_set_sample_rate(uint32_t hz);
void faculty175_audio_set_speaker_mute(bool mute);
void faculty175_audio_set_speaker_pa_level(bool enabled);
void faculty175_audio_set_speaker_volume(uint8_t volume);
/** Quiesce codecs, amplifier, and both I2S channels immediately before deep sleep. */
esp_err_t faculty175_audio_prepare_deep_sleep(uint32_t timeout_ms);
void faculty175_audio_noise_suppression_set_enabled(bool enabled);
void faculty175_audio_noise_suppression_reset(void);
void faculty175_audio_noise_suppression_status(faculty175_audio_noise_status_t *out);
esp_err_t faculty175_board_play_boot_chime(void);

void faculty175_display_fill_rgb565(uint16_t color);
void faculty175_display_fill_rect(int x, int y, int w, int h, uint16_t color);
void faculty175_display_draw_rgb565(const uint16_t *pixels, int x, int y, int w, int h);
void faculty175_display_draw_rgb565_stride(const uint16_t *pixels, int src_stride_pixels, int x, int y, int w, int h);
void faculty175_display_draw_pixel(int x, int y, uint16_t color);
void faculty175_display_draw_line(int x0, int y0, int x1, int y1, uint16_t color);
void faculty175_display_draw_circle(int cx, int cy, int r, uint16_t color);
void faculty175_display_fill_circle(int cx, int cy, int r, uint16_t color);
void faculty175_display_draw_text(const char *text, int x, int y, uint16_t color);
void faculty175_display_draw_centered_text(const char *text, int y, uint16_t color);
void faculty175_display_draw_bezel_label(const char *text, bool bottom, int radius, uint32_t scroll_ms, uint16_t color);
void faculty175_display_flush(void);
void faculty175_display_flush_rect(int x, int y, int w, int h);
void faculty175_display_flush_suspended_set(bool suspended);
size_t faculty175_display_prepare_ota(void);
void faculty175_display_resume_after_ota_error(void);
size_t faculty175_display_frame_pixel_count(void);
bool faculty175_display_frame_copy(uint16_t *out, size_t pixel_count);
void faculty175_display_frame_compose_carousel(const uint16_t *from, const uint16_t *to, int shift_px);
void faculty175_display_frame_compose_vertical(const uint16_t *from, const uint16_t *to, int shift_px);
void faculty175_display_frame_compose_radial(const uint16_t *from, const uint16_t *to, int radius_px);
void faculty175_display_frame_compose_nav_preview(const uint16_t *center,
                                                  const uint16_t *left,
                                                  const uint16_t *right,
                                                  const uint16_t *up,
                                                  const uint16_t *down);
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
void faculty175_display_draw_pocketwatch(const char *detail, uint32_t anim_ms, bool boot_mode);
void faculty175_display_boot_progress(const char *detail, uint8_t step, uint8_t total, bool active);
void faculty175_display_waveform_update(const uint8_t *waveform,
                                        const uint8_t *waveform_stream,
                                        size_t waveform_len,
                                        bool visible);
void faculty175_display_nav_mode_set(bool enabled);
void faculty175_display_touch_visual_update(int16_t x, int16_t y, bool down, uint32_t now_ms);
void faculty175_display_lock(void);
bool faculty175_display_lock_timeout(uint32_t timeout_ms);
void faculty175_display_unlock(void);
size_t faculty175_display_bmp_size(void);
int faculty175_display_write_bmp(FILE *out);
typedef esp_err_t (*faculty175_display_write_cb_t)(void *ctx, const uint8_t *data, size_t len);
size_t faculty175_display_bmp565_size(void);
esp_err_t faculty175_display_write_bmp565(faculty175_display_write_cb_t write_cb, void *ctx);

bool faculty175_button_pressed(void);
bool faculty175_button_just_pressed(void);
void faculty175_button_inject_press(void);

#ifdef __cplusplus
}
#endif
