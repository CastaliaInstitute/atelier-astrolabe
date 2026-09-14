#include "faculty175_face_native.h"
#include "faculty175_ota.h"
#include "faculty175_wifi_settings.h"

void faculty175_face_ota_draw(uint32_t anim_ms)
{
    faculty175_ota_status_t status = {};
    faculty175_ota_get_status(&status);
    faculty175_native_face_t face = {
        .id = FACULTY175_FACE_OTA,
        .title = "Firmware Update",
        .subtitle = "WI-FI OTA",
        .style = FACULTY175_NATIVE_STATUS,
        .hue = 3,
        .a = status.active ? "INSTALLING - KEEP POWER CONNECTED" :
             status.network_ready ? "READY FOR UPDATE" : "CONNECTING WI-FI",
        .b = faculty175_wifi_settings_url(),
        .c = status.last,
    };
    faculty175_face_native_draw(&face, anim_ms);
}
