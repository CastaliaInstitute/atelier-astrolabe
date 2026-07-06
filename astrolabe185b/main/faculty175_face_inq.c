#include "faculty175_face_native.h"

void faculty175_face_inq_draw(uint32_t anim_ms)
{
    static const faculty175_native_face_t face = {
        .id = FACULTY175_FACE_INQ,
        .title = "iNQ Card",
        .subtitle = "DAILY CARD",
        .style = FACULTY175_NATIVE_ORACLE,
        .hue = 1,
        .a = "INQUIRY PROMPT",
        .b = "CASTALIA CARDS",
        .c = "",
    };
    faculty175_face_native_draw(&face, anim_ms);
}
