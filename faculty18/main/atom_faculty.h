#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define ATOM_FACULTY_BUST_W 368
#define ATOM_FACULTY_BUST_H 448

/** Full-screen bust draw scale (100 = edge-to-edge). */
#define ATOM_FACULTY_BUST_DISPLAY_SCALE_PCT 88
/** Logical-FB downward offset after content centering. */
#define ATOM_FACULTY_BUST_DISPLAY_Y_NUDGE 8

typedef enum {
    ATOM_FACULTY_BUST_IDLE = 0,
    ATOM_FACULTY_BUST_LOADING,
    ATOM_FACULTY_BUST_READY,
    ATOM_FACULTY_BUST_ERROR,
} atom_faculty_bust_status_t;

typedef void (*atom_faculty_ui_notify_fn)(void);

esp_err_t atom_faculty_init(void);

void atom_faculty_set_ui_notify(atom_faculty_ui_notify_fn fn);

/** Queue a tiny faculty-bust download for `slug` (no-op if already loaded or in flight). */
void atom_faculty_request_bust(const char *slug);

atom_faculty_bust_status_t atom_faculty_bust_status(void);

const char *atom_faculty_loaded_slug(void);

/** Decoded bust draw size (valid when status is READY). */
bool atom_faculty_bust_draw_size(int *w, int *h);

#ifdef __cplusplus
extern "C" {
#endif

/** Decode PNG bytes into bust-sized RGB565 + opaque mask (PNGdec). */
bool atom_faculty_png_decode(const uint8_t *png,
                             size_t png_len,
                             uint16_t *out,
                             uint8_t *opaque,
                             int *out_w,
                             int *out_h);

#ifdef __cplusplus
}
#endif

/** Opaque-pixel bbox center in bust coordinates (valid when bust is ready). */
bool atom_faculty_bust_content_center(int *cx, int *cy);

/**
 * Blit origin so bust content centers in `area_*`.
 * `panel_rotated_ccw`: logical FB is rotated 90° CCW when flushed to the panel.
 */
void atom_faculty_bust_blit_origin(int area_x,
                                   int area_y,
                                   int area_w,
                                   int area_h,
                                   bool panel_rotated_ccw,
                                   int *out_x,
                                   int *out_y);

/** Draw decoded bust into the framebuffer; returns true when pixels are shown. */
bool atom_faculty_draw_bust(int x, int y);
