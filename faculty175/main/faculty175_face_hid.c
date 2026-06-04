#include "faculty175_face_native.h"

void faculty175_face_hid_draw(uint32_t anim_ms)
{
    static const faculty175_native_face_t face = {
        .id = FACULTY175_FACE_HID,
        .title = "HID Touchpad",
        .subtitle = "USB CONTROL",
        .style = FACULTY175_NATIVE_STATUS,
        .hue = 2,
        .a = "MOUSE / TOUCHPAD",
        .b = "NATIVE USB",
        .c = "",
    };
    faculty175_face_native_draw(&face, anim_ms);
}
