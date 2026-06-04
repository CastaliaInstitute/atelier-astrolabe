#include "faculty175_serial.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "esp_log.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "astrolabe_time.h"
#include "faculty175_board.h"
#include "faculty175_almanac.h"
#include "faculty175_ble.h"
#include "faculty175_charts.h"
#include "faculty175_device_auth.h"
#include "faculty175_face_dispatch.h"
#include "faculty175_faces.h"
#include "faculty175_gesture.h"
#include "faculty175_qa.h"
#include "faculty175_ota.h"
#include "faculty175_pmu.h"
#include "faculty175_touch.h"

static const char *TAG = "faculty175_serial";
static bool s_usb_serial_jtag_rx;

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
    printf("time commands:\n");
    printf("  time\n");
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

    if (faculty175_charts_handle(line)) {
        return;
    }

    if (faculty175_almanac_handle(line)) {
        return;
    }

    if (faculty175_ble_handle(line)) {
        return;
    }

    if (handle_touch_command(line)) {
        return;
    }

    if (handle_power_command(line)) {
        return;
    }

    if (strcasecmp(line, "help") == 0 || strcasecmp(line, "?") == 0) {
        printf("serial: screen | face screen | gesture help | button press | time | power | ble status | qa help | device help | ota help | faces help | charts help | almanac help | touch status\n");
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
        const ssize_t n = s_usb_serial_jtag_rx
                              ? usb_serial_jtag_read_bytes(&byte, 1, pdMS_TO_TICKS(20))
                              : read(STDIN_FILENO, &byte, 1);
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
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stdin, NULL, _IONBF, 0);
    const int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (flags >= 0) {
        (void)fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
    }
    xTaskCreate(serial_task, "serial", 8192, NULL, 3, NULL);
}
