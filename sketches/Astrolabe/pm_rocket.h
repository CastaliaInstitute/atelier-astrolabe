#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <time.h>

/** Launches shown on the launch-clock face and dial. */
static constexpr int kPmRocketMaxLaunches = 6;
static constexpr int kPmRocketMaxTimelineEvents = 18;

struct PmRocketTimelineEvent {
  bool valid = false;
  int32_t offset_sec = 0;
  char label[28];
};

struct PmRocketLaunch {
  bool valid = false;
  char id[40];
  char name[72];
  char vehicle[48];
  char provider[40];
  char pad[40];
  char location[56];
  char status_abbrev[16];
  /** Phone/QR stream URL. Usually a Supabase media-stream redirect. */
  char webcast_url[192];
  bool webcast_live = false;
  int64_t net_unix = 0;
};

struct PmRocketStatus {
  bool ok = false;
  int count = 0;
  PmRocketLaunch launches[kPmRocketMaxLaunches];
  int timeline_count = 0;
  PmRocketTimelineEvent timeline[kPmRocketMaxTimelineEvents];
  char error[96];
};

/** GET Launch Library 2 upcoming launches (next ~2 weeks). Blocking. */
bool pm_rocket_fetch(PmRocketStatus *out);

/** Start a background Launch Library fetch on a dedicated stack. */
bool pm_rocket_request_fetch(void);

/** Consume a completed background fetch result, if one is available. */
bool pm_rocket_consume_fetch(PmRocketStatus *out);

/** Background fetch is currently running. */
bool pm_rocket_fetch_busy(void);

/** FreeRTOS stack high-water mark for the background fetch task. */
uint32_t pm_rocket_fetch_stack_high_water(void);

/** Delete the idle background fetch task after its result has been consumed. */
bool pm_rocket_release_idle_task(void);

/** First valid launch in `status`, or nullptr. */
const PmRocketLaunch *pm_rocket_next(const PmRocketStatus *status);

/** Decoded launch/pad JPEG for the current primary launch (from last fetch). */
bool pm_rocket_pad_image_ready(void);

/** Draw photo centered, cover-scaled, dimmed (call before ring UI). */
void pm_rocket_pad_image_draw_background(int cx, int cy, int cover_radius, uint16_t bg_color, float dim_alpha);

/** Free decoded image (call when leaving Rocket face or primary launch changes). */
void pm_rocket_pad_image_release(void);
