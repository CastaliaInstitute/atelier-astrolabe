#pragma once

#include <Arduino_GFX_Library.h>
#include <cstdint>

#include "pin_config.h"

/** Shared panel center (466×466 round). */
constexpr int pm_face_lcd_cx = LCD_WIDTH / 2;
constexpr int pm_face_lcd_cy = LCD_HEIGHT / 2;

constexpr float pm_face_k_pi = 3.14159265f;
constexpr float pm_face_k_two_pi = pm_face_k_pi * 2.f;
constexpr float pm_face_hsv_s = 0.75f;
constexpr float pm_face_hsv_v = 0.14f;

uint16_t pm_face_color565_from_hsv(Arduino_GFX *out, float h_deg, float s, float v);
void pm_face_draw_centered_line(const char *text, int y, uint16_t fg, uint8_t textSizeX, uint8_t textSizeY);
void pm_face_draw_hand_radial(int cx, int cy, float ang, int len, uint16_t col, int half_w);
void pm_face_draw_label_at_polar(int rcx, int rcy, int r, float ang, const char *text, uint16_t col);
void pm_face_draw_radial_annulus_slice(int cx, int cy, float ang, int r0, int r1, uint16_t col, int half_w);
void pm_face_draw_circumference_rainbow_24h(bool valid);
/** Degrees clockwise from top (0 = now in rolling 12h mode). */
float pm_face_deg_to_rad(float deg_clockwise_from_top);
void pm_face_draw_annular_wedge(int cx, int cy, int r_inner, int r_outer, float start_deg, float end_deg,
                                uint16_t fill_col);
void pm_face_draw_daywheel_hue_ring_12h(int64_t now_unix, int r_inner, int r_outer);
void pm_face_draw_now_bead(int cx, int cy, int r, uint16_t col);
void pm_face_draw_thinking_progress_ring(float progress);
void pm_face_draw_voice_waves_overlay(bool outward, uint32_t t_ms);
void pm_face_draw_voice_wave_screen(bool outward, uint32_t t_ms, const char *label);
/** Radial “glowing gem” fill; `hue_deg_24h` = seconds-of-day × (360/86400), same as home/rim. */
uint16_t pm_face_draw_home_gem_glow(float hue_deg_24h);
/** Breath frame: gem disc only (does not redraw rainbow or fillScreen). */
void pm_face_draw_home_gem_breath_only(float hue_deg_24h);

/** Chakra-colored radial gem (home style). `wave_active` adds 2D ripples at `tone_hz`. */
void pm_face_draw_chakra_gem(int cx, int cy, int r_max, uint8_t cr, uint8_t cg, uint8_t cb,
                             float pulse_brightness, float wave_phase_rad, float tone_hz,
                             bool wave_active);
