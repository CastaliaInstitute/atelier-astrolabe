#include "faculty175_serial.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG_ENABLED && !CONFIG_TINYUSB_CDC_ENABLED
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#endif
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "astrolabe_time.h"
#include "faculty175_board.h"
#include "faculty175_ble.h"
#include "faculty175_charts.h"
#include "faculty175_device_auth.h"
#include "faculty175_face_dispatch.h"
#include "faculty175_faces.h"
#include "faculty175_gesture.h"
#include "faculty175_qa.h"
#include "faculty175_ota.h"
#include "faculty175_pocketwatch.h"
#include "faculty175_pmu.h"
#include "faculty175_quotes.h"
#include "faculty175_rocket.h"
#include "faculty175_touch.h"
#include "faculty175_wifi_monitor.h"
#include "faculty175_wifi_settings.h"

static const char *TAG = "faculty175_serial";
static bool s_usb_serial_jtag_rx;
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
        char saved_ssid[FACULTY175_WIFI_SSID_MAX + 1] = {};
        char saved_pass[FACULTY175_WIFI_PASS_MAX + 1] = {};
        const esp_err_t load_err = faculty175_wifi_settings_load(saved_ssid,
                                                                 sizeof(saved_ssid),
                                                                 saved_pass,
                                                                 sizeof(saved_pass));
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
               load_err == ESP_OK ? "yes" : "no",
               load_err == ESP_OK ? saved_ssid : "",
               (load_err == ESP_OK && saved_pass[0] != '\0') ? "set" : "empty");
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
    printf("  wifi incidents | wifi incidents clear\n");
    printf("  wifi router on|off\n");
    printf("  wifi set \"SSID\" \"password\"  (save primary and reboot)\n");
    printf("  wifi add \"SSID\" \"password\"  (save known network)\n");
    printf("  wifi remove \"SSID\"\n");
    printf("  wifi clear\n");
    fflush(stdout);
    return true;
}

static void emit_screen_bmp(void)
{
    const size_t bytes = faculty175_display_bmp_size();
    if (bytes == 0) {
        printf("screen: error no framebuffer\n");
        fflush(stdout);
        return;
    }

    const esp_log_level_t prev = esp_log_level_get("*");
    esp_log_level_set("*", ESP_LOG_ERROR);

    faculty175_display_lock();
    printf("screen: BEGIN w=%d h=%d bytes=%u\n", FACULTY175_LCD_W, FACULTY175_LCD_H, (unsigned)bytes);
    fflush(stdout);

    const int wrote = faculty175_display_write_bmp(stdout);
    fflush(stdout);

    if (wrote < 0 || (size_t)wrote != bytes) {
        printf("screen: error bmp write failed (%d)\n", wrote);
    } else {
        printf("screen: END\n");
    }
    fflush(stdout);
    faculty175_display_unlock();

    esp_log_level_set("*", prev);
}

static void emit_face_screen_bmp(void)
{
    faculty175_display_lock();
    const faculty175_face_desc_t *face = faculty175_faces_current();
    const uint32_t anim_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    if (face == NULL || !faculty175_face_dispatch_draw(face->id, anim_ms)) {
        faculty175_display_unlock();
        printf("screen: error face render failed\n");
        fflush(stdout);
        return;
    }
    emit_screen_bmp();
    faculty175_display_unlock();
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

    faculty175_pmu_status_t st = {
        .battery_percent = -1,
    };
    if (!faculty175_pmu_status(&st)) {
        printf("power: PMU unavailable\n");
        fflush(stdout);
        return true;
    }
    const bool on_battery = st.present && st.battery_present && !st.vbus_in && !st.charging;
    printf("power: source=%s battery=%s percent=%d mv=%u vbus=%s charging=%s discharging=%s\n",
           on_battery ? "battery" : "usb",
           st.battery_present ? "present" : "absent",
           st.battery_percent,
           (unsigned)st.battery_mv,
           st.vbus_in ? "yes" : "no",
           st.charging ? "yes" : "no",
           st.discharging ? "yes" : "no");
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
    if (*sub == '\0' || strcasecmp(sub, "face") == 0 || strcasecmp(sub, "read") == 0) {
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
    } else {
        printf("voice commands:\n");
        printf("  tts face\n");
        printf("  voice tts\n");
        printf("  voice stt [ms]\n");
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
        printf("  pipeline capture [ms]\n");
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
        if (capture_ms < 1500) {
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
    printf("  pipeline capture [ms]\n");
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

    if (line_is(line, "face screen") || line_is(line, "faces screen") || line_is(line, "face screen.bmp") ||
        line_is(line, "faces screen.bmp")) {
        emit_face_screen_bmp();
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

    if (handle_gesture_command(line)) {
        return;
    }

    if (handle_button_command(line)) {
        return;
    }

    if (handle_tts_command(line)) {
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

    if (handle_power_command(line)) {
        return;
    }

    if (strcasecmp(line, "help") == 0 || strcasecmp(line, "?") == 0) {
        printf("serial: screen | face screen | gesture help | button press | tts face | stt [ms] | voice stt [ms] | pipeline capture|status|stop|restart | wifi status|scan|set | time | watch status | power | i2c scan | ble status | qa help | device help | ota help | faces help | charts help | almanac help | quotes help | rocket help | touch status\n");
        (void)faculty175_qa_handle("qa help");
        return;
    }

    ESP_LOGW(TAG, "unknown command: %s (try: screen)", line);
}

static void serial_task(void *arg)
{
    (void)arg;
    char line[320];
    size_t pos = 0;

    ESP_LOGI(TAG, "command reader ready (type: help)");

    for (;;) {
        uint8_t byte = 0;
        ssize_t n = -1;
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG_ENABLED && !CONFIG_TINYUSB_CDC_ENABLED
        if (s_usb_serial_jtag_rx) {
            n = usb_serial_jtag_read_bytes(&byte, 1, pdMS_TO_TICKS(20));
        } else
#endif
        {
            n = read(STDIN_FILENO, &byte, 1);
        }
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
            handle_line(line);
            continue;
        }
        if (pos + 1 < sizeof(line)) {
            line[pos++] = (char)c;
        }
    }
}

void faculty175_serial_init(void)
{
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG_ENABLED && !CONFIG_TINYUSB_CDC_ENABLED
    usb_serial_jtag_driver_config_t usb_cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    const esp_err_t usb_err = usb_serial_jtag_driver_install(&usb_cfg);
    if (usb_err == ESP_OK || usb_err == ESP_ERR_INVALID_STATE) {
        usb_serial_jtag_vfs_set_rx_line_endings(ESP_LINE_ENDINGS_CRLF);
        usb_serial_jtag_vfs_set_tx_line_endings(ESP_LINE_ENDINGS_CRLF);
        usb_serial_jtag_vfs_use_nonblocking();
        s_usb_serial_jtag_rx = true;
    } else {
        ESP_LOGW(TAG, "USB Serial/JTAG driver install failed: %s", esp_err_to_name(usb_err));
    }
#endif
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stdin, NULL, _IONBF, 0);
    const int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (flags >= 0) {
        (void)fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
    }
    xTaskCreate(serial_task, "serial", 6144, NULL, 3, &s_serial_task);
}

TaskHandle_t faculty175_serial_task_handle(void)
{
    return s_serial_task;
}
