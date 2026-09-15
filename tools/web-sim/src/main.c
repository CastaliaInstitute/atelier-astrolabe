#include <stdbool.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <emscripten.h>

#include "faculty175_board.h"
#include "faculty175_astro_math.h"
#include "faculty175_face_dispatch.h"
#include "faculty175_face_psych_state.h"
#include "faculty175_faces.h"
#include "faculty175_human_design_math.h"

void faculty175_websim_display_init(void);
void faculty175_websim_display_tick(uint32_t elapsed_ms);

static faculty175_face_id_t s_face = FACULTY175_FACE_POCKETWATCH;
static uint32_t s_anim_ms;
static char s_voice_state[16];
static char s_voice_detail[96];
static uint32_t s_voice_until_ms;

static faculty175_face_id_t websim_face_id_from_index(int index)
{
    if (index < 0) {
        return FACULTY175_FACE_POCKETWATCH;
    }
    size_t offset = (size_t)index;
    for (faculty175_face_id_t id = 0; id < FACULTY175_FACE_COUNT; ++id) {
        if (faculty175_faces_get(id) == NULL) {
            continue;
        }
        if (offset == 0) {
            return id;
        }
        --offset;
    }
    return FACULTY175_FACE_POCKETWATCH;
}

static int websim_face_index_of_id(faculty175_face_id_t face_id)
{
    if (faculty175_faces_get(face_id) == NULL) {
        return -1;
    }
    int index = 0;
    for (faculty175_face_id_t id = 0; id < FACULTY175_FACE_COUNT; ++id) {
        if (faculty175_faces_get(id) == NULL) {
            continue;
        }
        if (id == face_id) {
            return index;
        }
        ++index;
    }
    return -1;
}

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
    int count = 0;
    for (faculty175_face_id_t id = 0; id < FACULTY175_FACE_COUNT; ++id) {
        if (faculty175_faces_get(id) != NULL) {
            ++count;
        }
    }
    return count;
}

EMSCRIPTEN_KEEPALIVE const char *astrolabe_web_face_name(int face_id)
{
    const faculty175_face_desc_t *face = faculty175_faces_get(websim_face_id_from_index(face_id));
    return face != NULL ? face->label : "";
}

EMSCRIPTEN_KEEPALIVE const char *astrolabe_web_face_slug(int face_id)
{
    const faculty175_face_desc_t *face = faculty175_faces_get(websim_face_id_from_index(face_id));
    return face != NULL ? face->slug : "";
}

EMSCRIPTEN_KEEPALIVE void astrolabe_web_set_face(int face_id)
{
    s_face = websim_face_id_from_index(face_id);
    draw_current_face();
}

EMSCRIPTEN_KEEPALIVE int astrolabe_web_current_face(void)
{
    return websim_face_index_of_id(s_face);
}

EMSCRIPTEN_KEEPALIVE int astrolabe_web_tap(int x, int y)
{
    if (s_face != FACULTY175_FACE_PSYCH_STATE ||
        !faculty175_face_psych_state_tap((int16_t)x, (int16_t)y)) {
        return 0;
    }
    draw_current_face();
    return 1;
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

EMSCRIPTEN_KEEPALIVE const char *astrolabe_web_astro_positions(double epoch_seconds)
{
    static char json[512];
    faculty175_chart_positions_t positions = {0};
    if (epoch_seconds <= 0.0 ||
        !faculty175_astro_positions_at_epoch((time_t)epoch_seconds, &positions)) {
        snprintf(json, sizeof(json), "{\"ok\":false,\"error\":\"invalid UTC epoch\"}");
        return json;
    }
    snprintf(json,
             sizeof(json),
             "{\"ok\":true,\"epoch\":%.0f,\"sun\":%.6f,\"moon\":%.6f,"
             "\"mercury\":%.6f,\"venus\":%.6f,\"mars\":%.6f,"
             "\"jupiter\":%.6f,\"saturn\":%.6f}",
             epoch_seconds,
             positions.lon[0],
             positions.lon[1],
             positions.lon[2],
             positions.lon[3],
             positions.lon[4],
             positions.lon[5],
             positions.lon[6]);
    return json;
}

EMSCRIPTEN_KEEPALIVE const char *astrolabe_web_human_design_reading(double birth_epoch_seconds)
{
    static char json[640];
    const time_t birth_epoch = (time_t)birth_epoch_seconds;
    time_t design_epoch = 0;
    faculty175_chart_positions_t personality = {0};
    faculty175_chart_positions_t design = {0};
    if (birth_epoch <= 0 ||
        !faculty175_astro_positions_at_epoch(birth_epoch, &personality) ||
        !faculty175_human_design_design_epoch(birth_epoch, &design_epoch) ||
        !faculty175_astro_positions_at_epoch(design_epoch, &design)) {
        snprintf(json, sizeof(json), "{\"ok\":false,\"error\":\"unable to calculate Human Design reading\"}");
        return json;
    }
    uint8_t p_sun_gate = 0, p_sun_line = 0, p_moon_gate = 0, p_moon_line = 0;
    uint8_t d_sun_gate = 0, d_sun_line = 0, d_moon_gate = 0, d_moon_line = 0;
    (void)faculty175_human_design_gate_line(personality.lon[0], &p_sun_gate, &p_sun_line);
    (void)faculty175_human_design_gate_line(personality.lon[1], &p_moon_gate, &p_moon_line);
    (void)faculty175_human_design_gate_line(design.lon[0], &d_sun_gate, &d_sun_line);
    (void)faculty175_human_design_gate_line(design.lon[1], &d_moon_gate, &d_moon_line);
    snprintf(json,
             sizeof(json),
             "{\"ok\":true,\"birthEpoch\":%lld,\"designEpoch\":%lld,"
             "\"personality\":{\"sun\":{\"longitude\":%.6f,\"gate\":%u,\"line\":%u},"
             "\"earth\":{\"longitude\":%.6f},"
             "\"moon\":{\"longitude\":%.6f,\"gate\":%u,\"line\":%u}},"
             "\"design\":{\"sun\":{\"longitude\":%.6f,\"gate\":%u,\"line\":%u},"
             "\"earth\":{\"longitude\":%.6f},"
             "\"moon\":{\"longitude\":%.6f,\"gate\":%u,\"line\":%u}}}",
             (long long)birth_epoch,
             (long long)design_epoch,
             personality.lon[0],
             p_sun_gate,
             p_sun_line,
             fmod(personality.lon[0] + 180.0, 360.0),
             personality.lon[1],
             p_moon_gate,
             p_moon_line,
             design.lon[0],
             d_sun_gate,
             d_sun_line,
             fmod(design.lon[0] + 180.0, 360.0),
             design.lon[1],
             d_moon_gate,
             d_moon_line);
    return json;
}

int main(void)
{
    faculty175_websim_display_init();
    draw_current_face();
    emscripten_set_main_loop(frame, 0, true);
    return 0;
}
