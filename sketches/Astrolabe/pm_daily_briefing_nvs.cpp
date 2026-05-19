#include "pm_daily_briefing_nvs.h"

#include <Preferences.h>
#include <string.h>

#include "pm_build_info.h"

static int ymd_key(const struct tm *local_tm) {
  if (!local_tm) {
    return 0;
  }
  return (local_tm->tm_year + 1900) * 10000 + (local_tm->tm_mon + 1) * 100 + local_tm->tm_mday;
}

bool pm_daily_briefing_should_auto_play(const struct tm *local_tm) {
  const int today = ymd_key(local_tm);
  if (today <= 0) {
    return false;
  }
  Preferences pref;
  if (!pref.begin("mynah", true)) {
    return true;
  }
  const int last = pref.getInt("daily_brief_ymd", 0);
  char stored_sha[48] = "";
  pref.getString("flash_sha", stored_sha, sizeof(stored_sha));
  pref.end();
  if (strcmp(stored_sha, PM_BUILD_GIT_SHA_FULL) != 0) {
    return true;
  }
  return last != today;
}

void pm_daily_briefing_mark_played(const struct tm *local_tm) {
  const int today = ymd_key(local_tm);
  if (today <= 0) {
    return;
  }
  Preferences pref;
  if (!pref.begin("mynah", false)) {
    return;
  }
  pref.putInt("daily_brief_ymd", today);
  pref.putString("flash_sha", PM_BUILD_GIT_SHA_FULL);
  pref.end();
}
