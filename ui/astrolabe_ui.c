#include "astrolabe_ui.h"

#include <math.h>
#include <stdio.h>

#include "lvgl.h"

static lv_obj_t *s_root;
static lv_obj_t *s_face_label;
static lv_obj_t *s_time_label;
static lv_obj_t *s_hint_label;
static lv_obj_t *s_dial;
static astrolabe_ui_face_t s_face = ASTROLABE_UI_FACE_CLASSIC_ANALOG;
static uint32_t s_elapsed_ms;

typedef struct {
  const char *name;
  const char *line1;
  const char *line2;
  uint32_t accent;
} face_meta_t;

static const face_meta_t k_faces[ASTROLABE_UI_FACE_COUNT] = {
    {"Classic", "commonplace journal home", "hue analog clock", 0x7fcde0},
    {"Apocalypso", "RISK PROFILE", "impact vector", 0xff6b6b},
    {"Digital", "", "", 0xffcf66},
    {"Spotify", "Vinyl Queue", "demo stream", 0x1ed760},
    {"Astrology", "transit wheel", "zodiac + planets", 0xb891ff},
    {"Moon", "lunar phase disk", "daily fortune", 0xd7e8ee},
    {"Calcifer", "RIGHT NOW", "Hue Daywheel", 0xff8a3d},
    {"Castalia", "CASTALIA", "scan phone", 0x6cc7ff},
    {"Settings", "WiFi", "serial: wifi SSID pass", 0x92a4b8},
    {"Synastry", "synastry", "dual natal wheel", 0xff99cc},
    {"Spectrum", "polar audio visualizer", "FFT rings", 0x00d4ff},
    {"Chakra", "ROOT  396 Hz", "solfeggio tone wheel", 0xff5bbd},
    {"Bowl", "singing bowl", "drag rainbow rim", 0xd6b56d},
    {"Rocket", "LAUNCH CLOCK", "14-day dial", 0xff734d},
    {"Radar", "NEARBY", "BLE peer radar", 0x6dff91},
    {"Faculty", "ASK FACULTY", "recent conversations", 0xffe08a},
    {"Weather", "72 deg", "temp + humidity rings", 0x63b3ff},
    {"Quotes", "quote of the day", "tiny faculty bust", 0xd9c7ff},
    {"Transits", "LIVE TRANSITS", "4-day motion + countdown", 0xa4e3ff},
    {"Tarot", "0  THE FOOL", "daily major", 0xd99a5f},
    {"Notes", "NOTES", "hold PWR to dictate", 0xb4c6d8},
    {"Ocarina", "Ocarina C", "tap holes", 0x8fd6c8},
    {"Bongo", "Bongo", "center low / rim high", 0xd8865b},
    {"Piano", "Circular Piano", "white outer  black inner", 0xf4f7fb},
    {"Level", "LEVEL", "FORWARD", 0x9dff7f},
    {"Tuning", "TUNING", "listening", 0xffa64d},
    {"PanDrum", "PanDrum", "14-note handpan", 0xb8e0ff},
    {"Alethiometer", "ALETHIOMETER", "36 symbols / 4 needles", 0xf0c36a},
    {"Runes", "RUNES", "past  present  future", 0xc1d6a4},
};

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

static void draw_line(lv_layer_t *layer, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color,
                      int32_t width) {
  lv_point_t points[2] = {{x0, y0}, {x1, y1}};
  lv_draw_line_dsc_t dsc;
  lv_draw_line_dsc_init(&dsc);
  dsc.p1 = lv_point_to_precise(&points[0]);
  dsc.p2 = lv_point_to_precise(&points[1]);
  dsc.color = lv_color_hex(color);
  dsc.width = width;
  dsc.round_end = 1;
  lv_draw_line(layer, &dsc);
}

