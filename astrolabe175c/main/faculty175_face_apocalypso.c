#include "faculty175_face_native.h"

void faculty175_face_apocalypso_draw(uint32_t anim_ms)
{
    static const faculty175_native_face_t face = {
        .id = FACULTY175_FACE_APOCALYPSO,
        .title = "Apocalypso",
        .subtitle = "TIDE OF NOW",
        .style = FACULTY175_NATIVE_ORACLE,
        .hue = 5,
        .a = "SIGNAL / OMEN",
        .b = "BUTTON SHUFFLES",
        .c = "",
    };
    faculty175_face_native_draw(&face, anim_ms);
}
