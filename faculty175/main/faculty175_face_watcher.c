#include "faculty175_face_native.h"
#include "faculty175_wifi_monitor.h"

#include <stdio.h>

void faculty175_face_watcher_draw(uint32_t anim_ms)
{
    faculty175_wifi_incident_t latest = {};
    const size_t total = faculty175_wifi_monitor_count();
    const bool have = faculty175_wifi_monitor_get_newest(0, &latest);

    static char line_a[48];
    static char line_b[48];
    snprintf(line_a, sizeof(line_a), "events=%u", (unsigned)total);
    if (have) {
        snprintf(line_b, sizeof(line_b), "last=%s", latest.type);
    } else {
        snprintf(line_b, sizeof(line_b), "monitoring wifi + lab");
    }

    static const faculty175_native_face_t face = {
        .id = FACULTY175_FACE_WATCHER,
        .title = "Watcher",
        .subtitle = "SECOPS MONITOR",
        .style = FACULTY175_NATIVE_RADAR,
        .hue = 3,
    };
    faculty175_native_face_t view = face;
    view.a = line_a;
    view.b = line_b;
    view.c = "incidents face for log";
    faculty175_face_native_draw(&view, anim_ms);
}
