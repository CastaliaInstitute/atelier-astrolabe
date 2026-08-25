#include "faculty175_face_native.h"

#include <stdio.h>

#include "faculty175_xdj_bridge.h"

/* The transport implementation is intentionally kept behind this face. The
 * face is registered now so navigation/API clients can target a stable slug
 * while the USB-host MIDI class driver is added. */
void faculty175_face_xdj_draw(uint32_t anim_ms)
{
    static char line_a[48];
    static char line_b[48];
    static char line_c[48];

    (void)faculty175_xdj_bridge_start();
    snprintf(line_a, sizeof(line_a), "USB MIDI HOST");
    snprintf(line_b, sizeof(line_b), "XDJ-RX2  %s", faculty175_xdj_bridge_usb_state_name());
    snprintf(line_c, sizeof(line_c), "WIFI STREAM %u", (unsigned)faculty175_xdj_bridge_stream_port());

    const faculty175_native_face_t face = {
        .id = FACULTY175_FACE_XDJ,
        .title = "XDJ Bridge",
        .subtitle = "USB MIDI TO WIFI",
        .style = FACULTY175_NATIVE_STATUS,
        .hue = 5,
        .a = line_a,
        .b = line_b,
        .c = line_c,
    };
    faculty175_face_native_draw(&face, anim_ms);
}
