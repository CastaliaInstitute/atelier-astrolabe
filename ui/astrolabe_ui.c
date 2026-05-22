#include "astrolabe_ui.h"

#include <math.h>
#include <stdio.h>

#include "lvgl.h"

static lv_obj_t *s_root;
static lv_obj_t *s_face_label;
static lv_obj_t *s_time_label;
static lv_obj_t *s_hint_label;
static lv_obj_t *s_dial;
static astrolabe_ui_face_t s_face = ASTROLABE_UI_FACE_DIGITAL;
static uint32_t s_elapsed_ms;

static int32_t ui_cx(void) { return ASTROLABE_UI_WIDTH / 2; }
static int32_t ui_cy(void) { return ASTROLABE_UI_HEIGHT / 2; }

static void clear_face(void) {
  lv_obj_clean(s_root);
  s_face_label = NULL;
  s_time_label = NULL;
  s_hint_label = NULL;
  s_dial = NULL;
}

static void style_screen(void) {
  lv_obj_set_style_bg_color(s_root, lv_color_hex(0x080a0f), 0);
  lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
}

static lv_obj_t *make_label(const char *text, int32_t y, const lv_font_t *font, uint32_t color) {
  lv_obj_t *label = lv_label_create(s_root);
  lv_label_set_text(label, text);
  lv_obj_set_width(label, ASTROLABE_UI_WIDTH - 48);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_align(label, LV_ALIGN_TOP_MID, 0, y);
  return label;
}

