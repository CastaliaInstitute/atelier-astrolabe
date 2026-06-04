#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define ATOM_FACULTY_BUST_W 128
#define ATOM_FACULTY_BUST_H 128

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

/** Draw decoded bust into the framebuffer; returns true when pixels are shown. */
bool atom_faculty_draw_bust(int x, int y);
