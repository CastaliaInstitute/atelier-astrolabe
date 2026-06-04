#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/** Round 1.75″ panel — fetch/decode smaller than full bleed for bezel margin. */
#define FACULTY175_FACULTY_BUST_W 380
#define FACULTY175_FACULTY_BUST_H 380

/** Blit decoded bust at native fetch size (centering handled by board). */
#define FACULTY175_FACULTY_BUST_DISPLAY_SCALE_PCT 100
/** Logical-FB vertical offset after content centering. Negative moves the bust up. */
#define FACULTY175_FACULTY_BUST_DISPLAY_Y_NUDGE (-10)

typedef enum {
    FACULTY175_FACULTY_BUST_IDLE = 0,
    FACULTY175_FACULTY_BUST_LOADING,
    FACULTY175_FACULTY_BUST_READY,
    FACULTY175_FACULTY_BUST_ERROR,
} faculty175_faculty_bust_status_t;

typedef void (*faculty175_faculty_ui_notify_fn)(void);

esp_err_t faculty175_faculty_init(void);

void faculty175_faculty_set_ui_notify(faculty175_faculty_ui_notify_fn fn);

/** Queue a tiny faculty-bust download for `slug` (no-op if already loaded or in flight). */
void faculty175_faculty_request_bust(const char *slug);

/** Evict any cached/loaded bust for `slug` and force a network download. */
void faculty175_faculty_request_bust_download(const char *slug);

/** Background-download roster busts missing from SPIFFS (after Wi-Fi is up). */
void faculty175_faculty_prefetch_roster(void);

faculty175_faculty_bust_status_t faculty175_faculty_bust_status(void);

const char *faculty175_faculty_loaded_slug(void);

#ifdef __cplusplus
extern "C" {
#endif

/** Decode PNG bytes into bust-sized RGB565 + opaque mask (PNGdec). */
bool faculty175_faculty_png_decode(const uint8_t *png,
                             size_t png_len,
                             uint16_t *out,
                             uint8_t *opaque,
                             int *out_w,
                             int *out_h);

#ifdef __cplusplus
}
#endif

/** Opaque-pixel bbox center in bust coordinates (valid when bust is ready). */
bool faculty175_faculty_bust_content_center(int *cx, int *cy);

/**
 * Blit origin so bust content centers in `area_*`.
 * `panel_rotated_ccw`: logical FB is rotated 90° CCW when flushed to the panel.
 */
void faculty175_faculty_bust_blit_origin(int area_x,
                                   int area_y,
                                   int area_w,
                                   int area_h,
                                   bool panel_rotated_ccw,
                                   int *out_x,
                                   int *out_y);

/** Draw decoded bust into the framebuffer; returns true when pixels are shown. */
bool faculty175_faculty_draw_bust(int x, int y);
