#include "atom_serial.h"

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

#include "faculty175_board.h"
#include "atom_device_auth.h"
#include "atom_qa.h"
#include "atom_ota.h"

static const char *TAG = "atom_serial";
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

    esp_log_level_set("*", prev);
}

static void handle_line(char *line)
{
    trim_inplace(line);
    if (line[0] == '\0') {
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

    if (atom_qa_handle(line)) {
        return;
    }

    if (atom_device_auth_handle(line)) {
        return;
    }

    if (atom_ota_handle(line)) {
        return;
    }

    if (strcasecmp(line, "help") == 0 || strcasecmp(line, "?") == 0) {
        printf("serial: screen | screen.bmp | qa help | device help | ota help\n");
        (void)atom_qa_handle("qa help");
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

void atom_serial_init(void)
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
