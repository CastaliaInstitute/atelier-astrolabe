#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <time.h>

/** Launches shown on the launch-clock face and dial. */
static constexpr int kPmRocketMaxLaunches = 6;

struct PmRocketLaunch {
  bool valid = false;
  char id[40];
  char name[72];
  char vehicle[48];
  char provider[40];
  char pad[40];
  char location[56];
  char status_abbrev[16];
  /** Official webcast URL from LL2 vidURLs (YouTube, etc.). */
  char webcast_url[128];
  bool webcast_live = false;
  int64_t net_unix = 0;
};

struct PmRocketStatus {
  bool ok = false;
  int count = 0;
  PmRocketLaunch launches[kPmRocketMaxLaunches];
  char error[96];
};

/** GET Launch Library 2 upcoming launches (next ~2 weeks). Blocking. */
bool pm_rocket_fetch(PmRocketStatus *out);

/** First valid launch in `status`, or nullptr. */
const PmRocketLaunch *pm_rocket_next(const PmRocketStatus *status);
