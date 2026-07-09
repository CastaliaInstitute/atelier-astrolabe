#include "faculty175_face_native.h"

void faculty175_face_orient_draw(uint32_t anim_ms)
{
    static const faculty175_native_face_t face = {
        .id = FACULTY175_FACE_ORIENT,
        .title = "Orientation",
        .subtitle = "6DOF DIAL",
        .style = FACULTY175_NATIVE_RADAR,
        .hue = 1,
        .a = "RELATIVE HEADING",
        .b = "TILT / ROLL",
        .c = "",
    };
    faculty175_face_native_draw(&face, anim_ms);
}
