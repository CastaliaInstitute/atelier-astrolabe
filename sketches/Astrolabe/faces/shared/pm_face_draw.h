#pragma once

#include <Arduino_GFX_Library.h>
#include <cstdint>

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

/**
 * Arc label band along a circular path (per-face placement).
 * Angles are clockwise from top (up = 0°/360°). Arc runs clockwise from start_deg to end_deg;
 * when end < start after normalization, the arc wraps through 0° (e.g. 270→90 is the top half).
 * Examples: 90–270 bottom half; -45–45 top quarter; 0–360 full ring.
 */
struct PmFaceArcLabelStyle {
  float start_deg;     /**< Required. */
  float end_deg;       /**< Required. */
  int r_px;            /**< Polar radius; 0 → just inside rainbow inner edge. */
  uint8_t text_size_x; /**< 0 → 1. */
  uint8_t text_size_y; /**< 0 → 1. */
  uint16_t color;      /**< 0 → light foreground. */
};

void pm_face_draw_arc_label_static(const char *text, const PmFaceArcLabelStyle *style);
/** Marquee when text is wider than the arc; `scroll_px_per_sec` ≤ 0 uses default speed. */
void pm_face_draw_arc_label_scroll(const char *text, uint32_t t_ms, float scroll_px_per_sec,
                                   const PmFaceArcLabelStyle *style);
