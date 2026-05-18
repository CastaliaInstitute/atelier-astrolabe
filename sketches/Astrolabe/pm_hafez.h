#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

struct PmHafezStatus {
  bool ok = false;
  bool configured = false;
  char quote[420] = "";
  char source[72] = "";
  char art_prompt[220] = "";
  char palette[40] = "";
  uint32_t art_seed = 0;
  uint32_t day_key = 0;
  char error[96] = "";
};

/** POST hafez-daily with optional epoch (defaults to now). Blocking; call off hot paint paths. */
bool pm_hafez_fetch(PmHafezStatus *out, time_t epoch_seconds = 0);
