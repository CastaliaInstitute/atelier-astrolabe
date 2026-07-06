#include "faculty175_face_incidents.h"

#include <stdio.h>

#include "faculty175_face_native.h"
#include "faculty175_wifi_monitor.h"

static size_t s_scroll;

void faculty175_face_incidents_draw(uint32_t anim_ms)
{
    const size_t total = faculty175_wifi_monitor_count();
    if (s_scroll >= total && total > 0) {
        s_scroll = total - 1u;
    }

    faculty175_wifi_incident_t event = {};
    const bool have = total > 0 && faculty175_wifi_monitor_get_newest(s_scroll, &event);

    static char line_a[56];
    static char line_b[56];
    static char line_c[56];
    if (have) {
        snprintf(line_a, sizeof(line_a), "%s", event.type);
        if (event.ssid[0] != '\0') {
            snprintf(line_b, sizeof(line_b), "%s", event.ssid);
        } else {
            snprintf(line_b, sizeof(line_b), "%s", event.detail);
        }
        snprintf(line_c,
                 sizeof(line_c),
                 "#%lu %lus swipe tap=clear",
                 (unsigned long)event.seq,
                 (unsigned long)(event.uptime_ms / 1000u));
    } else {
        snprintf(line_a, sizeof(line_a), "NO INCIDENTS");
        snprintf(line_b, sizeof(line_b), "SecOps events appear here");
        line_c[0] = '\0';
    }

    static const faculty175_native_face_t face = {
        .id = FACULTY175_FACE_INCIDENTS,
        .title = "Incidents",
        .subtitle = "SECOPS LOG",
        .style = FACULTY175_NATIVE_STATUS,
        .hue = 1,
    };
    faculty175_native_face_t view = face;
    view.a = line_a;
    view.b = line_b;
    view.c = have ? line_c : "lab + wifi events";
    (void)total;
    faculty175_face_native_draw(&view, anim_ms);
}

bool faculty175_face_incidents_action(uint32_t seed_ms)
{
    (void)seed_ms;
    faculty175_wifi_monitor_clear();
    s_scroll = 0;
    return true;
}

bool faculty175_face_incidents_scroll(int delta)
{
    const size_t total = faculty175_wifi_monitor_count();
    if (total == 0 || delta == 0) {
        return false;
    }
    if (delta > 0) {
        if (s_scroll + (size_t)delta >= total) {
            s_scroll = total - 1u;
        } else {
            s_scroll += (size_t)delta;
        }
    } else {
        const size_t step = (size_t)(-delta);
        if (s_scroll <= step) {
            s_scroll = 0;
        } else {
            s_scroll -= step;
        }
    }
    return true;
}

size_t faculty175_face_incidents_scroll_index(void)
{
    return s_scroll;
}