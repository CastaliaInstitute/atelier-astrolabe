#include "faculty175_wifi_monitor.h"

#include <stdio.h>
#include <string.h>

#include "esp_timer.h"

#include "faculty175_util.h"

#define DISRUPTION_WINDOW_MS 60000u
#define DISRUPTION_THRESHOLD 3u

static faculty175_wifi_incident_t s_events[FACULTY175_WIFI_INCIDENT_MAX];
static uint32_t s_next_seq = 1;
static size_t s_next_index;
static size_t s_count;
static uint32_t s_disconnect_times[DISRUPTION_THRESHOLD];
static size_t s_disconnect_next;

static uint32_t uptime_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void bssid_copy(uint8_t out[6], const uint8_t *in)
{
    if (out == NULL) {
        return;
    }
    if (in != NULL) {
        memcpy(out, in, 6);
    } else {
        memset(out, 0, 6);
    }
}

static void add_event(const char *type,
                      const char *ssid,
                      const uint8_t *bssid,
                      int reason,
                      int rssi,
                      uint8_t channel,
                      wifi_auth_mode_t authmode,
                      const char *detail)
{
    faculty175_wifi_incident_t *event = &s_events[s_next_index];
    memset(event, 0, sizeof(*event));
    event->seq = s_next_seq++;
    event->uptime_ms = uptime_ms();
    faculty175_strlcpy(event->type, type != NULL ? type : "event", sizeof(event->type));
    faculty175_strlcpy(event->ssid, ssid != NULL ? ssid : "", sizeof(event->ssid));
    bssid_copy(event->bssid, bssid);
    event->reason = reason;
    event->rssi = rssi;
    event->channel = channel;
    event->authmode = authmode;
    faculty175_strlcpy(event->detail, detail != NULL ? detail : "", sizeof(event->detail));

    s_next_index = (s_next_index + 1u) % FACULTY175_WIFI_INCIDENT_MAX;
    if (s_count < FACULTY175_WIFI_INCIDENT_MAX) {
        ++s_count;
    }
}

static void maybe_record_disruption(void)
{
    const uint32_t now = uptime_ms();
    s_disconnect_times[s_disconnect_next] = now;
    s_disconnect_next = (s_disconnect_next + 1u) % DISRUPTION_THRESHOLD;

    size_t recent = 0;
    for (size_t i = 0; i < DISRUPTION_THRESHOLD; ++i) {
        const uint32_t t = s_disconnect_times[i];
        if (t != 0 && now - t <= DISRUPTION_WINDOW_MS) {
            ++recent;
        }
    }
    if (recent >= DISRUPTION_THRESHOLD) {
        add_event("disruption", "", NULL, 0, 0, 0, WIFI_AUTH_MAX, "repeated disconnects");
    }
}

void faculty175_wifi_monitor_record_connected(const wifi_ap_record_t *ap)
{
    if (ap == NULL) {
        add_event("connected", "", NULL, 0, 0, 0, WIFI_AUTH_MAX, "");
        return;
    }
    add_event("connected",
              (const char *)ap->ssid,
              ap->bssid,
              0,
              ap->rssi,
              ap->primary,
              ap->authmode,
              "");
}

void faculty175_wifi_monitor_record_disconnected(const char *ssid,
                                                 const uint8_t bssid[6],
                                                 int reason,
                                                 int rssi)
{
    char detail[32];
    snprintf(detail, sizeof(detail), "reason=%d", reason);
    add_event("disconnect", ssid, bssid, reason, rssi, 0, WIFI_AUTH_MAX, detail);
    maybe_record_disruption();
}

void faculty175_wifi_monitor_record_ap_client(bool connected, int aid)
{
    char detail[32];
    snprintf(detail, sizeof(detail), "aid=%d", aid);
    add_event(connected ? "ap-client-join" : "ap-client-left", "", NULL, 0, 0, 0, WIFI_AUTH_MAX, detail);
}

void faculty175_wifi_monitor_record_scan(uint16_t count)
{
    char detail[32];
    snprintf(detail, sizeof(detail), "count=%u", (unsigned)count);
    add_event("scan", "", NULL, 0, 0, 0, WIFI_AUTH_MAX, detail);
}

void faculty175_wifi_monitor_record_note(const char *type, const char *detail)
{
    add_event(type != NULL ? type : "note", "", NULL, 0, 0, 0, WIFI_AUTH_MAX, detail);
}

size_t faculty175_wifi_monitor_count(void)
{
    return s_count;
}

bool faculty175_wifi_monitor_get_newest(size_t offset, faculty175_wifi_incident_t *out)
{
    if (out == NULL || offset >= s_count) {
        return false;
    }
    const size_t newest_index =
        s_count == FACULTY175_WIFI_INCIDENT_MAX ? (s_next_index + FACULTY175_WIFI_INCIDENT_MAX - 1u) % FACULTY175_WIFI_INCIDENT_MAX
                                                : s_count - 1u;
    const size_t index = (newest_index + FACULTY175_WIFI_INCIDENT_MAX - offset) % FACULTY175_WIFI_INCIDENT_MAX;
    *out = s_events[index];
    return true;
}

size_t faculty175_wifi_monitor_copy(faculty175_wifi_incident_t *out, size_t cap)
{
    if (out == NULL || cap == 0) {
        return 0;
    }
    const size_t n = s_count < cap ? s_count : cap;
    for (size_t i = 0; i < n; ++i) {
        if (!faculty175_wifi_monitor_get_newest(i, &out[i])) {
            return i;
        }
    }
    return n;
}

void faculty175_wifi_monitor_clear(void)
{
    memset(s_events, 0, sizeof(s_events));
    memset(s_disconnect_times, 0, sizeof(s_disconnect_times));
    s_next_index = 0;
    s_count = 0;
    s_disconnect_next = 0;
}

