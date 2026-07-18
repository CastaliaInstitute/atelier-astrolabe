#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FACULTY175_CHART_BODY_COUNT 7
#define FACULTY175_CHART_PROFILE_SLOTS 8

typedef enum {
    FACULTY175_CHART_ROLE_SELF = 0,
    FACULTY175_CHART_ROLE_PARTNER = 1,
    FACULTY175_CHART_ROLE_CHILD = 2,
} faculty175_chart_role_t;

typedef struct {
    char name[32];
    faculty175_chart_role_t role;
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    float lat_deg;
    float lon_deg;
    int32_t tz_offset_sec;
    char place[48];
    bool valid;
} faculty175_birth_chart_t;

typedef struct {
    double lon[FACULTY175_CHART_BODY_COUNT];
    bool ok;
} faculty175_chart_positions_t;

void faculty175_charts_ensure_family_seed(void);
bool faculty175_charts_sync_family_repo(void);
void faculty175_charts_family_repo(char *out, size_t out_cap);
bool faculty175_charts_primary(faculty175_birth_chart_t *out);
esp_err_t faculty175_charts_save_primary(const faculty175_birth_chart_t *chart);
int faculty175_charts_profile_count(void);
bool faculty175_charts_profile_get(int slot, faculty175_birth_chart_t *out);
esp_err_t faculty175_charts_profile_save(int slot, const faculty175_birth_chart_t *chart);
esp_err_t faculty175_charts_profile_clear(int slot);
int faculty175_charts_active_slot(void);
bool faculty175_charts_set_active_slot(int slot);
bool faculty175_charts_cycle_active(int delta, int *slot_out, faculty175_birth_chart_t *profile_out);
bool faculty175_charts_active(faculty175_birth_chart_t *out);
bool faculty175_charts_birth_positions(const faculty175_birth_chart_t *birth, faculty175_chart_positions_t *out);
bool faculty175_charts_positions_at(time_t epoch, faculty175_chart_positions_t *out);
bool faculty175_charts_birth_to_utc(const faculty175_birth_chart_t *birth, time_t *utc_out);
const char *faculty175_charts_body_label(int body);
const char *faculty175_charts_zodiac_abbr(double lon);
const char *faculty175_charts_role_label(faculty175_chart_role_t role);
bool faculty175_charts_handle(const char *line);

#ifdef __cplusplus
}
#endif
