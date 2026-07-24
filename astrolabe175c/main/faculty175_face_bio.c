#include "faculty175_face_native.h"

void faculty175_face_bio_draw(uint32_t anim_ms)
{
    static const faculty175_native_face_t face = {
        .id = FACULTY175_FACE_BIOMETRICS,
        .title = "Ring",
        .subtitle = "GESTURE + READINESS",
        .style = FACULTY175_NATIVE_STATUS,
        .hue = 2,
        .a = "ATTENTION MODEL",
        .b = "LOCAL SIGNALS",
        .c = "",
    };
    faculty175_face_native_draw(&face, anim_ms);
}