static void draw_circle(lv_layer_t *layer, int32_t x, int32_t y, int32_t r, uint32_t color, bool fill,
                        int32_t width) {
  lv_area_t area = {x - r, y - r, x + r, y + r};
  lv_draw_rect_dsc_t dsc;
  lv_draw_rect_dsc_init(&dsc);
  dsc.radius = LV_RADIUS_CIRCLE;
  dsc.bg_opa = fill ? LV_OPA_COVER : LV_OPA_TRANSP;
  dsc.bg_color = lv_color_hex(color);
  dsc.border_opa = LV_OPA_COVER;
  dsc.border_color = lv_color_hex(color);
  dsc.border_width = fill ? 0 : width;
  lv_draw_rect(layer, &dsc, &area);
}

static void draw_rect(lv_layer_t *layer, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color) {
  lv_area_t area = {x0, y0, x1, y1};
  lv_draw_rect_dsc_t dsc;
  lv_draw_rect_dsc_init(&dsc);
  dsc.bg_opa = LV_OPA_COVER;
  dsc.bg_color = lv_color_hex(color);
  lv_draw_rect(layer, &dsc, &area);
}

static void draw_arc(lv_layer_t *layer, int32_t radius, int32_t start_angle, int32_t end_angle, uint32_t color,
                     int32_t width) {
  lv_draw_arc_dsc_t dsc;
  lv_draw_arc_dsc_init(&dsc);
  dsc.color = lv_color_hex(color);
  dsc.width = width;
  dsc.center.x = ui_cx();
  dsc.center.y = ui_cy();
  dsc.radius = radius;
  dsc.start_angle = start_angle;
  dsc.end_angle = end_angle;
  lv_draw_arc(layer, &dsc);
}

static void draw_radial_line(lv_layer_t *layer, float deg, int32_t r0, int32_t r1, uint32_t color, int32_t width) {
  const float a = (deg - 90.0f) * 0.01745329252f;
  draw_line(layer, ui_cx() + (int32_t)(cosf(a) * r0), ui_cy() + (int32_t)(sinf(a) * r0),
            ui_cx() + (int32_t)(cosf(a) * r1), ui_cy() + (int32_t)(sinf(a) * r1), color, width);
}

static void draw_orbit_points(lv_layer_t *layer, int32_t count, int32_t radius, uint32_t color, int32_t dot_r) {
  for (int32_t i = 0; i < count; ++i) {
    const float a = ((float)i / (float)count) * 6.28318530718f - 1.57079632679f;
    draw_circle(layer, ui_cx() + (int32_t)(cosf(a) * radius), ui_cy() + (int32_t)(sinf(a) * radius), dot_r,
                color, true, 0);
  }
}

static void draw_staff(lv_layer_t *layer) {
  for (int32_t i = 0; i < 5; ++i) {
    draw_line(layer, 96, 172 + i * 18, 370, 172 + i * 18, 0x294052, 2);
  }
}

