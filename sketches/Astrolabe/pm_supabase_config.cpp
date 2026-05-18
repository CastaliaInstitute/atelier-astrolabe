#include "pm_supabase_config.h"

#include <Arduino.h>
#include <Preferences.h>
#include <cstring>

#include "pm_config.h"

static constexpr const char *kNvsNs = "mynah";
static constexpr const char *kKeySupabaseUrl = "sb_url";

static void trim_url(char *url) {
  if (!url) {
    return;
  }
  while (strlen(url) > 0 && url[strlen(url) - 1] == '/') {
    url[strlen(url) - 1] = '\0';
  }
}

static bool copy_trimmed(const char *in, char *out, size_t out_cap) {
  if (!in || !out || out_cap < 2) {
    return false;
  }
  strncpy(out, in, out_cap - 1);
  out[out_cap - 1] = '\0';
  trim_url(out);
  return out[0] != '\0';
}

bool pm_supabase_url_override_load(char *out, size_t out_cap) {
  if (!out || out_cap < 2) {
    return false;
  }
  out[0] = '\0';
  Preferences pref;
  if (!pref.begin(kNvsNs, true)) {
    return false;
  }
  const String url = pref.getString(kKeySupabaseUrl, "");
  pref.end();
  return copy_trimmed(url.c_str(), out, out_cap);
}

bool pm_supabase_url_get(char *out, size_t out_cap) {
  if (pm_supabase_url_override_load(out, out_cap)) {
    return true;
  }
  return copy_trimmed(MYNAH_SUPABASE_URL, out, out_cap);
}

bool pm_supabase_url_override_save(const char *url) {
  if (!url || url[0] == '\0') {
    pm_supabase_url_override_clear();
    return true;
  }

  char trimmed[160];
  if (!copy_trimmed(url, trimmed, sizeof(trimmed))) {
    return false;
  }
  if (strncmp(trimmed, "http://", 7) != 0 && strncmp(trimmed, "https://", 8) != 0) {
    return false;
  }

  Preferences pref;
  if (!pref.begin(kNvsNs, false)) {
    return false;
  }
  pref.putString(kKeySupabaseUrl, trimmed);
  pref.end();
  return true;
}

void pm_supabase_url_override_clear(void) {
  Preferences pref;
  if (!pref.begin(kNvsNs, false)) {
    return;
  }
  pref.remove(kKeySupabaseUrl);
  pref.end();
}
