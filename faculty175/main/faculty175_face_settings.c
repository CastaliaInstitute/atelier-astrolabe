#include "faculty175_face_native.h"

void faculty175_face_settings_draw(uint32_t anim_ms)
{
    static const faculty175_native_face_t face = {
        .id = FACULTY175_FACE_SETTINGS,
        .title = "Settings",
        .subtitle = "DEVICE",
        .style = FACULTY175_NATIVE_STATUS,
        .hue = 3,
        .a = "WIFI / OTA / FACE",
        .b = "NVS FLAGS",
        .c = "",
    };
    faculty175_face_native_draw(&face, anim_ms);
}
