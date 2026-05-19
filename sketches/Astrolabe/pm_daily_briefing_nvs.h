#pragma once

#include <stdbool.h>

struct tm;

/**
 * Auto-play after WiFi + valid time when:
 * - local calendar day changed, or
 * - firmware SHA differs from last briefing (new flash).
 */
bool pm_daily_briefing_should_auto_play(const struct tm *local_tm);

void pm_daily_briefing_mark_played(const struct tm *local_tm);
