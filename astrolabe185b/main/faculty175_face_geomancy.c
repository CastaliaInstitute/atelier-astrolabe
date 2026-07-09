#include "faculty175_face_native.h"

void faculty175_face_geomancy_draw(uint32_t anim_ms)
{
    static const faculty175_native_face_t face = {
        .id = FACULTY175_FACE_GEOMANCY,
        .title = "Geomancy",
        .subtitle = "16 FIGURES",
        .style = FACULTY175_NATIVE_ORACLE,
        .hue = 0,
        .a = "DAILY FIGURE",
        .b = "BUTTON CASTS",
        .c = "",
    };
    faculty175_face_native_draw(&face, anim_ms);
}
