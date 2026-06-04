#include "faculty175_face_native.h"

void faculty175_face_radar_draw(uint32_t anim_ms)
{
    static const faculty175_native_face_t face = {
        .id = FACULTY175_FACE_RADAR,
        .title = "Radar",
        .subtitle = "PEER FIELD",
        .style = FACULTY175_NATIVE_RADAR,
        .hue = 2,
        .a = "BLE / BEARING",
        .b = "LOCAL SWEEP",
        .c = "",
    };
    faculty175_face_native_draw(&face, anim_ms);
}
