#include "faculty175_face_native.h"

void faculty175_face_bongo_draw(uint32_t anim_ms)
{
    static const faculty175_native_face_t face = {
        .id = FACULTY175_FACE_BONGO,
        .title = "Bongo",
        .subtitle = "DRUM HEADS",
        .style = FACULTY175_NATIVE_INSTRUMENT,
        .hue = 0,
        .a = "PITCH BY RADIUS",
        .b = "TOUCH PLAY",
        .c = "",
    };
    faculty175_face_native_draw(&face, anim_ms);
}
