#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
  ASTROLABE_UI_WIDTH = 466,
  ASTROLABE_UI_HEIGHT = 466,
};

typedef enum {
  ASTROLABE_UI_FACE_DIGITAL = 0,
  ASTROLABE_UI_FACE_CLASSIC = 1,
} astrolabe_ui_face_t;

void astrolabe_ui_init(void);
void astrolabe_ui_set_face(astrolabe_ui_face_t face);
astrolabe_ui_face_t astrolabe_ui_current_face(void);
void astrolabe_ui_tick(uint32_t elapsed_ms);

#ifdef __cplusplus
}
#endif
