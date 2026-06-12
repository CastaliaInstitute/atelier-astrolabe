#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FACULTY175_APOCALYPSO_AXIS_COUNT 12

typedef struct {
    bool ok;
    bool demo;
    char updated_at[28];
    float value[FACULTY175_APOCALYPSO_AXIS_COUNT];
    char quality[FACULTY175_APOCALYPSO_AXIS_COUNT][12];
} faculty175_apocalypso_status_t;

esp_err_t faculty175_apocalypso_init(void);
void faculty175_apocalypso_start_auto_fetch_task(void);
void faculty175_apocalypso_request_refresh(void);
bool faculty175_apocalypso_current(faculty175_apocalypso_status_t *out);
const char *faculty175_apocalypso_state_name(void);
const char *faculty175_apocalypso_last(void);

#ifdef __cplusplus
}
#endif
