#include "p4_real_ui.h"

#include <cstdio>
#include <cstring>

#include "faces/pm_faces.h"
#include "faces/quotes/pm_face_quotes.h"
#include "pin_config.h"
#include "pm_display.h"
#include "pm_settings.h"

static PmDisplayCanvas *s_canvas = nullptr;
static lv_obj_t *s_image = nullptr;
static lv_image_dsc_t s_img_dsc = {};

static const char *const k_face_names[] = {
    "ClassicAnalog", "Apocalypso", "DigitalLocal", "Spotify", "Astrology", "Moon", "CalciferCountdown",
    "Castalia", "Settings", "Synastry", "Spectrum", "Chakra", "TibetanBowl", "Rocket", "Radar",
    "Faculty", "Weather", "Globe", "Sky", "Quotes", "LiveTransits", "Tarot", "Notes", "Ocarina",
    "Bongo", "Piano", "Level", "Tuning", "PanDrum", "Alethiometer", "Runes", "Orientation", "Luopan",
    "QuestionOfDay", "FocusTimer", "Biometrics", "Lenormand", "Pythia", "Geomancy", "EnochianAngel",
};

static int normalize_face(int face) {
  const int count = astrolabe_real_ui_face_count();
  while (face < 0) {
    face += count;
  }
  return face % count;
}

void astrolabe_real_ui_init_in(lv_obj_t *parent) {
  if (s_canvas != nullptr) {
    return;
  }

  s_canvas = new PmDisplayCanvas(ASTROLABE_REAL_UI_WIDTH, ASTROLABE_REAL_UI_HEIGHT, nullptr);
  s_canvas->begin(GFX_SKIP_OUTPUT_BEGIN);
  pm_display_bind(s_canvas);

  g_quotes_ui.ok = true;
  g_quotes_ui.demo = true;
  std::snprintf(g_quotes_ui.faculty_slug, sizeof(g_quotes_ui.faculty_slug), "a.plato");
  std::snprintf(g_quotes_ui.faculty_name, sizeof(g_quotes_ui.faculty_name), "Plato");
  std::snprintf(g_quotes_ui.quote, sizeof(g_quotes_ui.quote),
                "The beginning is the most important part of the work.");
  std::snprintf(g_quotes_ui.book_title, sizeof(g_quotes_ui.book_title), "The Republic");

  s_img_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
  s_img_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
  s_img_dsc.header.flags = 0;
  s_img_dsc.header.w = ASTROLABE_REAL_UI_WIDTH;
  s_img_dsc.header.h = ASTROLABE_REAL_UI_HEIGHT;
  s_img_dsc.header.stride = ASTROLABE_REAL_UI_WIDTH * sizeof(uint16_t);
  s_img_dsc.header.reserved_2 = 0;
  s_img_dsc.data_size = ASTROLABE_REAL_UI_WIDTH * ASTROLABE_REAL_UI_HEIGHT * sizeof(uint16_t);
  s_img_dsc.data = reinterpret_cast<const uint8_t *>(s_canvas->getFramebuffer());
  s_img_dsc.reserved = nullptr;
  s_img_dsc.reserved_2 = nullptr;

  s_image = lv_image_create(parent);
  lv_obj_remove_style_all(s_image);
  lv_obj_set_size(s_image, ASTROLABE_REAL_UI_WIDTH, ASTROLABE_REAL_UI_HEIGHT);
  lv_obj_set_pos(s_image, 0, 0);
  lv_image_set_src(s_image, &s_img_dsc);
  astrolabe_real_ui_set_face(ASTROLABE_REAL_UI_FACE_MOON);
}

void astrolabe_real_ui_tick(uint32_t elapsed_ms) {
  (void)elapsed_ms;
  if (s_canvas == nullptr || s_image == nullptr) {
    return;
  }
  pm_faces_draw(-1.f);
  lv_obj_invalidate(s_image);
}

void astrolabe_real_ui_set_face(int face) {
  if (s_canvas == nullptr) {
    return;
  }
  const ClockFace target = static_cast<ClockFace>(normalize_face(face));
  if (target == ClockFace::Settings) {
    pm_settings_set_page(SettingsPage::WiFi);
  }
  pm_faces_set(target);
  astrolabe_real_ui_tick(0);
}

void astrolabe_real_ui_cycle(int delta) {
  pm_faces_cycle(delta);
  astrolabe_real_ui_tick(0);
}

int astrolabe_real_ui_current_face(void) {
  if (pm_faces_castalia_active()) {
    return static_cast<int>(ClockFace::Castalia);
  }
  return static_cast<int>(pm_faces_current());
}

int astrolabe_real_ui_face_count(void) { return static_cast<int>(ClockFace::kNumFaces); }

const char *astrolabe_real_ui_face_name(int face) {
  face = normalize_face(face);
  if (face >= 0 && face < static_cast<int>(sizeof(k_face_names) / sizeof(k_face_names[0]))) {
    return k_face_names[face];
  }
  return "Unknown";
}

int astrolabe_real_ui_parse_face_name(const char *name) {
  if (name == nullptr) {
    return -1;
  }
  for (int i = 0; i < astrolabe_real_ui_face_count(); ++i) {
    if (strcasecmp(name, astrolabe_real_ui_face_name(i)) == 0) {
      return i;
    }
  }
  if (strcasecmp(name, "moon") == 0 || strcasecmp(name, "lunasay") == 0) {
    return ASTROLABE_REAL_UI_FACE_MOON;
  }
  if (strcasecmp(name, "classic") == 0 || strcasecmp(name, "astrolabe") == 0) {
    return static_cast<int>(ClockFace::ClassicAnalog);
  }
  return -1;
}

int astrolabe_real_ui_width(void) { return ASTROLABE_REAL_UI_WIDTH; }

int astrolabe_real_ui_height(void) { return ASTROLABE_REAL_UI_HEIGHT; }

const uint16_t *astrolabe_real_ui_framebuffer(void) {
  return s_canvas ? s_canvas->getFramebuffer() : nullptr;
}
