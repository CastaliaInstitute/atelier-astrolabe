#include "paper_memory.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "paper_board.h"
#include "paper_util.h"

static const char *TAG = "paper_memory";

#define PAPER_MEMORY_ROOT "/sdcard"
#define PAPER_MEMORY_CONVERSATIONS PAPER_MEMORY_ROOT "/PAPERCON.JSL"
#define PAPER_MEMORY_NOTES PAPER_MEMORY_ROOT "/PAPERNOT.JSL"
#define PAPER_MEMORY_HISTORY PAPER_MEMORY_ROOT "/PAPRHIST.TXT"

static bool s_ready;

static void json_write_escaped(FILE *f, const char *s)
{
    if (s == NULL) {
        return;
    }
    for (const unsigned char *p = (const unsigned char *)s; *p != '\0'; ++p) {
        switch (*p) {
            case '\\':
                fputs("\\\\", f);
                break;
            case '"':
                fputs("\\\"", f);
                break;
            case '\n':
                fputs("\\n", f);
                break;
            case '\r':
                fputs("\\r", f);
                break;
            case '\t':
                fputs("\\t", f);
                break;
            default:
                if (*p < 0x20) {
                    fprintf(f, "\\u%04x", (unsigned)*p);
                } else {
                    fputc((int)*p, f);
                }
                break;
        }
    }
}

static int64_t now_ms(void)
{
    time_t now = 0;
    time(&now);
    if (now > 1700000000) {
        return (int64_t)now * 1000;
    }
    return esp_timer_get_time() / 1000;
}

static void trim_newline(char *s)
{
    if (s == NULL) {
        return;
    }
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r')) {
        s[--len] = '\0';
    }
}

static void append_recent_message(paper_memory_message_t *out, size_t max_messages, size_t *count, bool faculty, const char *text)
{
    if (out == NULL || count == NULL || max_messages == 0 || text == NULL || text[0] == '\0') {
        return;
    }
    if (*count >= max_messages) {
        memmove(out, out + 1, (max_messages - 1u) * sizeof(out[0]));
        *count = max_messages - 1u;
    }
    out[*count].faculty = faculty;
    paper_strlcpy(out[*count].text, text, sizeof(out[*count].text));
    (*count)++;
}

esp_err_t paper_memory_init(void)
{
    s_ready = false;
    if (!paper_board_sd_ready()) {
        ESP_LOGW(TAG, "SD unavailable; conversation/note persistence disabled");
        return ESP_ERR_INVALID_STATE;
    }
    s_ready = true;
    ESP_LOGI(TAG, "conversation storage %s", PAPER_MEMORY_ROOT);
    return ESP_OK;
}

bool paper_memory_ready(void) { return s_ready; }
const char *paper_memory_root(void) { return PAPER_MEMORY_ROOT; }
const char *paper_memory_conversation_path(void) { return PAPER_MEMORY_CONVERSATIONS; }
const char *paper_memory_notes_path(void) { return PAPER_MEMORY_NOTES; }

esp_err_t paper_memory_append_turn(uint32_t turn_id,
                                   const char *faculty_slug,
                                   const char *faculty_name,
                                   const char *transcript,
                                   const char *reply)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    FILE *json = fopen(PAPER_MEMORY_CONVERSATIONS, "a");
    if (json == NULL) {
        ESP_LOGW(TAG, "open %s failed errno=%d", PAPER_MEMORY_CONVERSATIONS, errno);
        return ESP_FAIL;
    }
    fprintf(json, "{\"type\":\"conversation_turn\",\"ts_ms\":%lld,\"turn\":%u,\"facultySlug\":\"",
            (long long)now_ms(), (unsigned)turn_id);
    json_write_escaped(json, faculty_slug);
    fputs("\",\"facultyName\":\"", json);
    json_write_escaped(json, faculty_name);
    fputs("\",\"transcript\":\"", json);
    json_write_escaped(json, transcript);
    fputs("\",\"reply\":\"", json);
    json_write_escaped(json, reply);
    fputs("\"}\n", json);
    const int json_err = fclose(json);

    FILE *history = fopen(PAPER_MEMORY_HISTORY, "a");
    if (history != NULL) {
        fprintf(history, "User: %s | Faculty: %s\n", transcript != NULL ? transcript : "", reply != NULL ? reply : "");
        fclose(history);
    } else {
        ESP_LOGW(TAG, "open %s failed errno=%d", PAPER_MEMORY_HISTORY, errno);
    }
    if (json_err != 0) {
        ESP_LOGW(TAG, "close %s failed errno=%d", PAPER_MEMORY_CONVERSATIONS, errno);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t paper_memory_append_note(const char *source, const char *text)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (text == NULL || text[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    FILE *f = fopen(PAPER_MEMORY_NOTES, "a");
    if (f == NULL) {
        return ESP_FAIL;
    }
    fprintf(f, "{\"type\":\"note\",\"ts_ms\":%lld,\"source\":\"", (long long)now_ms());
    json_write_escaped(f, source != NULL && source[0] != '\0' ? source : "serial");
    fputs("\",\"text\":\"", f);
    json_write_escaped(f, text);
    fputs("\"}\n", f);
    return fclose(f) == 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t paper_memory_load_recent_history(char *out, size_t cap)
{
    if (out == NULL || cap == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    out[0] = '\0';
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    FILE *f = fopen(PAPER_MEMORY_HISTORY, "r");
    if (f == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    char line[256];
    while (fgets(line, sizeof(line), f) != NULL) {
        trim_newline(line);
        if (line[0] == '\0') {
            continue;
        }
        const size_t cur = strlen(out);
        const size_t add = strlen(line) + (cur > 0 ? 4u : 0u);
        if (cur + add + 1 > cap) {
            const size_t drop = (cur + add + 1) - cap;
            const size_t keep_from = drop < cur ? drop : cur;
            memmove(out, out + keep_from, cur - keep_from + 1u);
        }
        if (out[0] != '\0') {
            paper_strlcpy(out + strlen(out), " || ", cap - strlen(out));
        }
        paper_strlcpy(out + strlen(out), line, cap - strlen(out));
    }
    fclose(f);
    return out[0] != '\0' ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t paper_memory_load_recent_messages(paper_memory_message_t *out, size_t max_messages, size_t *out_count)
{
    if (out_count != NULL) {
        *out_count = 0;
    }
    if (out == NULL || max_messages == 0 || out_count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    FILE *f = fopen(PAPER_MEMORY_HISTORY, "r");
    if (f == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    char line[384];
    size_t count = 0;
    while (fgets(line, sizeof(line), f) != NULL) {
        trim_newline(line);
        char *user = strstr(line, "User: ");
        char *faculty = strstr(line, " | Faculty: ");
        if (user == NULL || faculty == NULL || faculty <= user) {
            continue;
        }
        *faculty = '\0';
        user += strlen("User: ");
        faculty += strlen(" | Faculty: ");
        append_recent_message(out, max_messages, &count, false, user);
        append_recent_message(out, max_messages, &count, true, faculty);
    }
    fclose(f);
    *out_count = count;
    return count > 0 ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t paper_memory_clear_history(void)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    remove(PAPER_MEMORY_HISTORY);
    return ESP_OK;
}