static void draw_round_mask(void) {
  lv_obj_t *mask = lv_obj_create(s_root);
  lv_obj_remove_style_all(mask);
  lv_obj_set_size(mask, ASTROLABE_UI_WIDTH - 8, ASTROLABE_UI_HEIGHT - 8);
  lv_obj_center(mask);
  lv_obj_set_style_radius(mask, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(mask, 2, 0);
  lv_obj_set_style_border_color(mask, lv_color_hex(0x2e6f85), 0);
  lv_obj_set_style_bg_opa(mask, LV_OPA_TRANSP, 0);
}

static void create_digital_face(void) {
  clear_face();
  style_screen();
  draw_round_mask();

  s_face_label = make_label("Astrolabe", 80, &lv_font_montserrat_22, 0x7fcde0);
  s_time_label = make_label("--:--:--", 177, &lv_font_montserrat_48, 0xf8fbff);
  s_hint_label = make_label("LVGL browser simulator", 298, &lv_font_montserrat_16, 0x8e9ba8);

  lv_obj_t *arc = lv_arc_create(s_root);
  lv_obj_set_size(arc, 382, 382);
  lv_obj_center(arc);
  lv_arc_set_range(arc, 0, 1000);
  lv_arc_set_value(arc, 0);
  lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
  lv_obj_set_style_arc_width(arc, 8, LV_PART_MAIN);
  lv_obj_set_style_arc_width(arc, 8, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(arc, lv_color_hex(0x1c2630), LV_PART_MAIN);
  lv_obj_set_style_arc_color(arc, lv_color_hex(0xffcf66), LV_PART_INDICATOR);
  s_dial = arc;
}

static void draw_tick(lv_layer_t *layer, int32_t index) {
  const float a = ((float)index / 60.0f) * 6.28318530718f - 1.57079632679f;
  const int32_t outer = 203;
  const int32_t inner = (index % 5 == 0) ? 178 : 190;
  lv_point_t points[2] = {
      {ui_cx() + (int32_t)(cosf(a) * outer), ui_cy() + (int32_t)(sinf(a) * outer)},
      {ui_cx() + (int32_t)(cosf(a) * inner), ui_cy() + (int32_t)(sinf(a) * inner)},
  };

  lv_draw_line_dsc_t dsc;
  lv_draw_line_dsc_init(&dsc);
  dsc.p1 = lv_point_to_precise(&points[0]);
  dsc.p2 = lv_point_to_precise(&points[1]);
  dsc.color = lv_color_hex(index % 5 == 0 ? 0xd7e8ee : 0x52636f);
  dsc.width = index % 5 == 0 ? 4 : 2;
  dsc.round_end = 1;
  lv_draw_line(layer, &dsc);
}

static void draw_hand(lv_layer_t *layer, float unit, int32_t length, uint32_t color, int32_t width) {
  const float a = unit * 6.28318530718f - 1.57079632679f;
  lv_point_t points[2] = {
      {ui_cx(), ui_cy()},
      {ui_cx() + (int32_t)(cosf(a) * length), ui_cy() + (int32_t)(sinf(a) * length)},
  };

  lv_draw_line_dsc_t dsc;
  lv_draw_line_dsc_init(&dsc);
  dsc.p1 = lv_point_to_precise(&points[0]);
  dsc.p2 = lv_point_to_precise(&points[1]);
  dsc.color = lv_color_hex(color);
  dsc.width = width;
  dsc.round_end = 1;
  lv_draw_line(layer, &dsc);
}

static void classic_draw_event(lv_event_t *event) {
  lv_layer_t *layer = lv_event_get_layer(event);
  const uint32_t day_ms = s_elapsed_ms % 86400000u;
  const float second = (float)(day_ms % 60000u) / 60000.0f;
  const float minute = (float)(day_ms % 3600000u) / 3600000.0f;
  const float hour = (float)(day_ms % 43200000u) / 43200000.0f;

  for (int32_t i = 0; i < 60; ++i) {
    draw_tick(layer, i);
  }
  draw_hand(layer, hour, 88, 0xf8fbff, 10);
  draw_hand(layer, minute, 139, 0x7fcde0, 7);
  draw_hand(layer, second, 166, 0xffcf66, 3);

  lv_draw_arc_dsc_t arc_dsc;
  lv_draw_arc_dsc_init(&arc_dsc);
  arc_dsc.color = lv_color_hex(0x2e6f85);
  arc_dsc.width = 3;
  arc_dsc.center.x = ui_cx();
  arc_dsc.center.y = ui_cy();
  arc_dsc.radius = 213;
  arc_dsc.start_angle = 0;
  arc_dsc.end_angle = 360;
  lv_draw_arc(layer, &arc_dsc);
}

static void create_classic_face(void) {
  clear_face();
  style_screen();

  s_dial = lv_obj_create(s_root);
  lv_obj_remove_style_all(s_dial);
  lv_obj_set_size(s_dial, ASTROLABE_UI_WIDTH, ASTROLABE_UI_HEIGHT);
  lv_obj_add_event_cb(s_dial, classic_draw_event, LV_EVENT_DRAW_MAIN, NULL);

  s_face_label = make_label("CLASSIC", 102, &lv_font_montserrat_18, 0x8e9ba8);
  s_time_label = make_label("--:--:--", 224, &lv_font_montserrat_24, 0xf8fbff);
}

static void refresh_time_labels(void) {
  const uint32_t day_s = (s_elapsed_ms / 1000u) % 86400u;
  const uint32_t hour = day_s / 3600u;
  const uint32_t minute = (day_s / 60u) % 60u;
  const uint32_t second = day_s % 60u;
  char time_text[16];
  snprintf(time_text, sizeof(time_text), "%02u:%02u:%02u", (unsigned)hour, (unsigned)minute, (unsigned)second);
  if (s_time_label) {
    lv_label_set_text(s_time_label, time_text);
  }
  if (s_face == ASTROLABE_UI_FACE_DIGITAL && s_dial) {
    lv_arc_set_value(s_dial, (int32_t)(((s_elapsed_ms % 60000u) * 1000u) / 60000u));
  }
}

void astrolabe_ui_init(void) {
  s_root = lv_screen_active();
  s_elapsed_ms = 12u * 3600u * 1000u;
  astrolabe_ui_set_face(ASTROLABE_UI_FACE_DIGITAL);
}

void astrolabe_ui_set_face(astrolabe_ui_face_t face) {
  if (!s_root) {
    return;
  }
  s_face = face;
  switch (s_face) {
  case ASTROLABE_UI_FACE_CLASSIC:
    create_classic_face();
    break;
  case ASTROLABE_UI_FACE_DIGITAL:
  default:
    s_face = ASTROLABE_UI_FACE_DIGITAL;
    create_digital_face();
    break;
  }
  refresh_time_labels();
}

astrolabe_ui_face_t astrolabe_ui_current_face(void) { return s_face; }

void astrolabe_ui_tick(uint32_t elapsed_ms) {
  s_elapsed_ms += elapsed_ms;
  refresh_time_labels();
  if (s_dial) {
    lv_obj_invalidate(s_dial);
  }
}
