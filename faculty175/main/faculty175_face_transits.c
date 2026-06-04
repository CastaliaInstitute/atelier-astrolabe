#include "faculty175_face_native.h"

void faculty175_face_transits_draw(uint32_t anim_ms)
{
    static const faculty175_native_face_t face = {
        .id = FACULTY175_FACE_TRANSITS,
        .title = "Live Transits",
        .subtitle = "NOW / NEXT",
        .style = FACULTY175_NATIVE_CELESTIAL,
        .hue = 4,
        .a = "MOON INGRESS",
        .b = "PAIR SPHERES",
        .c = "",
    };
    faculty175_face_native_draw(&face, anim_ms);
}
