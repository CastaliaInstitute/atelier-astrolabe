#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FACULTY175_ROCKET_MAX_LAUNCHES 5

typedef struct {
    bool valid;
    char id[24];
    char name[72];
    char vehicle[48];
    char provider[40];
    char pad[40];
    char location[56];
    char weather[72];
    char info_url[128];
    int64_t net_unix;
} faculty175_rocket_launch_t;

typedef struct {
    bool ok;
    int count;
    faculty175_rocket_launch_t launches[FACULTY175_ROCKET_MAX_LAUNCHES];
    char error[80];
} faculty175_rocket_status_t;

esp_err_t faculty175_rocket_init(void);
void faculty175_rocket_start_auto_fetch_task(void);
void faculty175_rocket_request_refresh(void);
bool faculty175_rocket_current(faculty175_rocket_status_t *out);
bool faculty175_rocket_handle(const char *line);
const char *faculty175_rocket_state_name(void);
const char *faculty175_rocket_last(void);
void faculty175_rocket_format_countdown(int64_t launch_unix, char *out, size_t cap);
void faculty175_rocket_format_local(int64_t launch_unix, char *out, size_t cap);

#ifdef __cplusplus
}
#endif
