#include "faculty175_cycle_health.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "astrolabe_time.h"
#include "nvs.h"

#define CYCLE_NVS_NS "cycle"
#define CYCLE_NVS_START "start_day"
#define CYCLE_NVS_LENGTH "length"
#define CYCLE_NVS_PERIOD "period"

enum { CYCLE_DEFAULT_LENGTH = 28, CYCLE_DEFAULT_PERIOD = 5 };

static bool parse_ymd(const char *text, struct tm *out)
{
    if (text == NULL || out == NULL) {
        return false;
    }
    int year = 0;
    int month = 0;
    int day = 0;
    char trailing = '\0';
    if (sscanf(text, "%d-%d-%d%c", &year, &month, &day, &trailing) != 3 ||
        year < 2024 || year > 2100 || month < 1 || month > 12 || day < 1 || day > 31) {
        return false;
    }
    struct tm local = {
        .tm_year = year - 1900,
        .tm_mon = month - 1,
        .tm_mday = day,
        .tm_hour = 12,
        .tm_isdst = -1,
    };
    const time_t epoch = mktime(&local);
    if (epoch < ASTROLABE_TIME_VALID_MIN_EPOCH) {
        return false;
    }
    struct tm verify = {};
    localtime_r(&epoch, &verify);
    if (verify.tm_year != local.tm_year || verify.tm_mon != local.tm_mon || verify.tm_mday != local.tm_mday) {
        return false;
    }
    *out = local;
    return true;
}

/* A timezone- and DST-safe civil date ordinal. */
static int32_t civil_day(const struct tm *local)
{
    int y = local->tm_year + 1900;
    const unsigned m = (unsigned)local->tm_mon + 1u;
    const unsigned d = (unsigned)local->tm_mday;
    y -= m <= 2u;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned mp = (unsigned)((int)m + (m > 2u ? -3 : 9));
    const unsigned doy = (153u * mp + 2u) / 5u + d - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097 + (int32_t)doe;
}

