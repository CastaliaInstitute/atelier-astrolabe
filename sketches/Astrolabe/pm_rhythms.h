#pragma once

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "pm_transit.h"

#define PM_RHYTHMS_CARD_SCHEMA "astrolabe_compact_card_v1"

static constexpr size_t kPmRhythmsMaxSignals = 12;

typedef enum {
  kPmRhythmsSignalTransitAspect = 0,
  kPmRhythmsSignalLunarPhase,
  kPmRhythmsSignalMoonSign,
  kPmRhythmsSignalSunSeason,
  kPmRhythmsSignalCycle,
} PmRhythmsSignalKind;

typedef struct {
  PmRhythmsSignalKind kind;
  int16_t score;
  int8_t body;
  int8_t natal_body;
  int8_t sign;
  int16_t aspect_deg;
  float orb_deg;
  int16_t cycle_day;
  char tag[20];
  char label[56];
  char safety[80];
} PmRhythmsSignal;

typedef struct {
  bool enabled;
  uint32_t last_period_start_ymd;
} PmRhythmsCycleState;

typedef struct {
  char schema[32];
  uint32_t local_ymd;
  bool stale;
  char stale_ribbon[32];
  char title[56];
  char favor[104];
  char watch[104];
  char practice[104];
  char symbols[80];
  char precision[96];
} PmRhythmsCompactCard;

bool pm_rhythms_compute_signals(const struct tm *utc, const struct tm *local, PmRhythmsSignal *signals,
                                size_t cap, size_t *count_out);
bool pm_rhythms_build_compact_card(const struct tm *utc, const struct tm *local,
                                   PmRhythmsCompactCard *out);
bool pm_rhythms_build_compact_card(PmRhythmsCompactCard *out);
bool pm_rhythms_get_compact_card(PmRhythmsCompactCard *out);

bool pm_rhythms_cycle_load(PmRhythmsCycleState *out);
void pm_rhythms_cycle_save(bool enabled, uint32_t last_period_start_ymd);
void pm_rhythms_cycle_clear(void);

void pm_rhythms_cache_clear(void);
