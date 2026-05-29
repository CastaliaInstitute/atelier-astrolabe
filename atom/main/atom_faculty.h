#pragma once

#include <stdbool.h>

#include "esp_err.h"

#define ATOM_FACULTY_BUST_W 96
#define ATOM_FACULTY_BUST_H 96

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

/** Draw decoded bust into the framebuffer; returns true when pixels are shown. */
bool atom_faculty_draw_bust(int x, int y);
