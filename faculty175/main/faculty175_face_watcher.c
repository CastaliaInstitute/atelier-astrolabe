#include "faculty175_face_native.h"

void faculty175_face_watcher_draw(uint32_t anim_ms)
{
    static const faculty175_native_face_t face = {
        .id = FACULTY175_FACE_WATCHER,
        .title = "Watcher",
        .subtitle = "PRESENCE",
        .style = FACULTY175_NATIVE_RADAR,
        .hue = 3,
        .a = "CAMERA METRICS",
        .b = "GREETING PIPELINE",
        .c = "",
    };
    faculty175_face_native_draw(&face, anim_ms);
}
