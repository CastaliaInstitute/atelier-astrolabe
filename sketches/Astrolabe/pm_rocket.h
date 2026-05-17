#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <time.h>

struct PmRocketLaunch {
  bool valid = false;
  char name[72];
  char vehicle[48];
  char provider[40];
  char pad[40];
  char location[56];
  char status_abbrev[16];
  int64_t net_unix = 0;
};

struct PmRocketStatus {
  bool ok = false;
  PmRocketLaunch upcoming;
  char error[96];
};

/** GET Launch Library 2 upcoming launches; picks the next non-past event. Blocking. */
bool pm_rocket_fetch(PmRocketStatus *out);
