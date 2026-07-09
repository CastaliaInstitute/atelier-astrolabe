#include "faculty175_face_native.h"

void faculty175_face_enochian_draw(uint32_t anim_ms)
{
    static const faculty175_native_face_t face = {
        .id = FACULTY175_FACE_ENOCHIAN,
        .title = "Enochian Angel",
        .subtitle = "TABLET ORACLE",
        .style = FACULTY175_NATIVE_TEXT,
        .hue = 4,
        .a = "LUMINOUS SIGIL",
        .b = "ANGELIC FACE",
        .c = "",
    };
    faculty175_face_native_draw(&face, anim_ms);
}
