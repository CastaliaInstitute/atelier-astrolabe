#include "faculty175_face_native.h"
#include "faculty175_km.h"

void faculty175_face_hid_draw(uint32_t anim_ms)
{
    const faculty175_native_face_t face = {
        .id = FACULTY175_FACE_HID,
        .title = "WiFi KM",
        .subtitle = faculty175_km_usb_ready() ? "USB CONNECTED" : "WAITING FOR USB",
        .style = FACULTY175_NATIVE_STATUS,
        .hue = 2,
        .a = faculty175_km_install_armed() ? "TAP AGAIN: INSTALL" : faculty175_km_pairing_code(),
        .b = faculty175_km_install_armed() ? "PI DESKTOP MUST BE READY" : "OPEN /km THEN PAIR",
        .c = "DOUBLE TAP INSTALLS PI AGENT",
    };
    faculty175_face_native_draw(&face, anim_ms);
}
