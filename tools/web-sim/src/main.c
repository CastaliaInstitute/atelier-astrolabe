#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include <emscripten.h>
#include <emscripten/html5.h>

#include "astrolabe_ui.h"
#include "lvgl.h"

#define CANVAS_ID "#astrolabe-canvas"

static lv_display_t *s_display;
static lv_indev_t *s_pointer;
static uint8_t *s_draw_buf;
static bool s_pointer_down;
static int32_t s_pointer_x;
static int32_t s_pointer_y;
static double s_last_frame_ms;

EM_JS(void, js_canvas_init, (int width, int height), {
  const canvas = document.querySelector('#astrolabe-canvas');
  canvas.width = width;
  canvas.height = height;
  Module.astrolabeCtx = canvas.getContext('2d', { alpha: false });
  Module.astrolabeImageData = Module.astrolabeCtx.createImageData(width, height);
});

EM_JS(void, js_canvas_flush_argb8888, (int x1, int y1, int x2, int y2, const uint8_t *src, int stride), {
  const image = Module.astrolabeImageData;
  const data = image.data;
  for (let y = y1; y <= y2; y++) {
    const srcRow = src + (y - y1) * stride;
    let dst = (y * image.width + x1) * 4;
    for (let x = x1; x <= x2; x++) {
      const offset = srcRow + (x - x1) * 4;
      const p = HEAPU8[offset + 0] | (HEAPU8[offset + 1] << 8) | (HEAPU8[offset + 2] << 16) |
                (HEAPU8[offset + 3] << 24);
      data[dst++] = (p >> 16) & 0xff;
      data[dst++] = (p >> 8) & 0xff;
      data[dst++] = p & 0xff;
      data[dst++] = 0xff;
    }
  }
  Module.astrolabeCtx.putImageData(image, 0, 0);
});

static EM_BOOL pointer_event(int event_type, const EmscriptenMouseEvent *event, void *user_data) {
  (void)event_type;
  (void)user_data;
  s_pointer_x = event->targetX;
  s_pointer_y = event->targetY;
  s_pointer_down = event->buttons != 0;
  return EM_TRUE;
}

static EM_BOOL touch_event(int event_type, const EmscriptenTouchEvent *event, void *user_data) {
  (void)user_data;
  if (event->numTouches < 1) {
    s_pointer_down = false;
    return EM_TRUE;
  }
  const EmscriptenTouchPoint *touch = &event->touches[0];
  s_pointer_x = touch->targetX;
  s_pointer_y = touch->targetY;
  s_pointer_down = event_type != EMSCRIPTEN_EVENT_TOUCHEND && event_type != EMSCRIPTEN_EVENT_TOUCHCANCEL;
  return EM_TRUE;
}

static void pointer_read(lv_indev_t *indev, lv_indev_data_t *data) {
  (void)indev;
  data->point.x = s_pointer_x;
  data->point.y = s_pointer_y;
  data->state = s_pointer_down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static void display_flush(lv_display_t *display, const lv_area_t *area, uint8_t *px_map) {
  (void)display;
  const int32_t width = lv_area_get_width(area);
  js_canvas_flush_argb8888(area->x1, area->y1, area->x2, area->y2, px_map, width * 4);
  lv_display_flush_ready(s_display);
}

static void frame(void) {
  const double now = emscripten_get_now();
  uint32_t elapsed = 16;
  if (s_last_frame_ms > 0) {
    elapsed = (uint32_t)(now - s_last_frame_ms);
    if (elapsed == 0) {
      elapsed = 1;
    }
    if (elapsed > 100) {
      elapsed = 100;
    }
  }
  s_last_frame_ms = now;

  lv_tick_inc(elapsed);
  astrolabe_ui_tick(elapsed);
  lv_timer_handler();
}

void astrolabe_web_set_face(int face_id) {
  if (face_id == ASTROLABE_UI_FACE_CLASSIC) {
    astrolabe_ui_set_face(ASTROLABE_UI_FACE_CLASSIC);
  } else {
    astrolabe_ui_set_face(ASTROLABE_UI_FACE_DIGITAL);
  }
}

int main(void) {
  lv_init();
  js_canvas_init(ASTROLABE_UI_WIDTH, ASTROLABE_UI_HEIGHT);

  s_display = lv_display_create(ASTROLABE_UI_WIDTH, ASTROLABE_UI_HEIGHT);
  lv_display_set_color_format(s_display, LV_COLOR_FORMAT_ARGB8888);

  const size_t draw_buf_size = ASTROLABE_UI_WIDTH * 80 * 4;
  s_draw_buf = malloc(draw_buf_size);
  lv_display_set_buffers(s_display, s_draw_buf, NULL, draw_buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(s_display, display_flush);

  s_pointer = lv_indev_create();
  lv_indev_set_type(s_pointer, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(s_pointer, pointer_read);

  emscripten_set_mousedown_callback(CANVAS_ID, NULL, false, pointer_event);
  emscripten_set_mouseup_callback(CANVAS_ID, NULL, false, pointer_event);
  emscripten_set_mousemove_callback(CANVAS_ID, NULL, false, pointer_event);
  emscripten_set_touchstart_callback(CANVAS_ID, NULL, false, touch_event);
  emscripten_set_touchend_callback(CANVAS_ID, NULL, false, touch_event);
  emscripten_set_touchmove_callback(CANVAS_ID, NULL, false, touch_event);
  emscripten_set_touchcancel_callback(CANVAS_ID, NULL, false, touch_event);

  astrolabe_ui_init();
  emscripten_set_main_loop(frame, 0, true);
  return 0;
}
