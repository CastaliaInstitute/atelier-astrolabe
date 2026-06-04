#include "faculty175_face_native.h"

void faculty175_face_moon_draw(uint32_t anim_ms)
{
    static const faculty175_native_face_t face = {
        .id = FACULTY175_FACE_MOON,
        .title = "Moon",
        .subtitle = "LUNAR ORACLE",
        .style = FACULTY175_NATIVE_CELESTIAL,
        .hue = 4,
        .a = "PHASE / FORTUNE",
        .b = "DAILY MOON",
        .c = "",
    };
    faculty175_face_native_draw(&face, anim_ms);
}
