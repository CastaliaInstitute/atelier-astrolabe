#include "faculty175_face_native.h"

void faculty175_face_quotes_draw(uint32_t anim_ms)
{
    static const faculty175_native_face_t face = {
        .id = FACULTY175_FACE_QUOTES,
        .title = "Quotes",
        .subtitle = "COMMONPLACE",
        .style = FACULTY175_NATIVE_TEXT,
        .hue = 5,
        .a = "QUOTE OF THE DAY",
        .b = "FACULTY MEMORY",
        .c = "",
    };
    faculty175_face_native_draw(&face, anim_ms);
}
