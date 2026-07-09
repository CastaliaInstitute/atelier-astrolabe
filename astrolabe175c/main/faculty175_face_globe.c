#include "faculty175_face_native.h"

void faculty175_face_globe_draw(uint32_t anim_ms)
{
    static const faculty175_native_face_t face = {
        .id = FACULTY175_FACE_GLOBE,
        .title = "Globe",
        .subtitle = "DAY / NIGHT",
        .style = FACULTY175_NATIVE_CELESTIAL,
        .hue = 2,
        .a = "EARTH DISK",
        .b = "TERMINATOR",
        .c = "",
    };
    faculty175_face_native_draw(&face, anim_ms);
}
