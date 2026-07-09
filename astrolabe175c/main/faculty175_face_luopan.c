#include "faculty175_face_native.h"

void faculty175_face_luopan_draw(uint32_t anim_ms)
{
    static const faculty175_native_face_t face = {
        .id = FACULTY175_FACE_LUOPAN,
        .title = "Luopan",
        .subtitle = "FENG SHUI DIAL",
        .style = FACULTY175_NATIVE_ORACLE,
        .hue = 0,
        .a = "RELATIVE COMPASS",
        .b = "RINGS / SECTORS",
        .c = "",
    };
    faculty175_face_native_draw(&face, anim_ms);
}
