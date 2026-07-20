#include "faculty175_serial.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_app_desc.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_wifi.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "astrolabe_time.h"
#include "faculty175_board.h"
#include "faculty175_breath.h"
#include "faculty175_ble.h"
#include "faculty175_charts.h"
#include "faculty175_device_auth.h"
#include "faculty175_deep_sleep.h"
#include "faculty175_family.h"
#include "faculty175_face_dispatch.h"
#include "faculty175_faces.h"
#include "faculty175_gesture.h"
#include "faculty175_km.h"
#include "faculty175_qa.h"
#include "faculty175_ota.h"
#include "faculty175_pocketwatch.h"
#include "faculty175_power_metrics.h"
#include "faculty175_pmu.h"
#include "faculty175_quotes.h"
#include "faculty175_rocket.h"
#include "faculty175_touch.h"
#include "faculty175_voice.h"
#include "faculty175_usb_screen.h"
#include "faculty175_wifi_monitor.h"
#include "faculty175_wifi_settings.h"

static const char *TAG = "faculty175_serial";
#define FACULTY175_SERIAL_TASK_STACK 8192
static TaskHandle_t s_serial_task;

static void trim_inplace(char *line)
{
    if (line == NULL) {
        return;
    }
    char *start = line;
    while (*start != '\0' && isspace((unsigned char)*start)) {
        ++start;
    }
    if (start != line) {
        memmove(line, start, strlen(start) + 1u);
    }
    size_t len = strlen(line);
    while (len > 0 && isspace((unsigned char)line[len - 1])) {
        line[--len] = '\0';
    }
}

static bool line_is(const char *line, const char *cmd)
{
    return line != NULL && cmd != NULL && strcasecmp(line, cmd) == 0;
}

static const char *parse_serial_arg(const char *p, char *out, size_t cap)
{
    if (out == NULL || cap == 0) {
        return p;
    }
    out[0] = '\0';
    if (p == NULL) {
        return NULL;
    }
    while (*p != '\0' && isspace((unsigned char)*p)) {
        ++p;
    }
    if (*p == '\0') {
        return p;
    }
    size_t w = 0;
    if (*p == '"') {
        ++p;
        while (*p != '\0' && *p != '"' && w + 1 < cap) {
            if (*p == '\\' && p[1] != '\0') {
                ++p;
            }
            out[w++] = *p++;
        }
        if (*p == '"') {
            ++p;
        }
    } else {
        while (*p != '\0' && !isspace((unsigned char)*p) && w + 1 < cap) {
            out[w++] = *p++;
        }
    }
    out[w] = '\0';
    while (*p != '\0' && isspace((unsigned char)*p)) {
        ++p;
    }
    return p;
}

static const char *wifi_auth_name(wifi_auth_mode_t auth)
{
    switch (auth) {
    case WIFI_AUTH_OPEN:
        return "open";
    case WIFI_AUTH_WEP:
        return "wep";
    case WIFI_AUTH_WPA_PSK:
        return "wpa";
    case WIFI_AUTH_WPA2_PSK:
        return "wpa2";
    case WIFI_AUTH_WPA_WPA2_PSK:
        return "wpa/wpa2";
    case WIFI_AUTH_WPA2_ENTERPRISE:
        return "wpa2-ent";
    case WIFI_AUTH_WPA3_PSK:
        return "wpa3";
    case WIFI_AUTH_WPA2_WPA3_PSK:
        return "wpa2/wpa3";
    default:
        return "?";
    }
}

static void print_bssid(const uint8_t bssid[6])
{
    printf("%02x:%02x:%02x:%02x:%02x:%02x",
           bssid[0],
           bssid[1],
           bssid[2],
           bssid[3],
           bssid[4],
           bssid[5]);
}

