#include "faculty175_face_native.h"

void faculty175_face_tuning_draw(uint32_t anim_ms)
{
    static const faculty175_native_face_t face = {
        .id = FACULTY175_FACE_TUNING,
        .title = "Tuning",
        .subtitle = "LIVE TUNER",
        .style = FACULTY175_NATIVE_STATUS,
        .hue = 3,
        .a = "NOTE DETECTION",
        .b = "TREBLE STAFF",
        .c = "",
    };
    faculty175_face_native_draw(&face, anim_ms);
}