static esp_err_t load_config(int32_t *start_day, uint8_t *length, uint8_t *period)
{
    if (start_day == NULL || length == NULL || period == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *start_day = 0;
    *length = CYCLE_DEFAULT_LENGTH;
    *period = CYCLE_DEFAULT_PERIOD;
    nvs_handle_t nvs;
    const esp_err_t open_err = nvs_open(CYCLE_NVS_NS, NVS_READONLY, &nvs);
    if (open_err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (open_err != ESP_OK) {
        return open_err;
    }
    (void)nvs_get_i32(nvs, CYCLE_NVS_START, start_day);
    (void)nvs_get_u8(nvs, CYCLE_NVS_LENGTH, length);
    (void)nvs_get_u8(nvs, CYCLE_NVS_PERIOD, period);
    nvs_close(nvs);
    if (*length < 21 || *length > 45) {
        *length = CYCLE_DEFAULT_LENGTH;
    }
    if (*period < 1 || *period > 14 || *period >= *length) {
        *period = CYCLE_DEFAULT_PERIOD;
    }
    return ESP_OK;
}

static esp_err_t store_config(int32_t start_day, uint8_t length, uint8_t period)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(CYCLE_NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_i32(nvs, CYCLE_NVS_START, start_day);
    if (err == ESP_OK) err = nvs_set_u8(nvs, CYCLE_NVS_LENGTH, length);
    if (err == ESP_OK) err = nvs_set_u8(nvs, CYCLE_NVS_PERIOD, period);
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);
    return err;
}

esp_err_t faculty175_cycle_health_set_start(const char *yyyy_mm_dd)
{
    struct tm anchor = {};
    if (!parse_ymd(yyyy_mm_dd, &anchor)) {
        return ESP_ERR_INVALID_ARG;
    }
    int32_t start = 0;
    uint8_t length = CYCLE_DEFAULT_LENGTH;
    uint8_t period = CYCLE_DEFAULT_PERIOD;
    esp_err_t err = load_config(&start, &length, &period);
    if (err != ESP_OK) return err;
    const time_t epoch = mktime(&anchor);
    return store_config((int32_t)epoch, length, period);
}

esp_err_t faculty175_cycle_health_set_lengths(uint8_t cycle_length, uint8_t period_length)
{
    if (cycle_length < 21 || cycle_length > 45 || period_length < 1 || period_length > 14 ||
        period_length >= cycle_length) {
        return ESP_ERR_INVALID_ARG;
    }
    int32_t start = 0;
    uint8_t ignored_length = 0;
    uint8_t ignored_period = 0;
    const esp_err_t err = load_config(&start, &ignored_length, &ignored_period);
    return err == ESP_OK ? store_config(start, cycle_length, period_length) : err;
}

esp_err_t faculty175_cycle_health_mark_bleeding_started_today(void)
{
    if (!astrolabe_time_valid()) {
        return ESP_ERR_INVALID_STATE;
    }
    struct tm local = {};
    astrolabe_time_local(&local);
    char today[11];
    snprintf(today, sizeof(today), "%04d-%02d-%02d", local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
    return faculty175_cycle_health_set_start(today);
}

esp_err_t faculty175_cycle_health_mark_bleeding_stopped_today(void)
{
    faculty175_cycle_health_status_t status = {};
    const esp_err_t err = faculty175_cycle_health_status(&status);
    if (err != ESP_OK) return err;
    if (!status.configured || status.day == 0) return ESP_ERR_INVALID_STATE;
    const uint8_t period = status.day > 14 ? 14 : status.day;
    return faculty175_cycle_health_set_lengths(status.cycle_length, period);
}

const char *faculty175_cycle_health_phase_label(faculty175_cycle_phase_t phase)
{
    switch (phase) {
        case FACULTY175_CYCLE_PHASE_MENSTRUATION: return "MENSTRUATION";
        case FACULTY175_CYCLE_PHASE_FOLLICULAR: return "FOLLICULAR";
        case FACULTY175_CYCLE_PHASE_OVULATION: return "OVULATION";
        case FACULTY175_CYCLE_PHASE_LUTEAL: return "LUTEAL";
        default: return "SET CYCLE START";
    }
}

esp_err_t faculty175_cycle_health_status(faculty175_cycle_health_status_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    int32_t start_day = 0;
    esp_err_t err = load_config(&start_day, &out->cycle_length, &out->period_length);
    if (err != ESP_OK) {
        return err;
    }
    out->time_valid = astrolabe_time_valid();
    if (start_day < ASTROLABE_TIME_VALID_MIN_EPOCH || !out->time_valid) {
        return ESP_OK;
    }
    struct tm local = {};
    astrolabe_time_local(&local);
    local.tm_hour = 12;
    local.tm_min = 0;
    local.tm_sec = 0;
    local.tm_isdst = -1;
    const time_t now = mktime(&local);
    if (now < ASTROLABE_TIME_VALID_MIN_EPOCH) {
        return ESP_OK;
    }
    const time_t anchor = (time_t)start_day;
    struct tm anchor_local = {};
    localtime_r(&anchor, &anchor_local);
    const int32_t today_day = civil_day(&local);
    const int32_t anchor_day = civil_day(&anchor_local);
    if (today_day < anchor_day) {
        return ESP_OK;
    }
    out->configured = true;
    const uint32_t elapsed = (uint32_t)(today_day - anchor_day);
    out->day = (uint8_t)(elapsed % out->cycle_length) + 1u;
    /* Reformat the actual stored anchor, not today's date. */
    snprintf(out->start_date, sizeof(out->start_date), "%04d-%02d-%02d",
             anchor_local.tm_year + 1900, anchor_local.tm_mon + 1, anchor_local.tm_mday);
    if (out->day <= out->period_length) {
        out->phase = FACULTY175_CYCLE_PHASE_MENSTRUATION;
    } else if (out->day == (uint8_t)(out->cycle_length - 13u)) {
        out->phase = FACULTY175_CYCLE_PHASE_OVULATION;
    } else if (out->day < (uint8_t)(out->cycle_length - 13u)) {
        out->phase = FACULTY175_CYCLE_PHASE_FOLLICULAR;
    } else {
        out->phase = FACULTY175_CYCLE_PHASE_LUTEAL;
    }
    return ESP_OK;
}

bool faculty175_cycle_health_handle(const char *line)
{
    if (line == NULL || strncasecmp(line, "cycle", 5) != 0 || (line[5] != '\0' && line[5] != ' ')) {
        return false;
    }
    const char *sub = line + 5;
    while (*sub == ' ') ++sub;
    if (*sub == '\0' || strcasecmp(sub, "status") == 0) {
        faculty175_cycle_health_status_t status = {};
        const esp_err_t err = faculty175_cycle_health_status(&status);
        printf("cycle: %s day=%u/%u phase=%s period=%u start=%s time=%s\n",
               status.configured ? "configured" : "not-configured",
               status.day, status.cycle_length, faculty175_cycle_health_phase_label(status.phase),
               status.period_length, status.start_date[0] != '\0' ? status.start_date : "-",
               status.time_valid ? "valid" : "waiting");
        (void)err;
        return true;
    }

    int32_t start_day = 0;
    uint8_t length = CYCLE_DEFAULT_LENGTH;
    uint8_t period = CYCLE_DEFAULT_PERIOD;
    esp_err_t err = load_config(&start_day, &length, &period);
    if (err != ESP_OK) {
        printf("cycle: %s\n", esp_err_to_name(err));
        return true;
    }
    if (strncasecmp(sub, "start ", 6) == 0) {
        err = faculty175_cycle_health_set_start(sub + 6);
    } else if (strncasecmp(sub, "length ", 7) == 0) {
        const int value = atoi(sub + 7);
        err = value >= 21 && value <= 45 ? store_config(start_day, (uint8_t)value, period) : ESP_ERR_INVALID_ARG;
    } else if (strncasecmp(sub, "period ", 7) == 0) {
        const int value = atoi(sub + 7);
        err = value >= 1 && value <= 14 && value < length ? store_config(start_day, length, (uint8_t)value) : ESP_ERR_INVALID_ARG;
    } else if (strcasecmp(sub, "clear") == 0) {
        err = store_config(0, length, period);
    } else if (strcasecmp(sub, "bleeding start") == 0) {
        err = faculty175_cycle_health_mark_bleeding_started_today();
    } else if (strcasecmp(sub, "bleeding stop") == 0) {
        err = faculty175_cycle_health_mark_bleeding_stopped_today();
    } else {
        printf("cycle: status | start YYYY-MM-DD | length 21..45 | period 1..14 | clear\n");
        return true;
    }
    printf("cycle: %s\n", esp_err_to_name(err));
    return true;
}
