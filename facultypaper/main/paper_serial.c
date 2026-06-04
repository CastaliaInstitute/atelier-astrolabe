#include "paper_serial.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "paper_board.h"
#include "paper_memory.h"
#include "paper_ota.h"
#include "paper_qa.h"

static const char *TAG = "paper_serial";
static paper_serial_turn_fn s_turn_fn;
static paper_serial_scroll_fn s_scroll_fn;

void paper_serial_set_turn_callback(paper_serial_turn_fn fn)
{
    s_turn_fn = fn;
}

void paper_serial_set_scroll_callback(paper_serial_scroll_fn fn)
{
    s_scroll_fn = fn;
}

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

static bool line_starts_with_cmd(const char *line, const char *cmd)
{
    if (line == NULL || cmd == NULL) {
        return false;
    }
    const size_t n = strlen(cmd);
    return strncasecmp(line, cmd, n) == 0 && (line[n] == '\0' || isspace((unsigned char)line[n]));
}

static void emit_screen_bmp(void)
{
    const size_t bytes = paper_display_bmp_size();
    if (bytes == 0) {
        printf("screen: error no framebuffer\n");
        fflush(stdout);
        return;
    }

    const esp_log_level_t prev = esp_log_level_get("*");
    esp_log_level_set("*", ESP_LOG_ERROR);

    printf("screen: BEGIN w=%d h=%d bytes=%u\n", PAPER_LCD_W, PAPER_LCD_H, (unsigned)bytes);
    fflush(stdout);

    const int wrote = paper_display_write_bmp(stdout);
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

    if (paper_qa_handle(line)) {
        return;
    }

    if (paper_ota_handle(line)) {
        return;
    }

    if (line_is(line, "buttons") || line_is(line, "button")) {
        const uint8_t mask = paper_button_debug_mask();
        printf("buttons: mask=0x%02x A=%u B=%u C=%u up=%u down=%u\n",
               mask,
               (unsigned)((mask & 0x01u) != 0),
               (unsigned)((mask & 0x02u) != 0),
               (unsigned)((mask & 0x04u) != 0),
               paper_button_up_pressed() ? 1u : 0u,
               paper_button_down_pressed() ? 1u : 0u);
        fflush(stdout);
        return;
    }

    if (line_starts_with_cmd(line, "scroll")) {
        char *arg = line + 6;
        while (*arg != '\0' && isspace((unsigned char)*arg)) {
            ++arg;
        }
        if (s_scroll_fn == NULL) {
            printf("scroll: unavailable\n");
        } else if (strcasecmp(arg, "up") == 0 || strcasecmp(arg, "older") == 0) {
            const esp_err_t err = s_scroll_fn(-1);
            printf("scroll: up %s\n", esp_err_to_name(err));
        } else if (strcasecmp(arg, "down") == 0 || strcasecmp(arg, "newer") == 0) {
            const esp_err_t err = s_scroll_fn(1);
            printf("scroll: down %s\n", esp_err_to_name(err));
        } else if (strcasecmp(arg, "reset") == 0 || strcasecmp(arg, "latest") == 0) {
            const esp_err_t err = s_scroll_fn(0);
            printf("scroll: reset %s\n", esp_err_to_name(err));
        } else {
            printf("scroll: use scroll up | scroll down | scroll reset\n");
        }
        fflush(stdout);
        return;
    }

    if (line_starts_with_cmd(line, "note")) {
        const char *text = line + 4;
        while (*text != '\0' && isspace((unsigned char)*text)) {
            ++text;
        }
        esp_err_t err = paper_memory_append_note("serial", text);
        if (err == ESP_OK) {
            printf("note: saved %s\n", paper_memory_notes_path());
        } else {
            printf("note: save failed %s\n", esp_err_to_name(err));
        }
        fflush(stdout);
        return;
    }

    if (line_starts_with_cmd(line, "turn")) {
        char *text = line + 4;
        while (*text != '\0' && isspace((unsigned char)*text)) {
            ++text;
        }
        char *sep = strstr(text, " | ");
        if (sep == NULL) {
            sep = strchr(text, '|');
        }
        if (sep == NULL || s_turn_fn == NULL) {
            printf("turn: use turn <user> | <faculty>\n");
        } else {
            *sep = '\0';
            char *reply = sep + 1;
            while (*reply != '\0' && (*reply == '|' || isspace((unsigned char)*reply))) {
                ++reply;
            }
            trim_inplace(text);
            trim_inplace(reply);
            const esp_err_t err = s_turn_fn(text, reply);
            if (err == ESP_OK) {
                printf("turn: saved %s\n", paper_memory_conversation_path());
            } else {
                printf("turn: save failed %s\n", esp_err_to_name(err));
            }
        }
        fflush(stdout);
        return;
    }

    if (strcasecmp(line, "help") == 0 || strcasecmp(line, "?") == 0) {
        printf("serial: screen | screen.bmp | scroll up/down/reset | note <text> | turn <user> | <faculty> | qa help | ota help\n");
        (void)paper_qa_handle("qa help");
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

void paper_serial_init(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stdin, NULL, _IONBF, 0);
    xTaskCreate(serial_task, "serial", 4096, NULL, 3, NULL);
}
