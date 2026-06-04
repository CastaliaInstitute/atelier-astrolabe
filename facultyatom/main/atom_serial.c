#include "atom_serial.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "astrolabe_time.h"
#include "atom_board.h"
#include "atom_ota.h"
#include "atom_qa.h"

static const char *TAG = "atom_serial";

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
    const size_t bytes = atom_display_bmp_size();
    if (bytes == 0) {
        printf("screen: error no framebuffer\n");
        fflush(stdout);
        return;
    }

    const esp_log_level_t prev = esp_log_level_get("*");
    esp_log_level_set("*", ESP_LOG_ERROR);

    printf("screen: BEGIN w=%d h=%d bytes=%u\n", ATOM_LCD_W, ATOM_LCD_H, (unsigned)bytes);
    fflush(stdout);

    const int wrote = atom_display_write_bmp(stdout);
    fflush(stdout);

    if (wrote < 0 || (size_t)wrote != bytes) {
        printf("screen: error bmp write failed (%d)\n", wrote);
    } else {
        printf("screen: END\n");
    }
    fflush(stdout);

    esp_log_level_set("*", prev);
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

    if (atom_qa_handle(line)) {
        return;
    }

    if (atom_ota_handle(line)) {
        return;
    }

    if (strcasecmp(line, "help") == 0 || strcasecmp(line, "?") == 0) {
        printf("serial: screen | screen.bmp | time | qa help | ota help\n");
        (void)atom_qa_handle("qa help");
        return;
    }

    ESP_LOGW(TAG, "unknown command: %s (try: screen)", line);
}

static void serial_task(void *arg)
{
    (void)arg;
    char line[160];
    size_t pos = 0;

    ESP_LOGI(TAG, "command reader ready (type: help)");

    for (;;) {
        uint8_t byte = 0;
        const ssize_t n = read(STDIN_FILENO, &byte, 1);
        if (n <= 0) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        const int c = (int)byte;
        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
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
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stdin, NULL, _IONBF, 0);
    xTaskCreate(serial_task, "serial", 4096, NULL, 3, NULL);
}
