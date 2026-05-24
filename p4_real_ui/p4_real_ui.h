#pragma once

#include <stdint.h>

#include "lvgl.h"
#include "pin_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef ASTROLABE_REAL_UI_WIDTH
#define ASTROLABE_REAL_UI_WIDTH LCD_WIDTH
#endif
#ifndef ASTROLABE_REAL_UI_HEIGHT
#define ASTROLABE_REAL_UI_HEIGHT LCD_HEIGHT
#endif
#define ASTROLABE_REAL_UI_FACE_MOON 5

void astrolabe_real_ui_init_in(lv_obj_t *parent);
void astrolabe_real_ui_tick(uint32_t elapsed_ms);
void astrolabe_real_ui_set_face(int face);
void astrolabe_real_ui_cycle(int delta);
int astrolabe_real_ui_current_face(void);
int astrolabe_real_ui_face_count(void);
const char *astrolabe_real_ui_face_name(int face);
int astrolabe_real_ui_parse_face_name(const char *name);
int astrolabe_real_ui_width(void);
int astrolabe_real_ui_height(void);
const uint16_t *astrolabe_real_ui_framebuffer(void);

#ifdef __cplusplus
}
#endif
