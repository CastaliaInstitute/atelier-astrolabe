#include "faculty175_face_native.h"

void faculty175_face_bowl_draw(uint32_t anim_ms)
{
    static const faculty175_native_face_t face = {
        .id = FACULTY175_FACE_BOWL,
        .title = "Tibetan Bowl",
        .subtitle = "RING STRIKE",
        .style = FACULTY175_NATIVE_INSTRUMENT,
        .hue = 0,
        .a = "SUSTAIN / DECAY",
        .b = "TOUCH RIM",
        .c = "",
    };
    faculty175_face_native_draw(&face, anim_ms);
}
