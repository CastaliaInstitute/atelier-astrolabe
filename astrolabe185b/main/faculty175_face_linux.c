#include "faculty175_face_native.h"

#include <stdio.h>

#include "faculty175_usb.h"
#if ASTROLABE185B_CYBER_FEATURES
#include "faculty175_usb_ncm.h"
#endif

void faculty175_face_linux_draw(uint32_t anim_ms)
{
    (void)anim_ms;
    static char line_a[48];
    static char line_b[48];
    static char line_c[48];

    const bool sd_ready = faculty175_usb_storage_ready();
#if ASTROLABE185B_CYBER_FEATURES
    const bool ncm_ready = faculty175_usb_ncm_ready();
#else
    const bool ncm_ready = false;
#endif

    snprintf(line_a, sizeof(line_a), "MSC SD %s", sd_ready ? "READY" : "WAITING");
    snprintf(line_b, sizeof(line_b), "NCM %s  172.31.77.1", ncm_ready ? "UP" : "WAIT HOST");
    snprintf(line_c, sizeof(line_c), "SSH root@172.31.77.2");

    const faculty175_native_face_t face = {
        .id = FACULTY175_FACE_LINUX,
        .title = "Linux Host",
        .subtitle = "CYBER SYSTEM DASHBOARD",
        .style = FACULTY175_NATIVE_STATUS,
        .hue = 3,
        .a = line_a,
        .b = line_b,
        .c = line_c,
    };
    faculty175_face_native_draw(&face, anim_ms);
}
