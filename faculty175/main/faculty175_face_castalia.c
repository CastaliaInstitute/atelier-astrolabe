#include "faculty175_face_native.h"

void faculty175_face_castalia_draw(uint32_t anim_ms)
{
    static const faculty175_native_face_t face = {
        .id = FACULTY175_FACE_CASTALIA,
        .title = "Castalia",
        .subtitle = "SERVICE HUB",
        .style = FACULTY175_NATIVE_TEXT,
        .hue = 3,
        .a = "CASTALIA LINK",
        .b = "ACCOUNT / COMMONPLACE",
        .c = "",
    };
    faculty175_face_native_draw(&face, anim_ms);
}