static void draw_device_face_event(lv_event_t *event) {
  lv_layer_t *layer = lv_event_get_layer(event);
  const float spin = (float)(s_elapsed_ms % 60000u) / 60000.0f;
  const int32_t cx = ui_cx();
  const int32_t cy = ui_cy();

  draw_circle(layer, cx, cy, 223, 0x1a2430, false, 2);

  switch (s_face) {
  case ASTROLABE_UI_FACE_APOCALYPSO:
    for (int32_t r = 52; r <= 180; r += 42) draw_circle(layer, cx, cy, r, 0x28443d, false, 2);
    for (int32_t i = 0; i < 8; ++i) draw_radial_line(layer, i * 45.0f, 38, 190, 0x27423a, 1);
    draw_radial_line(layer, 18.0f + spin * 45.0f, 0, 176, 0xf8fbff, 3);
    draw_arc(layer, 190, 312, 354, 0xff6b6b, 9);
    break;
  case ASTROLABE_UI_FACE_SPOTIFY:
    draw_circle(layer, cx, cy - 14, 158, 0x08090b, true, 0);
    for (int32_t r = 42; r < 154; r += 18) draw_circle(layer, cx, cy - 14, r, 0x203327, false, 2);
    draw_circle(layer, cx, cy - 14, 58, 0x1ed760, true, 0);
    draw_circle(layer, cx, cy - 14, 9, 0xf5f5ee, true, 0);
    draw_arc(layer, 199, 205, 336, 0x1ed760, 4);
    break;
  case ASTROLABE_UI_FACE_ASTROLOGY:
  case ASTROLABE_UI_FACE_SYNASTRY:
    draw_circle(layer, cx, cy, 188, 0x4e3b70, false, 2);
    draw_circle(layer, cx, cy, s_face == ASTROLABE_UI_FACE_SYNASTRY ? 126 : 144, 0x2a2748, false, 2);
    draw_circle(layer, cx, cy, 78, 0x2a2748, false, 2);
    for (int32_t i = 0; i < 12; ++i) draw_radial_line(layer, i * 30.0f, 78, 188, 0x3b3158, 1);
    draw_orbit_points(layer, 10, s_face == ASTROLABE_UI_FACE_SYNASTRY ? 126 : 144, 0xb891ff, 4);
    if (s_face == ASTROLABE_UI_FACE_SYNASTRY) draw_orbit_points(layer, 8, 88, 0xff99cc, 4);
    break;
  case ASTROLABE_UI_FACE_MOON:
    draw_circle(layer, cx, cy, 156, 0x1d2531, true, 0);
    draw_circle(layer, cx - 26, cy - 8, 132, 0xe5e1d2, true, 0);
    draw_circle(layer, cx + 34, cy - 8, 132, 0x1d2531, true, 0);
    draw_circle(layer, cx, cy, 158, 0x596574, false, 3);
    draw_orbit_points(layer, 8, 188, 0xd7e8ee, 3);
    break;
  case ASTROLABE_UI_FACE_CALCIFER_COUNTDOWN:
    for (int32_t i = 0; i < 12; ++i) draw_arc(layer, 194, i * 30 + 3, i * 30 + 24, i % 3 == 0 ? 0xff8a3d : 0x315068, 8);
    draw_arc(layer, 166, 20, 95, 0xffcf66, 16);
    draw_arc(layer, 146, 135, 178, 0x7fcde0, 14);
    draw_circle(layer, cx, cy, 96, 0x15191e, true, 0);
    break;
  case ASTROLABE_UI_FACE_CASTALIA:
    draw_rect(layer, 151, 132, 315, 296, 0xf8fbff);
    for (int32_t y = 0; y < 7; ++y) {
      for (int32_t x = 0; x < 7; ++x) {
        if (((x * 3 + y * 5) % 4) != 0) draw_rect(layer, 166 + x * 20, 147 + y * 20, 177 + x * 20, 158 + y * 20, 0x081018);
      }
    }
    break;
  case ASTROLABE_UI_FACE_SETTINGS:
    draw_circle(layer, cx, cy, 92, 0x16202a, true, 0);
    draw_arc(layer, 124, 35, 325, 0x6cc7ff, 8);
    draw_arc(layer, 158, 65, 295, 0x92a4b8, 5);
    draw_circle(layer, cx, cy, 26, 0x92a4b8, false, 5);
    break;
  case ASTROLABE_UI_FACE_SPECTRUM:
    for (int32_t i = 0; i < 48; ++i) {
      int32_t r0 = 76 + (i % 7) * 5;
      int32_t r1 = 118 + ((i * 17 + (int32_t)(spin * 100)) % 72);
      draw_radial_line(layer, i * 7.5f, r0, r1, i % 2 ? 0x00d4ff : 0xff5bbd, 3);
    }
    draw_circle(layer, cx, cy, 62, 0x0a1018, true, 0);
    break;
  case ASTROLABE_UI_FACE_CHAKRA:
    for (int32_t i = 0; i < 7; ++i) {
      static const uint32_t cols[7] = {0xff3f3f, 0xff8a3d, 0xffcf66, 0x74e06d, 0x63b3ff, 0x8b7cff, 0xff5bbd};
      draw_arc(layer, 176, i * 51 + 3, i * 51 + 45, cols[i], 18);
    }
    draw_circle(layer, cx, cy, 96, 0x4c172c, true, 0);
    draw_circle(layer, cx, cy, 58, 0xff5bbd, false, 6);
    break;
  case ASTROLABE_UI_FACE_TIBETAN_BOWL:
    draw_circle(layer, cx, cy + 8, 178, 0x4c4027, true, 0);
    draw_circle(layer, cx, cy, 166, 0xd6b56d, true, 0);
    draw_circle(layer, cx, cy, 130, 0x604f2f, true, 0);
    draw_circle(layer, cx, cy, 86, 0x282016, true, 0);
    draw_circle(layer, cx + 132, cy - 56, 9, 0xfff0b8, true, 0);
    break;
  case ASTROLABE_UI_FACE_ROCKET:
    draw_arc(layer, 182, 0, (int32_t)(spin * 360.0f), 0xff734d, 12);
    draw_circle(layer, cx, cy, 126, 0x142033, true, 0);
    draw_line(layer, cx, cy - 72, cx, cy + 48, 0xf8fbff, 6);
    draw_line(layer, cx, cy - 72, cx - 28, cy - 20, 0xff734d, 4);
    draw_line(layer, cx, cy - 72, cx + 28, cy - 20, 0xff734d, 4);
    draw_circle(layer, cx, cy + 62, 18, 0xffcf66, true, 0);
    break;
  case ASTROLABE_UI_FACE_RADAR:
    for (int32_t r = 42; r <= 178; r += 34) draw_circle(layer, cx, cy, r, 0x1e4a2a, false, 2);
    draw_radial_line(layer, spin * 360.0f, 0, 184, 0x6dff91, 3);
    draw_circle(layer, cx + 74, cy - 46, 7, 0x6dff91, true, 0);
    draw_circle(layer, cx - 98, cy + 68, 5, 0x56d364, true, 0);
    break;
  case ASTROLABE_UI_FACE_FACULTY:
    draw_circle(layer, cx, cy - 14, 118, 0x32281f, true, 0);
    draw_circle(layer, cx, cy - 42, 46, 0xffe08a, true, 0);
    draw_rect(layer, cx - 54, cy + 8, cx + 54, cy + 102, 0x6b5a36);
    draw_circle(layer, cx - 16, cy - 46, 5, 0x19140c, true, 0);
    draw_circle(layer, cx + 16, cy - 46, 5, 0x19140c, true, 0);
    break;
  case ASTROLABE_UI_FACE_WEATHER:
    for (int32_t i = 0; i < 24; ++i) draw_arc(layer, 186, i * 15 + 1, i * 15 + 10, i < 12 ? 0x63b3ff : 0xffa64d, 8);
    draw_arc(layer, 160, 20, 314, 0x294c66, 8);
    draw_circle(layer, cx, cy, 92, 0x101824, true, 0);
    draw_circle(layer, cx - 22, cy - 20, 26, 0xd9e8f6, true, 0);
    draw_circle(layer, cx + 8, cy - 20, 30, 0xd9e8f6, true, 0);
    draw_circle(layer, cx + 34, cy - 14, 22, 0xd9e8f6, true, 0);
    break;
  case ASTROLABE_UI_FACE_QUOTES:
    draw_rect(layer, 0, 316, ASTROLABE_UI_WIDTH, ASTROLABE_UI_HEIGHT, 0x16132a);
    draw_circle(layer, cx, 170, 72, 0xd9c7ff, false, 3);
    draw_line(layer, 118, 242, 348, 242, 0xd9c7ff, 2);
    break;
  case ASTROLABE_UI_FACE_LIVE_TRANSITS:
    draw_arc(layer, 192, 0, (int32_t)(spin * 360.0f), 0xa4e3ff, 8);
    draw_circle(layer, cx - 72, cy - 10, 62, 0x11192a, true, 0);
    draw_circle(layer, cx + 72, cy - 10, 62, 0x11192a, true, 0);
    draw_orbit_points(layer, 9, 72, 0xa4e3ff, 4);
    break;
  case ASTROLABE_UI_FACE_TAROT:
    draw_rect(layer, cx - 92, 88, cx + 92, 314, 0x231a22);
    draw_circle(layer, cx, 198, 70, 0xd99a5f, false, 3);
    draw_line(layer, cx, 124, cx - 58, 272, 0xd99a5f, 3);
    draw_line(layer, cx, 124, cx + 58, 272, 0xd99a5f, 3);
    break;
  case ASTROLABE_UI_FACE_NOTES:
    draw_circle(layer, cx, cy - 14, 128, 0x2a383e, false, 3);
    draw_circle(layer, cx, cy - 14, 62, 0x121c22, true, 0);
    draw_rect(layer, cx - 18, cy - 48, cx + 18, cy + 24, 0xb4c6d8);
    for (int32_t i = 0; i < 5; ++i) draw_rect(layer, 118 + i * 48, 254 - i * 8, 142 + i * 48, 254 + i * 8, 0x557080);
    break;
  case ASTROLABE_UI_FACE_OCARINA:
    draw_circle(layer, cx, cy, 142, 0x6b3e25, true, 0);
    for (int32_t i = 0; i < 6; ++i) draw_circle(layer, 154 + (i % 3) * 42, 188 + (i / 3) * 62, 18, 0x1a0e08, true, 0);
    draw_circle(layer, 300, 274, 24, 0x1a0e08, true, 0);
    break;
  case ASTROLABE_UI_FACE_BONGO:
    draw_circle(layer, cx, cy, 154, 0xd8865b, true, 0);
    draw_circle(layer, cx, cy, 126, 0xe6c7a4, true, 0);
    draw_circle(layer, cx, cy, 58, 0xb98d66, false, 3);
    draw_orbit_points(layer, 10, 166, 0x633b24, 6);
    break;
  case ASTROLABE_UI_FACE_PIANO:
    for (int32_t i = 0; i < 12; ++i) draw_arc(layer, 184, i * 30 + 2, i * 30 + 26, i % 2 ? 0x11161d : 0xf4f7fb, 42);
    for (int32_t i = 0; i < 7; ++i) draw_arc(layer, 138, i * 51 + 3, i * 51 + 38, 0x0a0e13, 34);
    draw_circle(layer, cx, cy, 46, 0x2c3640, true, 0);
    break;
  case ASTROLABE_UI_FACE_LEVEL:
    draw_circle(layer, cx, cy, 160, 0x233223, false, 3);
    draw_circle(layer, cx, cy, 128, 0x1f2c20, false, 2);
    draw_line(layer, cx - 160, cy, cx + 160, cy, 0x334734, 2);
    draw_line(layer, cx, cy - 160, cx, cy + 160, 0x334734, 2);
    draw_circle(layer, cx + 42, cy - 24, 26, 0x9dff7f, true, 0);
    break;
  case ASTROLABE_UI_FACE_TUNING:
    draw_staff(layer);
    draw_circle(layer, cx + 42, cy - 38, 12, 0xffa64d, true, 0);
    draw_line(layer, cx + 54, cy - 38, cx + 54, cy - 96, 0xffa64d, 3);
    draw_line(layer, 120, 312, 346, 312, 0x405066, 3);
    draw_line(layer, cx, 302, cx, 322, 0x9dff7f, 4);
    break;
  case ASTROLABE_UI_FACE_PAN_DRUM:
    draw_circle(layer, cx, cy, 166, 0x6d8492, true, 0);
    draw_circle(layer, cx, cy, 134, 0x526978, true, 0);
    draw_circle(layer, cx, cy, 34, 0x26333b, true, 0);
    draw_orbit_points(layer, 8, 98, 0xb8e0ff, 24);
    break;
  case ASTROLABE_UI_FACE_ALETHIOMETER:
    draw_circle(layer, cx, cy, 198, 0xf0c36a, false, 3);
    draw_circle(layer, cx, cy, 150, 0x4a3820, false, 2);
    for (int32_t i = 0; i < 36; ++i) draw_radial_line(layer, i * 10.0f, i % 3 == 0 ? 176 : 186, 198, 0xf0c36a, 1);
    draw_radial_line(layer, 28, 0, 138, 0xf8fbff, 3);
    draw_radial_line(layer, 146, 0, 112, 0xff99cc, 3);
    draw_radial_line(layer, 252, 0, 126, 0x6cc7ff, 3);
    draw_circle(layer, cx, cy, 18, 0xf0c36a, true, 0);
    break;
  case ASTROLABE_UI_FACE_RUNES:
    for (int32_t i = 0; i < 3; ++i) {
      int32_t x = 132 + i * 100;
      draw_circle(layer, x, cy, 54, 0x7b5634, true, 0);
      draw_line(layer, x - 14, cy - 28, x + 14, cy + 28, 0xc1d6a4, 4);
      draw_line(layer, x - 2, cy - 2, x + 24, cy - 26, 0xc1d6a4, 4);
    }
    break;
  default:
    draw_arc(layer, 188, 0, (int32_t)(spin * 360.0f), k_faces[s_face].accent, 8);
    draw_circle(layer, cx, cy, 94, 0x111820, true, 0);
    break;
  }
}

