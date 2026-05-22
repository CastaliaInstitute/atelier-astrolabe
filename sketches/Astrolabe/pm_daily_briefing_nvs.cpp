#include "pm_daily_briefing_nvs.h"

#include <string.h>
#include <time.h>

#include "pm_build_info.h"
#include "pm_nvs.h"

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
  char stored_sha[48] = "";
  const int last = pm_nvs_get_i32("mynah", "daily_brief_ymd", 0);
  (void)pm_nvs_get_str("mynah", "flash_sha", stored_sha, sizeof(stored_sha), "");
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
  (void)pm_nvs_set_i32("mynah", "daily_brief_ymd", today);
  (void)pm_nvs_set_str("mynah", "flash_sha", PM_BUILD_GIT_SHA_FULL);
}
