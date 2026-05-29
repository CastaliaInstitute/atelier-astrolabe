#pragma once

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

esp_err_t atom_board_init(void);
void atom_board_set_backlight(uint8_t percent);

esp_err_t atom_audio_read(int16_t *samples, size_t sample_count, size_t *out_read, uint32_t timeout_ms);
esp_err_t atom_audio_write_pcm(const int16_t *samples, size_t sample_count, uint32_t timeout_ms);
esp_err_t atom_audio_set_sample_rate(uint32_t hz);
void atom_audio_set_speaker_mute(bool mute);

void atom_display_fill_rgb565(uint16_t color);
void atom_display_fill_rect(int x, int y, int w, int h, uint16_t color);
void atom_display_draw_rgb565(const uint16_t *pixels, int x, int y, int w, int h);
void atom_display_draw_status(atom_ui_state_t state, const char *faculty_name, const char *detail);

bool atom_button_pressed(void);
bool atom_button_just_pressed(void);