static void create_digital_face(void) {
  clear_face();
  style_screen();
  draw_round_mask();

  s_time_label = make_label("--:--:--", 190, &lv_font_montserrat_48, 0xf8fbff);

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

static void create_device_face(astrolabe_ui_face_t face) {
  clear_face();
  style_screen();

  const face_meta_t *meta = &k_faces[face];

  s_dial = lv_obj_create(s_root);
  lv_obj_remove_style_all(s_dial);
  lv_obj_set_size(s_dial, ASTROLABE_UI_WIDTH, ASTROLABE_UI_HEIGHT);
  lv_obj_add_event_cb(s_dial, draw_device_face_event, LV_EVENT_DRAW_MAIN, NULL);

  switch (face) {
  case ASTROLABE_UI_FACE_WEATHER:
    s_face_label = make_label(meta->line1, 206, &lv_font_montserrat_48, 0xf8fbff);
    s_hint_label = make_label("partly cloudy", 250, &lv_font_montserrat_16, 0xb0c0da);
    make_label(meta->line2, 380, &lv_font_montserrat_16, 0x6f7d8a);
    break;
  case ASTROLABE_UI_FACE_TUNING:
    s_face_label = make_label(meta->line1, 44, &lv_font_montserrat_22, meta->accent);
    s_hint_label = make_label(meta->line2, 90, &lv_font_montserrat_24, 0xe8f8ff);
    make_label("A4  440.0 Hz", 132, &lv_font_montserrat_16, 0x92a4b8);
    break;
  case ASTROLABE_UI_FACE_TAROT:
    make_label("0", 38, &lv_font_montserrat_24, meta->accent);
    s_face_label = make_label("THE FOOL", 340, &lv_font_montserrat_22, 0xf5e3c0);
    s_hint_label = make_label(meta->line2, 408, &lv_font_montserrat_16, 0xa98d76);
    break;
  case ASTROLABE_UI_FACE_QUOTES:
    s_face_label = make_label("\"Know thy measure.\"", 104, &lv_font_montserrat_18, 0xf2eaff);
    s_hint_label = make_label(meta->line1, 348, &lv_font_montserrat_18, meta->accent);
    make_label(meta->line2, 390, &lv_font_montserrat_16, 0x8e7ca8);
    break;
  case ASTROLABE_UI_FACE_LEVEL:
    s_face_label = make_label(meta->line1, 46, &lv_font_montserrat_22, meta->accent);
    s_hint_label = make_label(meta->line2, 79, &lv_font_montserrat_16, 0x8e9ba8);
    make_label("tilt left", 354, &lv_font_montserrat_16, 0xf8fbff);
    break;
  case ASTROLABE_UI_FACE_BONGO:
  case ASTROLABE_UI_FACE_OCARINA:
  case ASTROLABE_UI_FACE_PAN_DRUM:
  case ASTROLABE_UI_FACE_PIANO:
    s_face_label = make_label(meta->line1, face == ASTROLABE_UI_FACE_PIANO ? 34 : 52, &lv_font_montserrat_22, 0xf0f6f0);
    s_hint_label = make_label(meta->line2, face == ASTROLABE_UI_FACE_PIANO ? 418 : 392, &lv_font_montserrat_16, meta->accent);
    break;
  case ASTROLABE_UI_FACE_ALETHIOMETER:
  case ASTROLABE_UI_FACE_RUNES:
  case ASTROLABE_UI_FACE_NOTES:
  case ASTROLABE_UI_FACE_LIVE_TRANSITS:
    s_face_label = make_label(meta->line1, 30, &lv_font_montserrat_22, meta->accent);
    s_hint_label = make_label(meta->line2, 56, &lv_font_montserrat_16, 0x8e9ba8);
    break;
  default:
    s_face_label = make_label(meta->line1[0] ? meta->line1 : meta->name, 42, &lv_font_montserrat_22, meta->accent);
    s_hint_label = make_label(meta->line2, 394, &lv_font_montserrat_16, 0x8e9ba8);
    break;
  }
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
  if (s_face == ASTROLABE_UI_FACE_DIGITAL_LOCAL && s_dial) {
    lv_arc_set_value(s_dial, (int32_t)(((s_elapsed_ms % 60000u) * 1000u) / 60000u));
  }
}

void astrolabe_ui_init(void) {
  s_root = lv_screen_active();
  s_elapsed_ms = 12u * 3600u * 1000u;
  astrolabe_ui_set_face(ASTROLABE_UI_FACE_CLASSIC_ANALOG);
}

void astrolabe_ui_set_face(astrolabe_ui_face_t face) {
  if (!s_root) {
    return;
  }
  if (face < 0 || face >= ASTROLABE_UI_FACE_COUNT) {
    face = ASTROLABE_UI_FACE_CLASSIC_ANALOG;
  }
  s_face = face;
  switch (s_face) {
  case ASTROLABE_UI_FACE_CLASSIC_ANALOG:
    create_classic_face();
    break;
  case ASTROLABE_UI_FACE_DIGITAL_LOCAL:
    create_digital_face();
    break;
  default:
    create_device_face(s_face);
    break;
  }
  refresh_time_labels();
}

astrolabe_ui_face_t astrolabe_ui_current_face(void) { return s_face; }

const char *astrolabe_ui_face_name(astrolabe_ui_face_t face) {
  if (face < 0 || face >= ASTROLABE_UI_FACE_COUNT) {
    return "";
  }
  return k_faces[face].name;
}

const char *astrolabe_ui_face_summary(astrolabe_ui_face_t face) {
  if (face < 0 || face >= ASTROLABE_UI_FACE_COUNT) {
    return "";
  }
  return k_faces[face].line2;
}

void astrolabe_ui_tick(uint32_t elapsed_ms) {
  s_elapsed_ms += elapsed_ms;
  refresh_time_labels();
  if (s_dial) {
    lv_obj_invalidate(s_dial);
  }
}
