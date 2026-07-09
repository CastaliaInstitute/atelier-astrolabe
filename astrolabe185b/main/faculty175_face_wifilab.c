#include "faculty175_face_wifilab.h"

#include "faculty175_face_native.h"
#include "faculty175_wifi_lab.h"

static faculty175_native_face_t native_desc(faculty175_face_id_t id)
{
    faculty175_wifi_lab_state_t state = {};
    faculty175_wifi_lab_get_state(&state);

    faculty175_native_face_t face = {
        .id = id,
        .title = "WiFi Lab",
        .subtitle = "AUTHORIZED USE",
        .style = FACULTY175_NATIVE_RADAR,
        .hue = 1,
        .a = state.status,
        .b = state.detail,
        .c = "",
    };

    if (id == FACULTY175_FACE_WSCAN) {
        face.title = "WiFi Scan";
        face.subtitle = "NETWORK SURVEY";
        if (state.ap_count > 0 && state.selected < state.ap_count) {
            static char line_a[48];
            static char line_b[48];
            snprintf(line_a, sizeof(line_a), "%s %ddBm", state.aps[state.selected].ssid, state.aps[state.selected].rssi);
            snprintf(line_b, sizeof(line_b), "ch=%u %u total tap=rescan",
                     (unsigned)state.aps[state.selected].channel,
                     (unsigned)state.ap_count);
            face.a = line_a;
            face.b = line_b;
        } else {
            face.a = "BROAD 2.4GHZ SCAN";
            face.b = "tap to scan";
        }
    } else if (id == FACULTY175_FACE_DEAUTH) {
        face.title = "Deauth";
        face.subtitle = "802.11 LAB";
        static char line_a[48];
        snprintf(line_a, sizeof(line_a), "sent=%lu %s",
                 (unsigned long)state.deauth_sent,
                 state.active ? "ACTIVE" : "idle");
        face.a = line_a;
        face.b = state.target_ssid[0] != '\0' ? state.target_ssid : "pick target on scan";
        face.c = "tap start/stop";
    } else if (id == FACULTY175_FACE_EVILTWIN) {
        face.title = "Evil Twin";
        face.subtitle = "CAPTURE LAB";
        static char line_a[48];
        snprintf(line_a, sizeof(line_a), "captures=%lu", (unsigned long)state.capture_count);
        face.a = line_a;
        face.b = state.last_cred[0] != '\0' ? state.last_cred : state.target_ssid;
        face.c = "portal /lab/portal";
    } else if (id == FACULTY175_FACE_HANDSHAKE) {
        face.title = "Handshake";
        face.subtitle = "EAPOL LAB";
        static char line_a[48];
        static char line_b[48];
        snprintf(line_a, sizeof(line_a), "eapol=%lu %s",
                 (unsigned long)state.handshake_count,
                 state.active ? "ACTIVE" : "idle");
        if (state.pcap_path[0] != '\0') {
            snprintf(line_b, sizeof(line_b), "pcap %uB /lab/handshake.pcap", (unsigned)state.pcap_bytes);
        } else {
            snprintf(line_b, sizeof(line_b), "%s", state.target_ssid[0] != '\0' ? state.target_ssid : "pick target on scan");
        }
        face.a = line_a;
        face.b = line_b;
        face.c = "tap start/stop exports pcap";
    }

    return face;
}

void faculty175_face_wifilab_draw(faculty175_face_id_t id, uint32_t anim_ms)
{
    if (!faculty175_wifi_lab_is_face(id)) {
        return;
    }
    const faculty175_native_face_t face = native_desc(id);
    faculty175_face_native_draw(&face, anim_ms);
}

bool faculty175_face_wifilab_action(faculty175_face_id_t id, uint32_t seed_ms)
{
    (void)seed_ms;
    return faculty175_wifi_lab_tap(id);
}

void faculty175_face_wifilab_tick(faculty175_face_id_t id, uint32_t now_ms)
{
    faculty175_wifi_lab_tick(id, now_ms);
}

void faculty175_face_wifilab_enter(faculty175_face_id_t id)
{
    faculty175_wifi_lab_on_enter(id);
}

void faculty175_face_wifilab_leave(faculty175_face_id_t id)
{
    faculty175_wifi_lab_on_leave(id);
}
