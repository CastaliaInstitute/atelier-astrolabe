#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ASTROLABE_TIME_VALID_MIN_EPOCH 1704067200LL /* 2024-01-01T00:00:00Z */
#define ASTROLABE_TIME_TZ_MAX_LEN 63

typedef struct {
    const char *server;
    uint32_t sync_wait_ms;
} astrolabe_time_config_t;

typedef struct {
    bool started;
    bool synced;
    time_t epoch;
    int64_t last_sync_epoch;
    uint32_t retry_count;
    char tz[ASTROLABE_TIME_TZ_MAX_LEN + 1];
} astrolabe_time_status_t;

esp_err_t astrolabe_time_start(const astrolabe_time_config_t *config);
esp_err_t astrolabe_time_retry_if_stale(void);
bool astrolabe_time_valid(void);
time_t astrolabe_time_now(void);
void astrolabe_time_utc(struct tm *out_tm);
void astrolabe_time_local(struct tm *out_tm);
size_t astrolabe_time_format_utc(char *out, size_t cap);
size_t astrolabe_time_format_local(char *out, size_t cap);
const char *astrolabe_time_timezone(void);
esp_err_t astrolabe_time_set_timezone(const char *tz);
esp_err_t astrolabe_time_set_epoch(time_t epoch);
void astrolabe_time_status(astrolabe_time_status_t *out);

#ifdef __cplusplus
}
#endif