static bool handle_wifi_command(const char *line)
{
    if (line == NULL || (strcasecmp(line, "wifi") != 0 && strncasecmp(line, "wifi ", 5) != 0)) {
        return false;
    }

    const char *sub = line + 4;
    while (*sub != '\0' && isspace((unsigned char)*sub)) {
        ++sub;
    }
    if (*sub == '\0' || strcasecmp(sub, "status") == 0) {
        faculty175_wifi_known_t known[FACULTY175_WIFI_KNOWN_MAX] = {};
        const size_t known_count = faculty175_wifi_settings_load_known(known, FACULTY175_WIFI_KNOWN_MAX);
        wifi_ap_record_t ap = {};
        const esp_err_t ap_err = esp_wifi_sta_get_ap_info(&ap);
        printf("wifi: runtime status=\"%s\" ssid=\"%s\" url=\"%s\" ap_active=%s ap_client=%s\n",
               faculty175_wifi_settings_status(),
               faculty175_wifi_settings_ssid(),
               faculty175_wifi_settings_url(),
               faculty175_wifi_settings_ap_active() ? "yes" : "no",
               faculty175_wifi_settings_ap_client_connected() ? "yes" : "no");
        printf("wifi: travel_router=%s upstream=\"%s\"\n",
               faculty175_wifi_settings_travel_router_enabled() ? "on" : "off",
               faculty175_wifi_settings_upstream_ssid());
        if (ap_err == ESP_OK) {
            printf("wifi: sta ssid=\"%s\" rssi=%d ch=%u auth=%s\n",
                   (const char *)ap.ssid,
                   (int)ap.rssi,
                   (unsigned)ap.primary,
                   wifi_auth_name(ap.authmode));
        } else {
            printf("wifi: sta disconnected err=%s\n", esp_err_to_name(ap_err));
        }
        printf("wifi: saved=%s ssid=\"%s\" pass=%s\n",
               known_count > 0 ? "yes" : "no",
               known_count > 0 ? known[0].ssid : "",
               (known_count > 0 && known[0].pass[0] != '\0') ? "set" : "empty");
        printf("wifi: known count=%u\n", (unsigned)known_count);
        for (size_t i = 0; i < known_count; ++i) {
            printf("wifi: known %u ssid=\"%s\" pass=%s%s\n",
                   (unsigned)i,
                   known[i].ssid,
                   known[i].pass[0] != '\0' ? "set" : "empty",
                   i == 0 ? " primary" : "");
        }
        fflush(stdout);
        return true;
    }

    if (strcasecmp(sub, "list") == 0 || strcasecmp(sub, "known") == 0) {
        faculty175_wifi_known_t known[FACULTY175_WIFI_KNOWN_MAX] = {};
        const size_t known_count = faculty175_wifi_settings_load_known(known, FACULTY175_WIFI_KNOWN_MAX);
        printf("wifi: known count=%u\n", (unsigned)known_count);
        for (size_t i = 0; i < known_count; ++i) {
            printf("wifi: known %u ssid=\"%s\" pass=%s%s\n",
                   (unsigned)i,
                   known[i].ssid,
                   known[i].pass[0] != '\0' ? "set" : "empty",
                   i == 0 ? " primary" : "");
        }
        fflush(stdout);
        return true;
    }

    if (strncasecmp(sub, "hostname", 8) == 0 &&
        (sub[8] == '\0' || isspace((unsigned char)sub[8]))) {
        const char *arg = sub + 8;
        while (*arg != '\0' && isspace((unsigned char)*arg)) {
            ++arg;
        }
        char hostname[FACULTY175_WIFI_HOSTNAME_MAX + 1] = {};
        if (*arg == '\0' || strcasecmp(arg, "status") == 0) {
            const esp_err_t err = faculty175_wifi_settings_load_hostname(hostname, sizeof(hostname));
            printf("wifi: hostname=%s%s%s\n",
                   err == ESP_OK ? "\"" : "auto (astrolabe-XXXX)",
                   err == ESP_OK ? hostname : "",
                   err == ESP_OK ? "\"" : "");
            fflush(stdout);
            return true;
        }
        const bool clear = strcasecmp(arg, "auto") == 0 || strcasecmp(arg, "clear") == 0;
        const esp_err_t err = faculty175_wifi_settings_save_hostname(clear ? NULL : arg);
        printf("wifi: hostname=%s err=%s%s\n",
               clear ? "auto (astrolabe-XXXX)" : arg,
               esp_err_to_name(err),
               err == ESP_OK ? " (rebooting)" : "");
        fflush(stdout);
        if (err == ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(250));
            esp_restart();
        }
        return true;
    }

    if (strcasecmp(sub, "incidents") == 0 || strcasecmp(sub, "events") == 0) {
        faculty175_wifi_incident_t events[FACULTY175_WIFI_INCIDENT_MAX] = {};
        const size_t count = faculty175_wifi_monitor_copy(events, FACULTY175_WIFI_INCIDENT_MAX);
        printf("wifi: incidents count=%u\n", (unsigned)count);
        for (size_t i = 0; i < count; ++i) {
            printf("wifi: incident seq=%lu t=%lums type=%s ssid=\"%s\" bssid=",
                   (unsigned long)events[i].seq,
                   (unsigned long)events[i].uptime_ms,
                   events[i].type,
                   events[i].ssid);
            print_bssid(events[i].bssid);
            printf(" reason=%d rssi=%d ch=%u auth=%s detail=\"%s\"\n",
                   events[i].reason,
                   events[i].rssi,
                   (unsigned)events[i].channel,
                   wifi_auth_name(events[i].authmode),
                   events[i].detail);
        }
        fflush(stdout);
        return true;
    }

    if (strcasecmp(sub, "incidents clear") == 0 || strcasecmp(sub, "events clear") == 0) {
        faculty175_wifi_monitor_clear();
        printf("wifi: incidents cleared\n");
        fflush(stdout);
        return true;
    }

    if (strncasecmp(sub, "router", 6) == 0) {
        const char *arg = sub + 6;
        while (*arg != '\0' && isspace((unsigned char)*arg)) {
            ++arg;
        }
        if (*arg == '\0' || strcasecmp(arg, "status") == 0) {
            printf("wifi: travel_router=%s upstream=\"%s\"\n",
                   faculty175_wifi_settings_travel_router_enabled() ? "on" : "off",
                   faculty175_wifi_settings_upstream_ssid());
            fflush(stdout);
            return true;
        }
        const bool enable = strcasecmp(arg, "on") == 0 || strcasecmp(arg, "enable") == 0 ||
                            strcasecmp(arg, "enabled") == 0 || strcmp(arg, "1") == 0;
        const bool disable = strcasecmp(arg, "off") == 0 || strcasecmp(arg, "disable") == 0 ||
                             strcasecmp(arg, "disabled") == 0 || strcmp(arg, "0") == 0;
        if (!enable && !disable) {
            printf("wifi: router usage: wifi router on|off\n");
            fflush(stdout);
            return true;
        }
        const esp_err_t err = faculty175_wifi_settings_set_travel_router_enabled(enable);
        printf("wifi: travel_router=%s err=%s (reboot or wifi set to apply)\n",
               enable ? "on" : "off",
               esp_err_to_name(err));
        fflush(stdout);
        return true;
    }

    if (strcasecmp(sub, "scan") == 0) {
        wifi_mode_t mode = WIFI_MODE_NULL;
        (void)esp_wifi_get_mode(&mode);
        faculty175_wifi_settings_set_scan_suppressed(true);
        if (mode == WIFI_MODE_AP) {
            (void)esp_wifi_set_mode(WIFI_MODE_APSTA);
        }
        (void)esp_wifi_disconnect();
        vTaskDelay(pdMS_TO_TICKS(250));
        wifi_scan_config_t scan = {
            .show_hidden = true,
        };
        printf("wifi: scan begin\n");
        fflush(stdout);
        esp_err_t err = esp_wifi_scan_start(&scan, true);
        if (err != ESP_OK) {
            printf("wifi: scan err=%s\n", esp_err_to_name(err));
            fflush(stdout);
            faculty175_wifi_settings_set_scan_suppressed(false);
            return true;
        }
        uint16_t count = 0;
        err = esp_wifi_scan_get_ap_num(&count);
        if (err != ESP_OK) {
            printf("wifi: scan count err=%s\n", esp_err_to_name(err));
            fflush(stdout);
            faculty175_wifi_settings_set_scan_suppressed(false);
            return true;
        }
        if (count > 24) {
            count = 24;
        }
        wifi_ap_record_t aps[24] = {};
        err = esp_wifi_scan_get_ap_records(&count, aps);
        if (err != ESP_OK) {
            printf("wifi: scan records err=%s\n", esp_err_to_name(err));
            fflush(stdout);
            faculty175_wifi_settings_set_scan_suppressed(false);
            return true;
        }
        for (uint16_t i = 0; i < count; ++i) {
            printf("wifi: ap %02u ssid=\"%s\" rssi=%d ch=%u auth=%s\n",
                   (unsigned)i,
                   (const char *)aps[i].ssid,
                   (int)aps[i].rssi,
                   (unsigned)aps[i].primary,
                   wifi_auth_name(aps[i].authmode));
        }
        faculty175_wifi_monitor_record_scan(count);
        printf("wifi: scan end count=%u\n", (unsigned)count);
        fflush(stdout);
        faculty175_wifi_settings_set_scan_suppressed(false);
        return true;
    }

    if (strcasecmp(sub, "reconnect") == 0) {
        const esp_err_t disconnect_err = esp_wifi_disconnect();
        vTaskDelay(pdMS_TO_TICKS(300));
        const esp_err_t connect_err = esp_wifi_connect();
        printf("wifi: reconnect disconnect=%s connect=%s\n",
               esp_err_to_name(disconnect_err),
               esp_err_to_name(connect_err));
        fflush(stdout);
        return true;
    }

    if (strncasecmp(sub, "set ", 4) == 0 || strncasecmp(sub, "save ", 5) == 0 ||
        strncasecmp(sub, "add ", 4) == 0) {
        const char *args = strchr(sub, ' ');
        char ssid[FACULTY175_WIFI_SSID_MAX + 1] = {};
        char pass[FACULTY175_WIFI_PASS_MAX + 1] = {};
        const bool make_primary = strncasecmp(sub, "add ", 4) != 0;
        args = parse_serial_arg(args, ssid, sizeof(ssid));
        (void)parse_serial_arg(args, pass, sizeof(pass));
        if (ssid[0] == '\0') {
            printf("wifi: set usage: wifi set \"SSID\" \"password\" | wifi add \"SSID\" \"password\"\n");
            fflush(stdout);
            return true;
        }
        const esp_err_t err = faculty175_wifi_settings_add_known(ssid, pass, make_primary);
        printf("wifi: %s ssid=\"%s\" pass=%s primary=%s err=%s\n",
               make_primary ? "set" : "add",
               ssid,
               pass[0] != '\0' ? "set" : "empty",
               make_primary ? "yes" : "no",
               esp_err_to_name(err));
        fflush(stdout);
        if (err == ESP_OK && make_primary) {
            vTaskDelay(pdMS_TO_TICKS(250));
            esp_restart();
        }
        return true;
    }

    if (strncasecmp(sub, "remove ", 7) == 0 || strncasecmp(sub, "rm ", 3) == 0) {
        const char *args = strchr(sub, ' ');
        char ssid[FACULTY175_WIFI_SSID_MAX + 1] = {};
        (void)parse_serial_arg(args, ssid, sizeof(ssid));
        const esp_err_t err = ssid[0] != '\0' ? faculty175_wifi_settings_remove_known(ssid) : ESP_ERR_INVALID_ARG;
        printf("wifi: remove ssid=\"%s\" err=%s\n", ssid, esp_err_to_name(err));
        fflush(stdout);
        return true;
    }

    if (strcasecmp(sub, "clear") == 0) {
        const esp_err_t err = faculty175_wifi_settings_clear_known();
        printf("wifi: clear known err=%s\n", esp_err_to_name(err));
        fflush(stdout);
        if (err == ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(250));
            esp_restart();
        }
        return true;
    }

    printf("wifi commands:\n");
    printf("  wifi status\n");
    printf("  wifi list\n");
    printf("  wifi scan\n");
    printf("  wifi reconnect\n");
    printf("  wifi incidents | wifi incidents clear\n");
    printf("  wifi router on|off\n");
    printf("  wifi hostname [name|auto]  (persistent mDNS name and reboot)\n");
    printf("  wifi set \"SSID\" \"password\"  (save primary and reboot)\n");
    printf("  wifi add \"SSID\" \"password\"  (save known network)\n");
    printf("  wifi remove \"SSID\"\n");
    printf("  wifi clear\n");
    fflush(stdout);
    return true;
}

static bool handle_km_command(const char *line)
{
    if (line == NULL || (strcasecmp(line, "km") != 0 && strncasecmp(line, "km ", 3) != 0)) {
        return false;
    }
    const char *sub = line + 2;
    while (isspace((unsigned char)*sub)) {
        ++sub;
    }
    if (*sub == '\0' || strcasecmp(sub, "status") == 0) {
        printf("km: enabled=%s usb=%s pair=%s wifi=/km\n",
               faculty175_km_enabled() ? "yes" : "no",
               faculty175_km_usb_ready() ? "mounted" : "waiting",
               faculty175_km_pairing_code());
        fflush(stdout);
        return true;
    }
    if (strcasecmp(sub, "release") == 0 || strcasecmp(sub, "release-all") == 0) {
        printf("km: release err=%s\n", esp_err_to_name(faculty175_km_release_all()));
        fflush(stdout);
        return true;
    }
    if (strcasecmp(sub, "install") == 0 || strcasecmp(sub, "install-pi") == 0) {
        const esp_err_t err = faculty175_km_install_pi_agent();
        printf("km: install %s err=%s\n",
               err == ESP_ERR_NOT_FINISHED ? "armed; repeat within 10 seconds" : "requested",
               esp_err_to_name(err));
        fflush(stdout);
        return true;
    }
    if (strncasecmp(sub, "type ", 5) == 0) {
        char text[64] = {};
        (void)parse_serial_arg(sub + 5, text, sizeof(text));
        printf("km: type bytes=%u err=%s\n", (unsigned)strlen(text),
               esp_err_to_name(faculty175_km_type_text(text)));
        fflush(stdout);
        return true;
    }
    if (strncasecmp(sub, "mouse ", 6) == 0) {
        int dx = 0;
        int dy = 0;
        int wheel = 0;
        unsigned buttons = 0;
        const int parsed = sscanf(sub + 6, "%d %d %d %u", &dx, &dy, &wheel, &buttons);
        const esp_err_t err = parsed >= 2
            ? faculty175_km_mouse((int16_t)dx, (int16_t)dy,
                                  (int8_t)(parsed >= 3 ? wheel : 0),
                                  (uint8_t)(parsed >= 4 ? buttons : 0))
            : ESP_ERR_INVALID_ARG;
        printf("km: mouse err=%s\n", esp_err_to_name(err));
        fflush(stdout);
        return true;
    }
    if (strncasecmp(sub, "click", 5) == 0) {
        unsigned button = 1;
        (void)sscanf(sub + 5, "%u", &button);
        const esp_err_t down_err = faculty175_km_mouse(0, 0, 0, (uint8_t)button);
        const esp_err_t up_err = faculty175_km_mouse(0, 0, 0, 0);
        printf("km: click down=%s up=%s\n", esp_err_to_name(down_err), esp_err_to_name(up_err));
        fflush(stdout);
        return true;
    }
    if (strncasecmp(sub, "key ", 4) == 0) {
        char name[32] = {};
        char action[12] = {};
        unsigned modifiers = 0;
        const char *args = parse_serial_arg(sub + 4, name, sizeof(name));
        args = parse_serial_arg(args, action, sizeof(action));
        if (args != NULL && *args != '\0') {
            modifiers = (unsigned)strtoul(args, NULL, 0);
        }
        const uint8_t keycode = faculty175_km_keycode_from_dom(name);
        esp_err_t err = keycode == 0 ? ESP_ERR_INVALID_ARG : ESP_OK;
        if (err == ESP_OK && (action[0] == '\0' || strcasecmp(action, "tap") == 0)) {
            err = faculty175_km_key(keycode, true, (uint8_t)modifiers);
            if (err == ESP_OK) {
                err = faculty175_km_key(keycode, false, 0);
            }
        } else if (err == ESP_OK) {
            const bool down = strcasecmp(action, "down") == 0;
            if (!down && strcasecmp(action, "up") != 0) {
                err = ESP_ERR_INVALID_ARG;
            } else {
                err = faculty175_km_key(keycode, down, (uint8_t)modifiers);
            }
        }
        printf("km: key code=%u err=%s\n", (unsigned)keycode, esp_err_to_name(err));
        fflush(stdout);
        return true;
    }
    printf("km commands:\n");
    printf("  km status | km release | km install (twice) | km type \"text\"\n");
    printf("  km mouse <dx> <dy> [wheel] [buttons]\n");
    printf("  km click [buttons] | km key <DOM-code> [tap|down|up] [modifiers]\n");
    fflush(stdout);
    return true;
}

