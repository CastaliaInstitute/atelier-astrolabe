#include "faculty175_face_native.h"

void faculty175_face_rocket_draw(uint32_t anim_ms)
{
    static const faculty175_native_face_t face = {
        .id = FACULTY175_FACE_ROCKET,
        .title = "Rocket",
        .subtitle = "LAUNCH CLOCK",
        .style = FACULTY175_NATIVE_STATUS,
        .hue = 0,
        .a = "WINDOW TRACKER",
        .b = "14 DAY DIAL",
        .c = "",
    };
    faculty175_face_native_draw(&face, anim_ms);
}
