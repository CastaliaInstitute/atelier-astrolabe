#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <time.h>

struct PmCalciferEvent {
  char summary[96];
  int64_t start_unix = 0;
  int64_t end_unix = 0;
  bool valid = false;
};

struct PmCalciferStatus {
  bool ok = false;
  bool configured = false;
  PmCalciferEvent current;
  PmCalciferEvent next;
  char error[96];
  char display_tz[48];
};

/** POST `calcifer-status` with optional epoch (defaults to now). Blocking; call off hot paint paths. */
bool pm_calcifer_fetch(PmCalciferStatus *out, time_t epoch_seconds = 0);