static esp_err_t serial_bmp_write_cb(void *ctx, const uint8_t *data, size_t len)
{
    FILE *out = (FILE *)ctx;
    if (out == NULL || (len > 0 && data == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (len > 0 && fwrite(data, 1, len, out) != len) {
        return ESP_FAIL;
    }
    fflush(out);
    vTaskDelay(pdMS_TO_TICKS(1));
    return ESP_OK;
}

static void serial_put_le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xffu);
    p[1] = (uint8_t)(v >> 8);
}

static void serial_put_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xffu);
    p[1] = (uint8_t)((v >> 8) & 0xffu);
    p[2] = (uint8_t)((v >> 16) & 0xffu);
    p[3] = (uint8_t)((v >> 24) & 0xffu);
}

static esp_err_t write_snapshot_bmp24(const uint16_t *frame,
                                      int w,
                                      int h,
                                      faculty175_display_write_cb_t write_cb,
                                      void *ctx)
{
    if (frame == NULL || write_cb == NULL || w <= 0 || h <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    enum {
        HEADER_BYTES = 54,
        ROWS_PER_CHUNK = 4,
    };
    const uint32_t row_stride = (((uint32_t)w * 24u + 31u) / 32u) * 4u;
    const uint32_t pixel_bytes = row_stride * (uint32_t)h;
    const uint32_t file_size = HEADER_BYTES + pixel_bytes;
    uint8_t header[HEADER_BYTES] = {};

    header[0] = 'B';
    header[1] = 'M';
    serial_put_le32(header + 2, file_size);
    serial_put_le32(header + 10, HEADER_BYTES);
    serial_put_le32(header + 14, 40u);
    serial_put_le32(header + 18, (uint32_t)w);
    serial_put_le32(header + 22, (uint32_t)h);
    serial_put_le16(header + 26, 1u);
    serial_put_le16(header + 28, 24u);
    serial_put_le32(header + 34, pixel_bytes);

    esp_err_t err = write_cb(ctx, header, sizeof(header));
    if (err != ESP_OK) {
        return err;
    }

    const size_t chunk_bytes = (size_t)row_stride * (size_t)ROWS_PER_CHUNK;
    uint8_t *chunk = heap_caps_malloc(chunk_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (chunk == NULL) {
        chunk = malloc(chunk_bytes);
    }
    if (chunk == NULL) {
        return ESP_ERR_NO_MEM;
    }

    for (int yi = 0; yi < h;) {
        const int rows = (h - yi) > ROWS_PER_CHUNK ? ROWS_PER_CHUNK : (h - yi);
        uint8_t *out = chunk;
        for (int r = 0; r < rows; ++r, ++yi) {
            const int sy = h - 1 - yi;
            const uint16_t *src = &frame[sy * w];
            uint8_t *dst = out;
            for (int sx = 0; sx < w; ++sx) {
                const uint16_t px = src[sx];
                const uint8_t red = (uint8_t)((((px >> 11) & 0x1fu) * 255u) / 31u);
                const uint8_t green = (uint8_t)((((px >> 5) & 0x3fu) * 255u) / 63u);
                const uint8_t blue = (uint8_t)(((px & 0x1fu) * 255u) / 31u);
                *dst++ = blue;
                *dst++ = green;
                *dst++ = red;
            }
            while ((uint32_t)(dst - out) < row_stride) {
                *dst++ = 0;
            }
            out += row_stride;
        }
        err = write_cb(ctx, chunk, (size_t)rows * (size_t)row_stride);
        if (err != ESP_OK) {
            free(chunk);
            return err;
        }
    }

    free(chunk);
    return ESP_OK;
}

static void emit_screen_bmp_snapshot(bool render_face)
{
    const size_t pixels = faculty175_display_frame_pixel_count();
    const size_t frame_bytes = pixels * sizeof(uint16_t);
    if (pixels == 0) {
        printf("screen: error no framebuffer\n");
        fflush(stdout);
        return;
    }

    uint16_t *frame = heap_caps_malloc(frame_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (frame == NULL) {
        frame = heap_caps_malloc(frame_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (frame == NULL) {
        printf("screen: error alloc failed bytes=%u\n", (unsigned)frame_bytes);
        fflush(stdout);
        return;
    }

    if (render_face) {
        const faculty175_face_desc_t *face = faculty175_faces_current();
        const uint32_t anim_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        if (face == NULL || !faculty175_face_dispatch_draw(face->id, anim_ms)) {
            free(frame);
            printf("screen: error face render failed\n");
            fflush(stdout);
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    faculty175_display_lock();
    const bool copied = faculty175_display_frame_copy(frame, pixels);
    faculty175_display_unlock();
    if (!copied) {
        free(frame);
        printf("screen: error frame copy failed\n");
        fflush(stdout);
        return;
    }

    const uint32_t row_stride = (((uint32_t)FACULTY175_LCD_W * 24u + 31u) / 32u) * 4u;
    const size_t bytes = 54u + (size_t)row_stride * (size_t)FACULTY175_LCD_H;
    const esp_log_level_t prev = esp_log_level_get("*");
    const esp_log_level_t prev_wdt = esp_log_level_get("task_wdt");
    esp_log_level_set("*", ESP_LOG_NONE);
    esp_log_level_set("task_wdt", ESP_LOG_NONE);

    printf("screen: BEGIN w=%d h=%d bytes=%u format=bmp24 snapshot=yes\n",
           FACULTY175_LCD_W,
           FACULTY175_LCD_H,
           (unsigned)bytes);
    fflush(stdout);

#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG_ENABLED
    usb_serial_jtag_vfs_set_tx_line_endings(ESP_LINE_ENDINGS_LF);
#endif
    const esp_err_t write_err = write_snapshot_bmp24(frame, FACULTY175_LCD_W, FACULTY175_LCD_H, serial_bmp_write_cb, stdout);
    fflush(stdout);
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG_ENABLED
    usb_serial_jtag_vfs_set_tx_line_endings(ESP_LINE_ENDINGS_CRLF);
#endif
    free(frame);

    esp_log_level_set("task_wdt", prev_wdt);
    esp_log_level_set("*", prev);

    if (write_err != ESP_OK) {
        printf("screen: error bmp write failed (%s)\n", esp_err_to_name(write_err));
    } else {
        printf("screen: END\n");
    }
    fflush(stdout);

    esp_log_level_set("*", prev);
}

static void emit_screen_bmp(void)
{
    emit_screen_bmp_snapshot(false);
}

static void emit_face_screen_bmp(void)
{
    emit_screen_bmp_snapshot(false);
}

static void write_b64_block(const uint8_t *data, size_t len)
{
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t col = 0;
    for (size_t i = 0; i < len; i += 3) {
        const uint32_t b0 = data[i];
        const uint32_t b1 = i + 1 < len ? data[i + 1] : 0;
        const uint32_t b2 = i + 2 < len ? data[i + 2] : 0;
        const uint32_t v = (b0 << 16) | (b1 << 8) | b2;
        fputc(table[(v >> 18) & 0x3f], stdout);
        fputc(table[(v >> 12) & 0x3f], stdout);
        fputc(i + 1 < len ? table[(v >> 6) & 0x3f] : '=', stdout);
        fputc(i + 2 < len ? table[v & 0x3f] : '=', stdout);
        col += 4;
        if (col >= 76) {
            fputc('\n', stdout);
            fflush(stdout);
            (void)esp_task_wdt_reset();
            vTaskDelay(pdMS_TO_TICKS(3));
            col = 0;
        }
    }
    if (col != 0) {
        fputc('\n', stdout);
        fflush(stdout);
        (void)esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(3));
    }
}

static void emit_voice_pcm_b64(void)
{
    static const char path[] = "/voice/voice-turn.pcm";
    FILE *in = fopen(path, "rb");
    if (in == NULL) {
        printf("voice-pcm: error open %s\n", path);
        fflush(stdout);
        return;
    }

    if (fseek(in, 0, SEEK_END) != 0) {
        fclose(in);
        printf("voice-pcm: error size\n");
        fflush(stdout);
        return;
    }
    const long file_size = ftell(in);
    if (file_size <= 0 || fseek(in, 0, SEEK_SET) != 0) {
        fclose(in);
        printf("voice-pcm: error empty\n");
        fflush(stdout);
        return;
    }

    const esp_log_level_t prev = esp_log_level_get("*");
    const esp_log_level_t prev_wdt = esp_log_level_get("task_wdt");
    esp_log_level_set("*", ESP_LOG_NONE);
    esp_log_level_set("task_wdt", ESP_LOG_NONE);
    printf("voice-pcm: BEGIN rate=16000 channels=1 format=s16le bytes=%ld encoding=base64\n", file_size);

    /* A multiple of three keeps Base64 padding confined to the final block. */
    uint8_t chunk[768];
    size_t total = 0;
    while (total < (size_t)file_size) {
        const size_t got = fread(chunk, 1, sizeof(chunk), in);
        if (got == 0) {
            break;
        }
        write_b64_block(chunk, got);
        total += got;
    }
    fclose(in);
    printf("voice-pcm: END bytes=%u status=%s\n",
           (unsigned)total,
           total == (size_t)file_size ? "ESP_OK" : "ESP_FAIL");
    fflush(stdout);
    esp_log_level_set("task_wdt", prev_wdt);
    esp_log_level_set("*", prev);
}

static void emit_face_raw565_b64(void)
{
    const size_t pixels = faculty175_display_frame_pixel_count();
    const size_t bytes = pixels * sizeof(uint16_t);
    uint16_t *frame = (uint16_t *)malloc(bytes);
    if (frame == NULL) {
        printf("raw565: error alloc failed bytes=%u\n", (unsigned)bytes);
        fflush(stdout);
        return;
    }
    faculty175_display_lock();
    const bool copied = faculty175_display_frame_copy(frame, pixels);
    faculty175_display_unlock();
    if (!copied) {
        free(frame);
        printf("raw565: error frame copy failed\n");
        fflush(stdout);
        return;
    }

    const esp_log_level_t prev = esp_log_level_get("*");
    const esp_log_level_t prev_wdt = esp_log_level_get("task_wdt");
    esp_log_level_set("*", ESP_LOG_NONE);
    esp_log_level_set("task_wdt", ESP_LOG_NONE);
    printf("raw565: BEGIN w=%d h=%d bytes=%u encoding=base64\n", FACULTY175_LCD_W, FACULTY175_LCD_H, (unsigned)bytes);
    write_b64_block((const uint8_t *)frame, bytes);
    printf("raw565: END\n");
    fflush(stdout);
    esp_log_level_set("task_wdt", prev_wdt);
    esp_log_level_set("*", prev);
    free(frame);
}

static bool handle_touch_command(const char *line)
{
    if (line == NULL || (strcasecmp(line, "touch") != 0 && strncasecmp(line, "touch ", 6) != 0)) {
        return false;
    }

    const char *sub = strchr(line, ' ');
    sub = sub != NULL ? sub + 1 : "status";
    while (*sub == ' ') {
        ++sub;
    }

    if (*sub == '\0' || strcasecmp(sub, "status") == 0) {
        printf("touch: ready=%s int=%s\n",
               faculty175_touch_ready() ? "yes" : "no",
               faculty175_touch_int_active() ? "active" : "idle");
        fflush(stdout);
        return true;
    }

    char cmd[16] = {};
    unsigned duration_ms = 3000;
    (void)sscanf(sub, "%15s %u", cmd, &duration_ms);
    if (strcasecmp(cmd, "sample") == 0) {
        if (duration_ms < 250) {
            duration_ms = 250;
        } else if (duration_ms > 10000) {
            duration_ms = 10000;
        }
        printf("touch: sample begin %u ms\n", duration_ms);
        fflush(stdout);
        const TickType_t start = xTaskGetTickCount();
        const TickType_t until = start + pdMS_TO_TICKS(duration_ms);
        unsigned hits = 0;
        while ((int32_t)(until - xTaskGetTickCount()) > 0) {
            int16_t xs[1] = {};
            int16_t ys[1] = {};
            const uint8_t n = faculty175_touch_sample(xs, ys, 1);
            if (n > 0) {
                ++hits;
                printf("touch: x=%d y=%d int=%s\n",
                       (int)xs[0],
                       (int)ys[0],
                       faculty175_touch_int_active() ? "active" : "idle");
                fflush(stdout);
            }
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        printf("touch: sample end hits=%u\n", hits);
        fflush(stdout);
        return true;
    }

    printf("touch commands:\n");
    printf("  touch status\n");
    printf("  touch sample [ms]\n");
    fflush(stdout);
    return true;
}

static bool handle_i2c_command(const char *line)
{
    if (line == NULL || (strcasecmp(line, "i2c scan") != 0 && strcasecmp(line, "i2c lines") != 0 &&
                         strcasecmp(line, "i2c drive") != 0 && strcasecmp(line, "i2c try") != 0)) {
        return false;
    }

    if (strcasecmp(line, "i2c lines") == 0) {
        printf("i2c: lines sda15=%d scl14=%d tp_int11=%d tp_rst2=%d gpio10=%d gpio41=%d gpio42=%d\n",
               gpio_get_level(GPIO_NUM_15),
               gpio_get_level(GPIO_NUM_14),
               gpio_get_level(GPIO_NUM_11),
               gpio_get_level(GPIO_NUM_2),
               gpio_get_level(GPIO_NUM_10),
               gpio_get_level(GPIO_NUM_41),
               gpio_get_level(GPIO_NUM_42));
        fflush(stdout);
        return true;
    }

    if (strcasecmp(line, "i2c drive") == 0) {
        printf("i2c: drive input sda15=%d scl14=%d\n", gpio_get_level(GPIO_NUM_15), gpio_get_level(GPIO_NUM_14));
        const gpio_config_t out = {
            .pin_bit_mask = (1ULL << GPIO_NUM_15) | (1ULL << GPIO_NUM_14),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        (void)gpio_config(&out);
        (void)gpio_set_level(GPIO_NUM_15, 1);
        (void)gpio_set_level(GPIO_NUM_14, 1);
        vTaskDelay(pdMS_TO_TICKS(20));
        printf("i2c: drive high sda15=%d scl14=%d\n", gpio_get_level(GPIO_NUM_15), gpio_get_level(GPIO_NUM_14));
        (void)gpio_set_level(GPIO_NUM_15, 0);
        (void)gpio_set_level(GPIO_NUM_14, 0);
        vTaskDelay(pdMS_TO_TICKS(20));
        printf("i2c: drive low sda15=%d scl14=%d\n", gpio_get_level(GPIO_NUM_15), gpio_get_level(GPIO_NUM_14));
        const gpio_config_t in = {
            .pin_bit_mask = (1ULL << GPIO_NUM_15) | (1ULL << GPIO_NUM_14),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        (void)gpio_config(&in);
        vTaskDelay(pdMS_TO_TICKS(20));
        printf("i2c: drive release sda15=%d scl14=%d\n", gpio_get_level(GPIO_NUM_15), gpio_get_level(GPIO_NUM_14));
        fflush(stdout);
        return true;
    }

    if (strcasecmp(line, "i2c try") == 0) {
        const struct {
            gpio_num_t sda;
            gpio_num_t scl;
        } pairs[] = {
            {GPIO_NUM_15, GPIO_NUM_14},
            {GPIO_NUM_14, GPIO_NUM_15},
            {GPIO_NUM_10, GPIO_NUM_11},
            {GPIO_NUM_11, GPIO_NUM_10},
            {GPIO_NUM_41, GPIO_NUM_42},
            {GPIO_NUM_42, GPIO_NUM_41},
            {GPIO_NUM_17, GPIO_NUM_18},
            {GPIO_NUM_18, GPIO_NUM_17},
        };
        printf("i2c: try begin\n");
        for (size_t i = 0; i < sizeof(pairs) / sizeof(pairs[0]); ++i) {
            i2c_master_bus_handle_t try_bus = NULL;
            const i2c_master_bus_config_t cfg = {
                .i2c_port = I2C_NUM_1,
                .sda_io_num = pairs[i].sda,
                .scl_io_num = pairs[i].scl,
                .clk_source = I2C_CLK_SRC_DEFAULT,
                .glitch_ignore_cnt = 7,
                .flags = {
                    .enable_internal_pullup = true,
                },
            };
            if (i2c_new_master_bus(&cfg, &try_bus) != ESP_OK || try_bus == NULL) {
                printf("i2c: try sda=%d scl=%d bus-fail\n", (int)pairs[i].sda, (int)pairs[i].scl);
                continue;
            }
            unsigned found = 0;
            for (uint8_t addr = 0x08; addr < 0x78; ++addr) {
                if (i2c_master_probe(try_bus, addr, 12) == ESP_OK) {
                    printf("i2c: try sda=%d scl=%d addr=0x%02x\n", (int)pairs[i].sda, (int)pairs[i].scl, addr);
                    ++found;
                }
                vTaskDelay(pdMS_TO_TICKS(1));
            }
            printf("i2c: try sda=%d scl=%d found=%u\n", (int)pairs[i].sda, (int)pairs[i].scl, found);
            (void)i2c_del_master_bus(try_bus);
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        printf("i2c: try end\n");
        fflush(stdout);
        return true;
    }

    i2c_master_bus_handle_t bus = faculty175_i2c_bus();
    if (bus == NULL) {
        printf("i2c: bus unavailable\n");
        fflush(stdout);
        return true;
    }

    printf("i2c: scan begin\n");
    unsigned found = 0;
    for (uint8_t addr = 0x08; addr < 0x78; ++addr) {
        if (i2c_master_probe(bus, addr, 20) == ESP_OK) {
            printf("i2c: addr=0x%02x\n", addr);
            ++found;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    printf("i2c: scan end found=%u\n", found);
    fflush(stdout);
    return true;
}

static bool handle_power_command(const char *line)
{
    if (line == NULL || (strcasecmp(line, "power") != 0 && strncasecmp(line, "power ", 6) != 0 &&
                         strcasecmp(line, "battery") != 0 && strncasecmp(line, "battery ", 8) != 0)) {
        return false;
    }

    const char *args = line + (strncasecmp(line, "battery", 7) == 0 ? 7 : 5);
    char sub[24] = {0};
    char arg[24] = {0};
    char arg2[24] = {0};
    args = parse_serial_arg(args, sub, sizeof(sub));
    args = parse_serial_arg(args, arg, sizeof(arg));
    (void)parse_serial_arg(args, arg2, sizeof(arg2));

    if (strcasecmp(sub, "deep-sleep") == 0) {
        if (strcasecmp(arg, "cancel") == 0) {
            faculty175_deep_sleep_cancel();
            printf("power: deep-sleep cancelled\n");
        } else if (arg[0] != '\0' && strcasecmp(arg, "status") != 0) {
            char *end = NULL;
            const unsigned long minutes = strtoul(arg, &end, 10);
            if (end == arg || *end != '\0' || minutes < 1u || minutes > 7u * 24u * 60u ||
                !faculty175_deep_sleep_request((uint32_t)minutes * 60u)) {
                printf("power: deep-sleep duration must be 1..10080 minutes\n");
            } else {
                printf("power: deep-sleep=%lu min armed; entry waits for battery/VBUS removal\n",
                       minutes);
            }
        } else {
            faculty175_deep_sleep_status_t sleep = {0};
            faculty175_deep_sleep_status(&sleep);
            printf("power: deep-sleep pending=%s retained=%s completed=%s requested_s=%lu "
                   "start_pct=%d start_mv=%u start_epoch=%lu prepare_flags=0x%02x wake_cause=%d\n",
                   sleep.pending ? "yes" : "no",
                   sleep.retained ? "yes" : "no",
                   sleep.completed ? "yes" : "no",
                   (unsigned long)sleep.requested_sleep_s,
                   sleep.start_battery_percent,
                   (unsigned)sleep.start_battery_mv,
                   (unsigned long)sleep.started_epoch_s,
                   (unsigned)sleep.prepare_flags,
                   sleep.wake_cause);
        }
        fflush(stdout);
        return true;
    }

    if (strcasecmp(sub, "scenario") == 0 && arg[0] != '\0' &&
        strcasecmp(arg, "status") != 0) {
        faculty175_power_scenario_t scenario;
        if (!faculty175_power_scenario_parse(arg, &scenario)) {
            printf("power: scenarios: normal full-wifi full-offline dim-wifi dim-offline off-wifi sleep-offline\n");
            fflush(stdout);
            return true;
        }
        uint32_t minutes = 24u * 60u;
        if (scenario == FACULTY175_POWER_SCENARIO_NORMAL) {
            minutes = 0u;
        } else if (arg2[0] != '\0') {
            char *end = NULL;
            const unsigned long parsed = strtoul(arg2, &end, 10);
            if (end == arg2 || *end != '\0' || parsed < 1u || parsed > 7u * 24u * 60u) {
                printf("power: scenario duration must be 1..10080 minutes\n");
                fflush(stdout);
                return true;
            }
            minutes = (uint32_t)parsed;
        }
        const uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        if (!faculty175_power_scenario_set(scenario, now_ms, minutes * 60000u)) {
            printf("power: unable to set scenario\n");
        } else {
            printf("power: scenario=%s duration_min=%lu armed; applies while on battery\n",
                   faculty175_power_scenario_name(scenario),
                   (unsigned long)minutes);
        }
        fflush(stdout);
        return true;
    }

    if (strcasecmp(sub, "stream") == 0) {
        float hz = 0.2f;
        if (arg[0] != '\0' && strcasecmp(arg, "on") != 0) {
            if (strcasecmp(arg, "off") == 0 || strcmp(arg, "0") == 0) {
                hz = 0.0f;
            } else {
                char *end = NULL;
                hz = strtof(arg, &end);
                if (end == arg || end == NULL || *end != '\0') {
                    printf("power: usage: power stream [off|0.1..5]\n");
                    fflush(stdout);
                    return true;
                }
            }
        }
        if (!faculty175_power_metrics_stream_set(hz)) {
            printf("power: stream rate must be between 0 and 5 Hz\n");
        } else if (hz == 0.0f) {
            printf("power: stream off\n");
        } else {
            printf("power: stream %.1f Hz\n", (double)hz);
        }
        fflush(stdout);
        return true;
    }

    faculty175_power_metrics_t metrics = {0};
    faculty175_power_metrics_status(&metrics);
    const faculty175_pmu_status_t *st = &metrics.pmu;
    if (!st->present) {
        printf("power: PMU unavailable\n");
        fflush(stdout);
        return true;
    }
    const bool on_battery = st->battery_present && !st->vbus_in && !st->charging;
    const bool docked = st->vbus_in || st->charging;
    const esp_app_desc_t *app = esp_app_get_description();
    printf("power: firmware=%s reset_reason=%d pmu_on=0x%02x pmu_off=0x%02x source=%s docked=%s battery=%s percent=%d mv=%u vbus=%s charging=%s discharging=%s power_savings=%s mode=%s wifi=%s ble=%s scenario=%s scenario_elapsed_s=%lu scenario_remaining_s=%lu uptime_s=%lu awake_s=%lu breathing_s=%lu dimmed_s=%lu asleep_s=%lu scenario_battery_s=%lu scenario_awake_s=%lu scenario_breathing_s=%lu scenario_dimmed_s=%lu scenario_asleep_s=%lu scenario_wifi_s=%lu scenario_ble_s=%lu discharge_drop=%d discharge_elapsed_s=%lu rate_pct_h=%.3f estimate=%s stream_hz=%.1f\n",
           app != NULL ? app->version : "unknown",
           (int)esp_reset_reason(),
           (unsigned)st->power_on_source_flags,
           (unsigned)st->power_off_source_flags,
           on_battery ? "battery" : (docked ? "dock" : "external"),
           docked ? "yes" : "no",
           st->battery_present ? "present" : "absent",
           st->battery_percent,
           (unsigned)st->battery_mv,
           st->vbus_in ? "yes" : "no",
           st->charging ? "yes" : "no",
           st->discharging ? "yes" : "no",
           on_battery ? "enabled" : "disabled",
           faculty175_power_mode_name(metrics.mode),
           metrics.wifi_active ? "on" : "off",
           metrics.ble_active ? "on" : "off",
           faculty175_power_scenario_name(metrics.scenario),
           (unsigned long)(metrics.scenario_elapsed_ms / 1000u),
           (unsigned long)(metrics.scenario_remaining_ms / 1000u),
           (unsigned long)(metrics.uptime_ms / 1000u),
           (unsigned long)(metrics.awake_ms / 1000u),
           (unsigned long)(metrics.breathing_ms / 1000u),
           (unsigned long)(metrics.dimmed_ms / 1000u),
           (unsigned long)(metrics.asleep_ms / 1000u),
           (unsigned long)(metrics.scenario_battery_ms / 1000u),
           (unsigned long)(metrics.scenario_awake_ms / 1000u),
           (unsigned long)(metrics.scenario_breathing_ms / 1000u),
           (unsigned long)(metrics.scenario_dimmed_ms / 1000u),
           (unsigned long)(metrics.scenario_asleep_ms / 1000u),
           (unsigned long)(metrics.scenario_wifi_ms / 1000u),
           (unsigned long)(metrics.scenario_ble_ms / 1000u),
           metrics.discharge_drop_percent,
           (unsigned long)(metrics.discharge_elapsed_ms / 1000u),
           (double)metrics.discharge_percent_per_hour,
           metrics.estimate_valid ? "valid" : "learning",
           (double)metrics.stream_hz);
    if (metrics.estimate_valid) {
        printf("power: estimated remaining %.2f hours at observed usage mix\n", (double)metrics.remaining_hours);
    } else if (on_battery) {
        printf("power: runtime estimate learning; needs at least 15 minutes and a 1%% battery drop\n");
    }
    fflush(stdout);
    return true;
}

static bool handle_audio_command(const char *line)
{
    if (line == NULL || (strcasecmp(line, "audio") != 0 && strncasecmp(line, "audio ", 6) != 0)) {
        return false;
    }

    char sub[24] = {};
    char arg[24] = {};
    const char *args = line + 5;
    args = parse_serial_arg(args, sub, sizeof(sub));
    (void)parse_serial_arg(args, arg, sizeof(arg));

    if (sub[0] == '\0' || strcasecmp(sub, "status") == 0 || strcasecmp(sub, "ns") == 0 ||
        strcasecmp(sub, "noise") == 0) {
        if (arg[0] != '\0') {
            if (strcasecmp(arg, "on") == 0 || strcasecmp(arg, "enable") == 0 ||
                strcasecmp(arg, "enabled") == 0 || strcmp(arg, "1") == 0) {
                faculty175_audio_noise_suppression_set_enabled(true);
            } else if (strcasecmp(arg, "off") == 0 || strcasecmp(arg, "disable") == 0 ||
                       strcasecmp(arg, "disabled") == 0 || strcmp(arg, "0") == 0) {
                faculty175_audio_noise_suppression_set_enabled(false);
            } else if (strcasecmp(arg, "reset") == 0) {
                faculty175_audio_noise_suppression_reset();
            } else if (strcasecmp(sub, "ns") == 0 || strcasecmp(sub, "noise") == 0) {
                printf("audio: usage audio ns [on|off|reset]\n");
                fflush(stdout);
                return true;
            }
        }
        faculty175_audio_noise_status_t st = {};
        faculty175_audio_noise_suppression_status(&st);
        printf("audio: ns=%s floor_rms=%u last_rms=%u gain=%.2f frames=%u\n",
               st.enabled ? "on" : "off",
               (unsigned)st.noise_rms,
               (unsigned)st.last_rms,
               (double)st.last_gain_q8 / 256.0,
               (unsigned)st.frames);
        fflush(stdout);
        return true;
    }

    if (strcasecmp(sub, "help") == 0) {
        printf("audio commands:\n");
        printf("  audio status\n");
        printf("  audio ns [on|off|reset]\n");
        fflush(stdout);
        return true;
    }

    printf("audio: unknown command (try: audio help)\n");
    fflush(stdout);
    return true;
}

static bool parse_gesture_kind(const char *sub, faculty175_gesture_kind_t *out_kind, int16_t *out_value)
{
    char a[24] = {};
    char b[24] = {};
    int value = 0;
    (void)sscanf(sub, "%23s %23s %d", a, b, &value);
    if (a[0] == '\0') {
        return false;
    }

    if (strcasecmp(a, "swipe") == 0) {
        if (strcasecmp(b, "left") == 0) {
            *out_kind = FACULTY175_GESTURE_SWIPE_LEFT;
            return true;
        }
        if (strcasecmp(b, "right") == 0) {
            *out_kind = FACULTY175_GESTURE_SWIPE_RIGHT;
            return true;
        }
        if (strcasecmp(b, "up") == 0) {
            *out_kind = FACULTY175_GESTURE_SWIPE_UP;
            return true;
        }
        if (strcasecmp(b, "down") == 0) {
            *out_kind = FACULTY175_GESTURE_SWIPE_DOWN;
            return true;
        }
        return false;
    }

    if (strcasecmp(a, "left") == 0 || strcasecmp(a, "swipe-left") == 0) {
        *out_kind = FACULTY175_GESTURE_SWIPE_LEFT;
        return true;
    }
    if (strcasecmp(a, "right") == 0 || strcasecmp(a, "swipe-right") == 0) {
        *out_kind = FACULTY175_GESTURE_SWIPE_RIGHT;
        return true;
    }
    if (strcasecmp(a, "up") == 0 || strcasecmp(a, "swipe-up") == 0) {
        *out_kind = FACULTY175_GESTURE_SWIPE_UP;
        return true;
    }
    if (strcasecmp(a, "down") == 0 || strcasecmp(a, "swipe-down") == 0) {
        *out_kind = FACULTY175_GESTURE_SWIPE_DOWN;
        return true;
    }
    if (strcasecmp(a, "tap") == 0) {
        *out_kind = FACULTY175_GESTURE_TAP;
        return true;
    }
    if (strcasecmp(a, "long") == 0 || strcasecmp(a, "longtap") == 0 ||
        strcasecmp(a, "long-tap") == 0) {
        *out_kind = FACULTY175_GESTURE_LONG_TAP;
        return true;
    }
    if (strcasecmp(a, "cw") == 0 || strcasecmp(a, "rotate-cw") == 0) {
        *out_kind = FACULTY175_GESTURE_BEZEL_ROTATE_CW;
        *out_value = 1;
        return true;
    }
    if (strcasecmp(a, "ccw") == 0 || strcasecmp(a, "rotate-ccw") == 0) {
        *out_kind = FACULTY175_GESTURE_BEZEL_ROTATE_CCW;
        *out_value = -1;
        return true;
    }
    if (strcasecmp(a, "bezel") == 0) {
        if (strcasecmp(b, "tap") == 0) {
            *out_kind = FACULTY175_GESTURE_BEZEL_TAP;
            *out_value = (int16_t)value;
            return true;
        }
        if (strcasecmp(b, "cw") == 0 || strcasecmp(b, "rotate-cw") == 0) {
            *out_kind = FACULTY175_GESTURE_BEZEL_ROTATE_CW;
            *out_value = 1;
            return true;
        }
        if (strcasecmp(b, "ccw") == 0 || strcasecmp(b, "rotate-ccw") == 0) {
            *out_kind = FACULTY175_GESTURE_BEZEL_ROTATE_CCW;
            *out_value = -1;
            return true;
        }
    }
    return false;
}

static bool handle_gesture_command(const char *line)
{
    if (line == NULL || (strcasecmp(line, "gesture") != 0 && strncasecmp(line, "gesture ", 8) != 0 &&
                         strcasecmp(line, "gestures") != 0 && strncasecmp(line, "gestures ", 9) != 0)) {
        return false;
    }

    const char *sub = strchr(line, ' ');
    sub = sub != NULL ? sub + 1 : "help";
    while (*sub == ' ') {
        ++sub;
    }

    if (*sub == '\0' || strcasecmp(sub, "help") == 0) {
        printf("gesture commands:\n");
        printf("  gesture tap | long | swipe left|right|up|down\n");
        printf("  gesture bezel tap [index] | bezel cw | bezel ccw\n");
        fflush(stdout);
        return true;
    }

    faculty175_gesture_kind_t kind = FACULTY175_GESTURE_NONE;
    int16_t value = 0;
    if (!parse_gesture_kind(sub, &kind, &value)) {
        printf("gesture: error unknown '%s'\n", sub);
        fflush(stdout);
        return true;
    }

    const bool ok = faculty175_gesture_inject(kind, FACULTY175_LCD_W / 2, FACULTY175_LCD_H / 2, value);
    printf("gesture: inject %s\n", ok ? "ESP_OK" : "ESP_FAIL");
    fflush(stdout);
    return true;
}

static bool handle_button_command(const char *line)
{
    if (line == NULL || (strcasecmp(line, "button") != 0 && strncasecmp(line, "button ", 7) != 0)) {
        return false;
    }
    const char *sub = strchr(line, ' ');
    sub = sub != NULL ? sub + 1 : "press";
    while (*sub == ' ') {
        ++sub;
    }
    if (*sub == '\0' || strcasecmp(sub, "press") == 0 || strcasecmp(sub, "tap") == 0) {
        faculty175_button_inject_press();
        printf("button: inject ESP_OK\n");
    } else {
        printf("button commands:\n");
        printf("  button press\n");
    }
    fflush(stdout);
    return true;
}

static bool handle_tts_command(const char *line)
{
    if (line == NULL || (strcasecmp(line, "tts") != 0 && strncasecmp(line, "tts ", 4) != 0 &&
                         strcasecmp(line, "voice") != 0 && strncasecmp(line, "voice ", 6) != 0)) {
        return false;
    }
    const char *sub = strchr(line, ' ');
    sub = sub != NULL ? sub + 1 : "face";
    while (*sub == ' ') {
        ++sub;
    }
    if (strncasecmp(line, "voice", 5) == 0 && (strcasecmp(sub, "tts") == 0 || strncasecmp(sub, "tts ", 4) == 0)) {
        sub += 3;
        while (*sub == ' ') {
            ++sub;
        }
        if (*sub == '\0') {
            sub = "face";
        }
    }
    if (strcasecmp(sub, "status") == 0) {
        char reason[128];
        const bool ready = faculty175_voice_config_ready(reason, sizeof(reason));
        printf("tts: status ready=%s playback=%s reason=%s\n",
               ready ? "yes" : "no",
               faculty175_voice_tts_playback_busy() ? "busy" : "idle",
               reason);
    } else if (*sub == '\0' || strcasecmp(sub, "face") == 0 || strcasecmp(sub, "read") == 0) {
        const bool ok = faculty175_request_current_face_tts();
        printf("tts: face %s\n", ok ? "ESP_OK" : "ESP_FAIL");
    } else if (strcasecmp(sub, "stt") == 0 || strncasecmp(sub, "stt ", 4) == 0) {
        const char *ms_arg = sub + 3;
        while (*ms_arg == ' ') {
            ++ms_arg;
        }
        unsigned capture_ms = 9000;
        if (*ms_arg != '\0') {
            capture_ms = (unsigned)strtoul(ms_arg, NULL, 10);
        }
        if (capture_ms < 1000) {
            capture_ms = 1000;
        } else if (capture_ms > 30000) {
            capture_ms = 30000;
        }
        const esp_err_t err = faculty175_request_qa_stt(capture_ms);
        printf("stt: capture_ms=%u %s\n", capture_ms, esp_err_to_name(err));
    } else if (strcasecmp(sub, "pcm") == 0) {
        emit_voice_pcm_b64();
    } else {
        printf("voice commands:\n");
        printf("  tts status\n");
        printf("  tts face\n");
        printf("  voice tts\n");
        printf("  voice stt [ms]\n");
        printf("  voice pcm  (USB-only Base64 export of the last STT capture)\n");
    }
    fflush(stdout);
    return true;
}

static bool handle_family_command(const char *line)
{
    if (line == NULL || (strcasecmp(line, "family") != 0 && strncasecmp(line, "family ", 7) != 0 &&
                         strcasecmp(line, "wellness") != 0 && strncasecmp(line, "wellness ", 9) != 0 &&
                         strcasecmp(line, "synastry wellness") != 0)) {
        return false;
    }
    const char *sub = strchr(line, ' ');
    sub = sub != NULL ? sub + 1 : "status";
    while (*sub == ' ') {
        ++sub;
    }
    if (*sub != '\0' && strcasecmp(sub, "status") != 0 && strcasecmp(sub, "wellness") != 0) {
        printf("family commands:\n");
        printf("  family status\n");
        printf("  wellness\n");
        fflush(stdout);
        return true;
    }

    faculty175_family_wellness_t states[FACULTY175_FAMILY_SUBJECT_MAX] = {};
    const size_t count = faculty175_family_snapshot(states, FACULTY175_FAMILY_SUBJECT_MAX);
    printf("family: espnow=%s channel=%d subjects=%u\n",
           faculty175_family_ready() ? "ready" : "waiting",
           faculty175_family_channel(),
           (unsigned)count);
    if (count == 0) {
        char summary[128];
        faculty175_family_format_summary(summary, sizeof(summary));
        printf("family: %s\n", summary);
    }
    for (size_t i = 0; i < count; ++i) {
        const faculty175_family_wellness_t *s = &states[i];
        printf("family: subject=%u name=\"%s\" cue=%s score=%u age=%lums seq=%lu src=%02x:%02x:%02x:%02x:%02x:%02x flags=0x%02x stress=%u trend=%+d hrv=%u hr=%u spo2=%u sleep=%u light=%u deep=%u rem=%u awake=%u debt=%u batt=%u\n",
               s->subject_id,
               s->subject_name,
               faculty175_family_guidance_cue(s),
               faculty175_family_load_score(s),
               (unsigned long)s->age_ms,
               (unsigned long)s->seq,
               s->source_mac[0],
               s->source_mac[1],
               s->source_mac[2],
               s->source_mac[3],
               s->source_mac[4],
               s->source_mac[5],
               s->flags,
               s->stress,
               s->stress_trend_30m,
               s->hrv_ms,
               s->heart_rate_bpm,
               s->spo2_percent,
               s->sleep_total_min,
               s->sleep_light_min,
               s->sleep_deep_min,
               s->sleep_rem_min,
               s->sleep_awake_min,
               faculty175_family_sleep_debt_min(s),
               s->battery_percent);
    }
    fflush(stdout);
    return true;
}

static bool handle_pipeline_command(const char *line)
{
    if (line == NULL || (strcasecmp(line, "pipeline") != 0 && strncasecmp(line, "pipeline ", 9) != 0 &&
                         strcasecmp(line, "stream") != 0 && strncasecmp(line, "stream ", 7) != 0)) {
        return false;
    }
    const char *sub = strchr(line, ' ');
    sub = sub != NULL ? sub + 1 : "";
    while (*sub == ' ') {
        ++sub;
    }
    if (*sub == '\0' || strcasecmp(sub, "help") == 0) {
        printf("pipeline commands:\n");
        printf("  pipeline capture [ms]  (0 = rolling duplex until silence)\n");
        printf("  pipeline status\n");
        printf("  pipeline stop\n");
        printf("  pipeline restart\n");
        printf("  stream capture\n");
        fflush(stdout);
        return true;
    }
    if (strcasecmp(sub, "status") == 0) {
        bool configured = false;
        bool created = false;
        bool started = false;
        bool starting = false;
        faculty175_streaming_pipeline_status(&configured, &created, &started, &starting);
        printf("pipeline: configured=%s created=%s started=%s starting=%s\n",
               configured ? "yes" : "no",
               created ? "yes" : "no",
               started ? "yes" : "no",
               starting ? "yes" : "no");
        bool speech_active = false;
        bool manual_pending = false;
        bool manual_active = false;
        uint32_t last_rms = 0;
        uint32_t noise_rms = 0;
        uint32_t start_threshold = 0;
        uint32_t capture_bytes = 0;
        uint32_t queued_segments = 0;
        uint32_t turn_segments = 0;
        uint32_t read_ok = 0;
        uint32_t read_zero = 0;
        uint32_t read_err = 0;
        esp_err_t last_read_err = ESP_OK;
        faculty175_streaming_pipeline_diag(&speech_active,
                                           &manual_pending,
                                           &manual_active,
                                           &last_rms,
                                           &noise_rms,
                                           &start_threshold,
                                           &capture_bytes,
                                           &queued_segments,
                                           &turn_segments,
                                           &read_ok,
                                           &read_zero,
                                           &read_err,
                                           &last_read_err);
        printf("pipeline: speech=%s manual_pending=%s manual_active=%s rms=%lu noise=%lu threshold=%lu capture_bytes=%lu queued=%lu turn_segments=%lu read_ok=%lu read_zero=%lu read_err=%lu last_read=%s\n",
               speech_active ? "yes" : "no",
               manual_pending ? "yes" : "no",
               manual_active ? "yes" : "no",
               (unsigned long)last_rms,
               (unsigned long)noise_rms,
               (unsigned long)start_threshold,
               (unsigned long)capture_bytes,
               (unsigned long)queued_segments,
               (unsigned long)turn_segments,
               (unsigned long)read_ok,
               (unsigned long)read_zero,
               (unsigned long)read_err,
               esp_err_to_name(last_read_err));
        fflush(stdout);
        return true;
    }
    if (strcasecmp(sub, "capture") == 0 || strncasecmp(sub, "capture ", 8) == 0 ||
        strcasecmp(sub, "listen") == 0 || strncasecmp(sub, "listen ", 7) == 0 ||
        strcasecmp(sub, "trigger") == 0 || strncasecmp(sub, "trigger ", 8) == 0) {
        uint32_t capture_ms = 2500;
        const char *ms_arg = sub;
        while (*ms_arg != '\0' && *ms_arg != ' ') {
            ++ms_arg;
        }
        while (*ms_arg == ' ') {
            ++ms_arg;
        }
        if (*ms_arg != '\0') {
            capture_ms = (uint32_t)strtoul(ms_arg, NULL, 10);
        }
        if (capture_ms != 0 && capture_ms < 1500) {
            capture_ms = 1500;
        } else if (capture_ms > 15000) {
            capture_ms = 15000;
        }
        const esp_err_t err = faculty175_request_streaming_capture(capture_ms);
        printf("pipeline: capture_ms=%lu %s\n", (unsigned long)capture_ms, esp_err_to_name(err));
        fflush(stdout);
        return true;
    }
    if (strcasecmp(sub, "stop") == 0) {
        const esp_err_t err = faculty175_request_streaming_pipeline_stop();
        printf("pipeline: stop %s\n", esp_err_to_name(err));
        fflush(stdout);
        return true;
    }
    if (strcasecmp(sub, "restart") == 0) {
        const esp_err_t err = faculty175_request_streaming_pipeline_restart();
        printf("pipeline: restart %s\n", esp_err_to_name(err));
        fflush(stdout);
        return true;
    }
    printf("pipeline commands:\n");
    printf("  pipeline capture [ms]  (0 = rolling duplex until silence)\n");
    printf("  pipeline status\n");
    printf("  pipeline stop\n");
    printf("  pipeline restart\n");
    printf("  stream capture\n");
    fflush(stdout);
    return true;
}

static bool handle_stt_command(const char *line)
{
    if (line == NULL || (strcasecmp(line, "stt") != 0 && strncasecmp(line, "stt ", 4) != 0)) {
        return false;
    }
    const char *sub = strchr(line, ' ');
    unsigned capture_ms = 9000;
    if (sub != NULL) {
        while (*sub == ' ') {
            ++sub;
        }
        if (*sub != '\0') {
            capture_ms = (unsigned)strtoul(sub, NULL, 10);
        }
    }
    if (capture_ms < 1000) {
        capture_ms = 1000;
    } else if (capture_ms > 30000) {
        capture_ms = 30000;
    }
    const esp_err_t err = faculty175_request_qa_stt(capture_ms);
    printf("stt: capture_ms=%u %s\n", capture_ms, esp_err_to_name(err));
    fflush(stdout);
    return true;
}

static void print_time_status(void)
{
    astrolabe_time_status_t status = {};
    astrolabe_time_status(&status);
    char utc[32] = {};
    char local[32] = {};
    (void)astrolabe_time_format_utc(utc, sizeof(utc));
    (void)astrolabe_time_format_local(local, sizeof(local));
    printf("time: valid=%s started=%s synced=%s epoch=%lld utc=%s local=%s tz=%s retries=%lu\n",
           astrolabe_time_valid() ? "yes" : "no",
           status.started ? "yes" : "no",
           status.synced ? "yes" : "no",
           (long long)status.epoch,
           utc[0] != '\0' ? utc : "-",
           local[0] != '\0' ? local : "-",
           status.tz[0] != '\0' ? status.tz : "-",
           (unsigned long)status.retry_count);
    fflush(stdout);
}

static bool handle_time_command(const char *line)
{
    if (line == NULL || (strcasecmp(line, "time") != 0 && strncasecmp(line, "time ", 5) != 0)) {
        return false;
    }
    const char *sub = strchr(line, ' ');
    sub = sub != NULL ? sub + 1 : "";
    while (*sub == ' ') {
        ++sub;
    }
    if (*sub == '\0' || strcasecmp(sub, "status") == 0) {
        print_time_status();
        return true;
    }
    if (strcasecmp(sub, "tz") == 0 || strcasecmp(sub, "timezone") == 0) {
        printf("time: tz=%s\n", astrolabe_time_timezone());
        fflush(stdout);
        return true;
    }
    if (strncasecmp(sub, "tz ", 3) == 0 || strncasecmp(sub, "timezone ", 9) == 0) {
        const char *tz = sub[1] == 'z' || sub[1] == 'Z' ? sub + 3 : sub + 9;
        while (*tz == ' ') {
            ++tz;
        }
        const esp_err_t err = astrolabe_time_set_timezone(tz);
        printf("time: set tz=%s %s\n", tz, esp_err_to_name(err));
        fflush(stdout);
        return true;
    }
    if (strncasecmp(sub, "set ", 4) == 0 || strncasecmp(sub, "epoch ", 6) == 0) {
        const char *epoch_str = strncasecmp(sub, "set ", 4) == 0 ? sub + 4 : sub + 6;
        while (*epoch_str == ' ') {
            ++epoch_str;
        }
        char *end = NULL;
        const long long epoch_ll = strtoll(epoch_str, &end, 10);
        while (end != NULL && *end == ' ') {
            ++end;
        }
        const esp_err_t err = (epoch_str[0] != '\0' && end != NULL && *end == '\0')
                                  ? astrolabe_time_set_epoch((time_t)epoch_ll)
                                  : ESP_ERR_INVALID_ARG;
        printf("time: set epoch=%lld %s\n", epoch_ll, esp_err_to_name(err));
        print_time_status();
        return true;
    }
    printf("time commands:\n");
    printf("  time\n");
    printf("  time set <unix_epoch>\n");
    printf("  time tz\n");
    printf("  time tz <POSIX_TZ>\n");
    fflush(stdout);
    return true;
}

static void handle_line(char *line)
{
    trim_inplace(line);
    if (line[0] == '\0') {
        return;
    }
    faculty175_ble_serial_activity();

    if (line_is(line, "face screen") || line_is(line, "faces screen") || line_is(line, "face screen.bmp") ||
        line_is(line, "faces screen.bmp")) {
        emit_face_screen_bmp();
        return;
    }
    if (line_is(line, "face raw565") || line_is(line, "faces raw565") || line_is(line, "face raw565.b64") ||
        line_is(line, "faces raw565.b64")) {
        emit_face_raw565_b64();
        return;
    }

    if (line_is(line, "screen") || line_is(line, "screen.bmp")) {
        emit_screen_bmp();
        return;
    }

    if (line_is(line, "qa screen") || line_is(line, "qa screen.bmp")) {
        emit_screen_bmp();
        return;
    }

    if (handle_time_command(line)) {
        return;
    }

    if (strcasecmp(line, "breath") == 0 || strcasecmp(line, "breath status") == 0) {
        faculty175_breath_status_t status = {};
        faculty175_breath_status(&status);
        printf("breath: state=%s rate_bpm=%.1f confidence=%.2f amplitude_deg=%.4f waveform=%.3f axis=%c pitch_deg=%.4f roll_deg=%.4f signal_deg=%.4f motion_dps=%.3f samples=%lu breaths=%lu calibration_ms=%lu guide=%s phase_ms=%lu cycle=%lu target=%.3f alignment=%.2f stream_hz=%.1f\n",
               faculty175_breath_state_name(status.state),
               (double)status.rate_bpm,
               (double)status.confidence,
               (double)status.amplitude_deg,
               (double)status.waveform,
               status.axis,
               (double)status.pitch_deg,
               (double)status.roll_deg,
               (double)status.signal_deg,
               (double)status.motion_rate_dps,
               (unsigned long)status.samples,
               (unsigned long)status.breaths,
               (unsigned long)status.calibration_ms,
               faculty175_breath_guide_phase_name(status.guide_phase),
               (unsigned long)status.guide_phase_ms,
               (unsigned long)status.guide_cycle,
               (double)status.guide_target,
               (double)status.guide_alignment,
               (double)faculty175_breath_stream_hz());
        fflush(stdout);
        return;
    }
    if (strcasecmp(line, "breath reset") == 0) {
        faculty175_breath_reset();
        printf("breath: reset; open the Iron Man face and remain still for calibration\n");
        fflush(stdout);
        return;
    }
    if (strncasecmp(line, "breath stream", 13) == 0) {
        const char *arg = line + 13;
        while (*arg == ' ') {
            ++arg;
        }
        float hz = 10.0f;
        if (*arg != '\0' && strcasecmp(arg, "on") != 0) {
            if (strcasecmp(arg, "off") == 0 || strcmp(arg, "0") == 0) {
                hz = 0.0f;
            } else {
                char *end = NULL;
                hz = strtof(arg, &end);
                while (end != NULL && *end == ' ') {
                    ++end;
                }
                if (end == arg || end == NULL || *end != '\0') {
                    printf("breath: usage: breath stream [off|1..25]\n");
                    fflush(stdout);
                    return;
                }
            }
        }
        if (!faculty175_breath_stream_set(hz)) {
            printf("breath: stream rate must be between 0 and 25 Hz\n");
        } else if (hz == 0.0f) {
            printf("breath: stream off\n");
        } else {
            printf("breath: stream %.1f Hz\n", (double)hz);
            printf("breath_csv_header,ms,state,pitch_deg,roll_deg,signal_deg,amplitude_deg,waveform,rate_bpm,confidence,axis,motion_rate_dps,samples,breaths,guide_phase,guide_phase_ms,guide_cycle,guide_target,guide_alignment\n");
        }
        fflush(stdout);
        return;
    }

    if (handle_gesture_command(line)) {
        return;
    }

    if (handle_button_command(line)) {
        return;
    }

    if (handle_tts_command(line)) {
        return;
    }

    if (handle_family_command(line)) {
        return;
    }

    if (handle_pipeline_command(line)) {
        return;
    }

    if (handle_stt_command(line)) {
        return;
    }

    if (handle_wifi_command(line)) {
        return;
    }

    if (handle_km_command(line)) {
        return;
    }

    if (faculty175_qa_handle(line)) {
        return;
    }

    if (faculty175_device_auth_handle(line)) {
        return;
    }

    if (faculty175_ota_handle(line)) {
        return;
    }

    if (faculty175_faces_handle(line)) {
        return;
    }

    if (faculty175_pocketwatch_handle(line)) {
        return;
    }

    if (faculty175_charts_handle(line)) {
        return;
    }

    if (faculty175_quotes_handle(line)) {
        return;
    }

    if (faculty175_rocket_handle(line)) {
        return;
    }

    if (faculty175_ble_handle(line)) {
        return;
    }

    if (handle_i2c_command(line)) {
        return;
    }

    if (handle_touch_command(line)) {
        return;
    }

    if (handle_audio_command(line)) {
        return;
    }

    if (handle_power_command(line)) {
        return;
    }

    if (strcasecmp(line, "help") == 0 || strcasecmp(line, "?") == 0) {
        printf("serial: screen | face screen | km help | breath status|reset|stream [hz|off] | gesture help | button press | tts face | stt [ms] | voice stt [ms] | family status | pipeline capture|status|stop|restart | wifi status|scan|set | time | watch status | power | audio status|ns | i2c scan | ble status | qa help | device help | ota help | faces help | charts help | almanac help | quotes help | rocket help | touch status\n");
        (void)faculty175_qa_handle("qa help");
        return;
    }

    ESP_LOGW(TAG, "unknown command: %s (try: screen)", line);
}

static ssize_t serial_read_byte(uint8_t *byte, TickType_t timeout)
{
    if (byte == NULL) {
        return -1;
    }
    (void)timeout;
    return read(STDIN_FILENO, byte, 1);
}

static void receive_screen_jpeg(size_t length)
{
    if (length < 64 || length > FACULTY175_USB_SCREEN_MAX_JPEG) {
        printf("screen: ERROR invalid JPEG length max=%u\n", (unsigned)FACULTY175_USB_SCREEN_MAX_JPEG);
        fflush(stdout);
        return;
    }
    uint8_t *jpeg = heap_caps_malloc(length, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (jpeg == NULL) {
        printf("screen: ERROR no memory\n");
        fflush(stdout);
        return;
    }
    printf("screen: READY bytes=%u\n", (unsigned)length);
    fflush(stdout);
    size_t received = 0;
    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(5000);
    while (received < length && (int32_t)(deadline - xTaskGetTickCount()) > 0) {
        uint8_t byte = 0;
        if (serial_read_byte(&byte, pdMS_TO_TICKS(20)) > 0) {
            jpeg[received++] = byte;
        } else {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
    if (received != length) {
        printf("screen: ERROR timeout received=%u expected=%u\n", (unsigned)received, (unsigned)length);
    } else {
        const esp_err_t err = faculty175_usb_screen_show_jpeg(jpeg, length);
        printf("screen: %s err=%s bytes=%u\n", err == ESP_OK ? "OK" : "ERROR",
               esp_err_to_name(err), (unsigned)length);
    }
    fflush(stdout);
    heap_caps_free(jpeg);
}

static void serial_task(void *arg)
{
    (void)arg;
    char line[320];
    size_t pos = 0;

    ESP_LOGI(TAG, "command reader ready (type: help)");

    for (;;) {
        uint8_t byte = 0;
        const ssize_t n = serial_read_byte(&byte, pdMS_TO_TICKS(20));
        if (n <= 0) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        const int c = (int)byte;
        if (c == '\r' || c == '\n') {
            if (pos == 0) {
                continue;
            }
            line[pos] = '\0';
            pos = 0;
            unsigned jpeg_length = 0;
            if (sscanf(line, "screen put jpeg %u", &jpeg_length) == 1) {
                receive_screen_jpeg(jpeg_length);
            } else if (strcasecmp(line, "screen stop") == 0) {
                faculty175_usb_screen_stop();
                printf("screen: stopped\n");
                fflush(stdout);
            } else {
                handle_line(line);
            }
            continue;
        }
        if (pos + 1 < sizeof(line)) {
            line[pos++] = (char)c;
        }
    }
}

void faculty175_serial_init(void)
{
    if (s_serial_task != NULL) {
        return;
    }
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stdin, NULL, _IONBF, 0);
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG_ENABLED
    usb_serial_jtag_driver_config_t usb_serial_config = {
        .tx_buffer_size = 1024,
        .rx_buffer_size = 1024,
    };
    const esp_err_t usb_serial_err = usb_serial_jtag_driver_install(&usb_serial_config);
    if (usb_serial_err == ESP_OK) {
        usb_serial_jtag_vfs_use_driver();
    } else {
        ESP_LOGW(TAG, "USB Serial/JTAG RX driver unavailable: %s", esp_err_to_name(usb_serial_err));
    }
#endif
    const int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (flags >= 0) {
        (void)fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
    }
    /* Several diagnostics format Wi-Fi and QA snapshots on this task's stack.
       Keep enough headroom for nested NVS calls; 4608 bytes overflowed in
       `wifi status` and corrupted the NVS partition-manager list. */
    xTaskCreate(serial_task, "serial", 8192, NULL, 3, &s_serial_task);
}

TaskHandle_t faculty175_serial_task_handle(void)
{
    return s_serial_task;
}
