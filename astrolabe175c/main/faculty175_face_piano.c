#include "faculty175_face_native.h"

void faculty175_face_piano_draw(uint32_t anim_ms)
{
    static const faculty175_native_face_t face = {
        .id = FACULTY175_FACE_PIANO,
        .title = "Piano",
        .subtitle = "ONE OCTAVE",
        .style = FACULTY175_NATIVE_INSTRUMENT,
        .hue = 5,
        .a = "WHITE / BLACK KEYS",
        .b = "TOUCH PLAY",
        .c = "",
    };
    faculty175_face_native_draw(&face, anim_ms);
}
