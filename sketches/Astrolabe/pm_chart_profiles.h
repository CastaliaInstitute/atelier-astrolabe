#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "pm_birth_nvs.h"

/** Partner or child birth profile for synastry / family charts (NVS). */
typedef enum : uint8_t {
  PmChartRolePartner = 0,
  PmChartRoleChild = 1,
} PmChartRole;

typedef struct {
  char name[24];
  PmChartRole role;
  uint16_t year;
  uint8_t month;
  uint8_t day;
  uint8_t hour;
  uint8_t minute;
  float lat_deg;
  float lon_deg;
  int32_t tz_offset_sec;
  char place[40];
  bool valid;
} PmChartProfile;

static constexpr int kPmChartProfileSlots = 8;

int pm_chart_profile_count(void);
bool pm_chart_profile_get(int slot, PmChartProfile *out);
bool pm_chart_profile_save(int slot, const PmChartProfile *in);
void pm_chart_profile_clear(int slot);
int pm_chart_profile_first_free_slot(void);

/** Seeds Camille, Aidan, Finn, and Aleia into empty/missing NVS slots without overwriting user edits. */
void pm_chart_profiles_ensure_demo_seed(void);

int pm_chart_profiles_active_slot(void);
bool pm_chart_profiles_set_active_slot(int slot);
bool pm_chart_profiles_cycle_active(int delta, int *slot_out, PmChartProfile *profile_out);
bool pm_chart_profiles_active(PmChartProfile *out);
bool pm_chart_profile_to_birth(const PmChartProfile *profile, PmBirthSpec *out);

const char *pm_chart_role_label(PmChartRole role);
