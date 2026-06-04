#include "faculty175_face_native.h"

void faculty175_face_ocarina_draw(uint32_t anim_ms)
{
    static const faculty175_native_face_t face = {
        .id = FACULTY175_FACE_OCARINA,
        .title = "Ocarina",
        .subtitle = "BREATH KEYS",
        .style = FACULTY175_NATIVE_INSTRUMENT,
        .hue = 3,
        .a = "HOLES / SCALE",
        .b = "TOUCH PLAY",
        .c = "",
    };
    faculty175_face_native_draw(&face, anim_ms);
}
