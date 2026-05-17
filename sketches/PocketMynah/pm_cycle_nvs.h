#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

typedef struct {
  uint16_t last_period_year;
  uint8_t last_period_month;
  uint8_t last_period_day;
  uint8_t cycle_length_days;
  uint8_t period_length_days;
  bool has_last_period;
} PmCycleProfile;

static constexpr uint8_t PM_CYCLE_DEFAULT_LENGTH_DAYS = 28;
static constexpr uint8_t PM_CYCLE_DEFAULT_PERIOD_DAYS = 5;
static constexpr uint8_t PM_CYCLE_MIN_LENGTH_DAYS = 21;
static constexpr uint8_t PM_CYCLE_MAX_LENGTH_DAYS = 45;

bool pm_cycle_load(PmCycleProfile *out);
void pm_cycle_save(const PmCycleProfile *in);
void pm_cycle_clear(void);

bool pm_cycle_set_last_period(uint16_t year, uint8_t month, uint8_t day);
bool pm_cycle_log_period_started_today(const struct tm *local_tm);
bool pm_cycle_set_cycle_length_days(uint8_t days);
bool pm_cycle_set_period_length_days(uint8_t days);
uint8_t pm_cycle_adjust_cycle_length_preset(int delta);

bool pm_cycle_ymd_sane(uint16_t year, uint8_t month, uint8_t day);
int32_t pm_cycle_days_between(uint16_t start_year, uint8_t start_month, uint8_t start_day,
                              uint16_t end_year, uint8_t end_month, uint8_t end_day);
int32_t pm_cycle_day_index_for_date(const PmCycleProfile *profile, uint16_t year, uint8_t month, uint8_t day);
