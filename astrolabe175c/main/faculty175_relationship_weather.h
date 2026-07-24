#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "esp_err.h"
#include "faculty175_charts.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FACULTY175_RELATIONSHIP_ARC_DAYS 10
#define FACULTY175_RELATIONSHIP_DATE_LEN 11

typedef enum {
    FACULTY175_RELATIONSHIP_OPEN = 0,
    FACULTY175_RELATIONSHIP_EASY,
    FACULTY175_RELATIONSHIP_CHANGEABLE,
    FACULTY175_RELATIONSHIP_TENDER,
    FACULTY175_RELATIONSHIP_INTENSE,
    FACULTY175_RELATIONSHIP_CONDITION_COUNT,
} faculty175_relationship_condition_t;

typedef struct {
    bool available;
    char primary_name[sizeof(((faculty175_birth_chart_t *)0)->name)];
    char target_name[sizeof(((faculty175_birth_chart_t *)0)->name)];
    char selected_date[FACULTY175_RELATIONSHIP_DATE_LEN];
    int active_slot;
    int offset_days;
    time_t selected_epoch;
    faculty175_relationship_condition_t arc[FACULTY175_RELATIONSHIP_ARC_DAYS];
} faculty175_relationship_weather_snapshot_t;

faculty175_relationship_condition_t faculty175_relationship_weather_at(
    const faculty175_chart_positions_t *primary,
    const faculty175_chart_positions_t *target,
    time_t epoch);

const char *faculty175_relationship_weather_name(
    faculty175_relationship_condition_t condition);
const char *faculty175_relationship_weather_guidance(
    faculty175_relationship_condition_t condition);
const char *faculty175_relationship_weather_symbol(
    faculty175_relationship_condition_t condition);
uint32_t faculty175_relationship_weather_color(
    faculty175_relationship_condition_t condition);

/** Select a civil date for time travel. An empty date returns to today. */
esp_err_t faculty175_relationship_weather_select_date(const char *date);
void faculty175_relationship_weather_select_today(void);
bool faculty175_relationship_weather_snapshot(
    faculty175_relationship_weather_snapshot_t *out);

#ifdef __cplusplus
}
#endif
