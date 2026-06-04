#include "faculty175_face_native.h"

void faculty175_face_digital_draw(uint32_t anim_ms)
{
    static const faculty175_native_face_t face = {
        .id = FACULTY175_FACE_DIGITAL,
        .title = "Digital Local",
        .subtitle = "DEVICE TIMEBASE",
        .style = FACULTY175_NATIVE_DIGITAL,
        .hue = 3,
        .a = "SECONDS SINCE BOOT",
        .b = "LOCAL CLOCK FACE",
        .c = "",
    };
    faculty175_face_native_draw(&face, anim_ms);
}
