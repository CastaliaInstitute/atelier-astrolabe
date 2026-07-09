#include <stdbool.h>
#include <stdint.h>

#include <emscripten.h>

#include "faculty175_board.h"
#include "faculty175_face_dispatch.h"
#include "faculty175_faces.h"

void faculty175_websim_display_init(void);
void faculty175_websim_display_tick(uint32_t elapsed_ms);

static faculty175_face_id_t s_face = FACULTY175_FACE_POCKETWATCH;
static uint32_t s_anim_ms;

static void draw_current_face(void)
{
    if (!faculty175_face_dispatch_draw(s_face, s_anim_ms)) {
        const faculty175_face_desc_t *face = faculty175_faces_get(s_face);
        faculty175_display_draw_status(FACULTY175_UI_LISTEN,
                                       face != NULL ? face->label : "Faculty175",
                                       "not implemented",
                                       s_anim_ms,
                                       NULL,
                                       NULL,
                                       0);
    }
}

static void frame(void)
{
    s_anim_ms += 16;
    faculty175_websim_display_tick(16);
    draw_current_face();
}

EMSCRIPTEN_KEEPALIVE int astrolabe_web_face_count(void)
{
    return (int)faculty175_faces_count();
}

EMSCRIPTEN_KEEPALIVE const char *astrolabe_web_face_name(int face_id)
{
    const faculty175_face_desc_t *face = faculty175_faces_get((faculty175_face_id_t)face_id);
    return face != NULL ? face->label : "";
}

EMSCRIPTEN_KEEPALIVE void astrolabe_web_set_face(int face_id)
{
    if (face_id < 0 || face_id >= (int)faculty175_faces_count()) {
        face_id = FACULTY175_FACE_POCKETWATCH;
    }
    s_face = (faculty175_face_id_t)face_id;
    draw_current_face();
}

int main(void)
{
    faculty175_websim_display_init();
    draw_current_face();
    emscripten_set_main_loop(frame, 0, true);
    return 0;
}
