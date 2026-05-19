#include "pm_daily_briefing.h"

#include <stdio.h>
#include <string.h>

#include "faces/astrology/pm_face_astrology.h"
#include "faces/moon/pm_face_moon.h"
#include "faces/synastry/pm_face_synastry.h"
#include "pm_build_info.h"
#include "pm_user_nvs.h"
#include "pm_wifi_ntp.h"

static bool append_section(char *out, size_t cap, size_t *off, const char *title, const char *body) {
  if (!out || !off || !title || !body || body[0] == '\0' || *off >= cap) {
    return false;
  }
  const int n = snprintf(out + *off, cap - *off, "\n\n[%s]\n%s", title, body);
  if (n <= 0 || static_cast<size_t>(n) >= cap - *off) {
    return false;
  }
  *off += static_cast<size_t>(n);
  return true;
}

bool pm_daily_briefing_build_device_facts(char *out, size_t cap) {
  if (!out || cap < 32) {
    return false;
  }
  out[0] = '\0';
  if (!pm_wifi_connected() || !pm_time_valid()) {
    return false;
  }

  size_t off = 0;
  out[0] = '\0';

  {
    char wearer[120];
    snprintf(wearer, sizeof(wearer),
             "Address the watch wearer as %s (honorific + first name only).",
             pm_user_formal_name());
    append_section(out, cap, &off, "WEARER", wearer);
  }

  {
    char flash[640];
    snprintf(flash, sizeof(flash),
             "Build %s on branch %s (%s).%s%s",
             PM_BUILD_GIT_SHA, PM_BUILD_BRANCH, PM_BUILD_DATE,
             PM_BUILD_DIRTY ? " Built with uncommitted local changes." : "",
             PM_BUILD_COMMIT_MSG[0] != '\0' ? " Commit message: " : "");
    if (PM_BUILD_COMMIT_MSG[0] != '\0') {
      const size_t used = strlen(flash);
      if (used + 2 < sizeof(flash)) {
        strncat(flash, PM_BUILD_COMMIT_MSG, sizeof(flash) - used - 1);
      }
    }
    append_section(out, cap, &off, "FLASH UPDATE", flash);
  }

  char astro_msg[2200];
  if (pm_face_astrology_build_voice_message(astro_msg, sizeof(astro_msg))) {
    append_section(out, cap, &off, "ASTROLOGY", astro_msg);
  }

  char syn_msg[2600];
  if (pm_face_synastry_build_voice_message(syn_msg, sizeof(syn_msg))) {
    append_section(out, cap, &off, "SYNASTRY", syn_msg);
  }

  char moon_msg[2200];
  if (pm_face_moon_build_fortune_message(moon_msg, sizeof(moon_msg))) {
    append_section(out, cap, &off, "MOON", moon_msg);
  }

  return off > 0 || out[0] != '\0';
}
