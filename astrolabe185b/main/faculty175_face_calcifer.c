#include "faculty175_face_native.h"

void faculty175_face_calcifer_draw(uint32_t anim_ms)
{
    static const faculty175_native_face_t face = {
        .id = FACULTY175_FACE_CALCIFER,
        .title = "Calcifer",
        .subtitle = "DAYWHEEL",
        .style = FACULTY175_NATIVE_ANALOG,
        .hue = 0,
        .a = "HEARTH COUNTDOWN",
        .b = "CIRCADIAN ROLL",
        .c = "",
    };
    faculty175_face_native_draw(&face, anim_ms);
}
