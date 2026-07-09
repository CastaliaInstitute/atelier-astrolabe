#include "faculty175_face_native.h"

void faculty175_face_classic_draw(uint32_t anim_ms)
{
    static const faculty175_native_face_t face = {
        .id = FACULTY175_FACE_CLASSIC,
        .title = "Classic Analog",
        .subtitle = "12 HOUR DIAL",
        .style = FACULTY175_NATIVE_ANALOG,
        .hue = 0,
        .a = "LOCAL SOLAR",
        .b = "HANDS FROM BOOT",
        .c = "",
    };
    faculty175_face_native_draw(&face, anim_ms);
}
