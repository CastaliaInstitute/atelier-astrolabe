#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <emscripten.h>

#include "faculty175_board.h"
#include "faculty175_face_dispatch.h"
#include "faculty175_faces.h"

void faculty175_websim_display_init(void);
void faculty175_websim_display_tick(uint32_t elapsed_ms);

static faculty175_face_id_t s_face = FACULTY175_FACE_POCKETWATCH;
static uint32_t s_anim_ms;
static char s_voice_state[16];
static char s_voice_detail[96];
static uint32_t s_voice_until_ms;

static void draw_current_face(void)
{
    if (s_voice_state[0] != '\0' && s_anim_ms < s_voice_until_ms) {
        const faculty175_face_desc_t *face = faculty175_faces_get(s_face);
        faculty175_ui_state_t ui = FACULTY175_UI_THINK;
        if (strcmp(s_voice_state, "listen") == 0) {
            ui = FACULTY175_UI_LISTEN;
        } else if (strcmp(s_voice_state, "speak") == 0) {
            ui = FACULTY175_UI_SPEAK;
        }
        faculty175_display_draw_status(ui,
                                       face != NULL ? face->label : "Faculty175",
                                       s_voice_detail[0] != '\0' ? s_voice_detail : s_voice_state,
                                       s_anim_ms,
                                       NULL,
                                       NULL,
                                       0);
        return;
    }
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

EMSCRIPTEN_KEEPALIVE const char *astrolabe_web_face_slug(int face_id)
{
    const faculty175_face_desc_t *face = faculty175_faces_get((faculty175_face_id_t)face_id);
    return face != NULL ? face->slug : "";
}

EMSCRIPTEN_KEEPALIVE void astrolabe_web_set_face(int face_id)
{
    if (face_id < 0 || face_id >= (int)faculty175_faces_count()) {
        face_id = FACULTY175_FACE_POCKETWATCH;
    }
    s_face = (faculty175_face_id_t)face_id;
    draw_current_face();
}

EMSCRIPTEN_KEEPALIVE int astrolabe_web_current_face(void)
{
    return (int)s_face;
}

EMSCRIPTEN_KEEPALIVE void astrolabe_web_voice_state(const char *state, const char *detail, int duration_ms)
{
    snprintf(s_voice_state, sizeof(s_voice_state), "%s", state != NULL ? state : "");
    snprintf(s_voice_detail, sizeof(s_voice_detail), "%s", detail != NULL ? detail : "");
    if (duration_ms < 250) {
        duration_ms = 250;
    }
    s_voice_until_ms = s_anim_ms + (uint32_t)duration_ms;
    draw_current_face();
}

EMSCRIPTEN_KEEPALIVE void astrolabe_web_button_press(void)
{
    const faculty175_face_desc_t *face = faculty175_faces_get(s_face);
    snprintf(s_voice_state, sizeof(s_voice_state), "speak");
    snprintf(s_voice_detail,
             sizeof(s_voice_detail),
             "button TTS: %s",
             face != NULL && face->slug != NULL ? face->slug : "face");
    s_voice_until_ms = s_anim_ms + 4000u;
    draw_current_face();
}

int main(void)
{
    faculty175_websim_display_init();
    draw_current_face();
    emscripten_set_main_loop(frame, 0, true);
    return 0;
}
