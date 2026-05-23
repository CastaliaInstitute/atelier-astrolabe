#include <emscripten.h>

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <ctime>

#include "faces/pm_faces.h"
#include "faces/quotes/pm_face_quotes.h"
#include "pin_config.h"
#include "pm_display.h"

static PmDisplayCanvas *s_canvas;

EM_JS(void, js_canvas_init, (int width, int height), {
  const canvas = document.querySelector('#astrolabe-canvas');
  canvas.width = width;
  canvas.height = height;
  Module.astrolabeCtx = canvas.getContext('2d', { alpha: false });
  Module.astrolabeImageData = Module.astrolabeCtx.createImageData(width, height);
});

EM_JS(void, js_canvas_flush_rgb565, (const uint16_t *src, int width, int height), {
  const image = Module.astrolabeImageData;
  const data = image.data;
  for (let i = 0, j = 0; i < width * height; ++i) {
    const p = HEAPU16[(src >> 1) + i];
    data[j++] = ((p >> 11) & 0x1f) * 255 / 31;
    data[j++] = ((p >> 5) & 0x3f) * 255 / 63;
    data[j++] = (p & 0x1f) * 255 / 31;
    data[j++] = 255;
  }
  Module.astrolabeCtx.putImageData(image, 0, 0);
});

static void frame(void) {
  if (!s_canvas) return;
  pm_faces_draw(-1.f);
  js_canvas_flush_rgb565(s_canvas->getFramebuffer(), LCD_WIDTH, LCD_HEIGHT);
}

extern "C" EMSCRIPTEN_KEEPALIVE void astrolabe_web_set_face(int face_id) {
  if (face_id < 0 || face_id >= static_cast<int>(ClockFace::kNumFaces)) face_id = 0;
  pm_faces_set(static_cast<ClockFace>(face_id));
  frame();
}

int main() {
  js_canvas_init(LCD_WIDTH, LCD_HEIGHT);
  s_canvas = new PmDisplayCanvas(LCD_WIDTH, LCD_HEIGHT, nullptr);
  s_canvas->begin(GFX_SKIP_OUTPUT_BEGIN);
  pm_display_bind(s_canvas);
  g_quotes_ui.ok = true;
  g_quotes_ui.demo = true;
  std::snprintf(g_quotes_ui.faculty_slug, sizeof(g_quotes_ui.faculty_slug), "a.plato");
  std::snprintf(g_quotes_ui.faculty_name, sizeof(g_quotes_ui.faculty_name), "Plato");
  std::snprintf(g_quotes_ui.quote, sizeof(g_quotes_ui.quote),
                "The beginning is the most important part of the work.");
  std::snprintf(g_quotes_ui.book_title, sizeof(g_quotes_ui.book_title), "The Republic");
  pm_faces_set(ClockFace::ClassicAnalog);
  emscripten_set_main_loop(frame, 0, true);
  return 0;
}
