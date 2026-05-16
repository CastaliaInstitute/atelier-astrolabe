#pragma once

#include <stdbool.h>
#include <stdint.h>

/** Civil birth date/time in the same local timezone as `localtime` after NTP (not stored separately). */
typedef struct {
  uint16_t year;
  uint8_t month;
  uint8_t day;
  uint8_t hour;
  uint8_t minute;
  bool valid;
} PmBirthSpec;

bool pm_birth_load(PmBirthSpec *out);
void pm_birth_save(const PmBirthSpec *in);
void pm_birth_clear(void);
