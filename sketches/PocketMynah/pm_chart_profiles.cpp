#include "pm_chart_profiles.h"

#include <Preferences.h>
#include <stdio.h>
#include <string.h>

static constexpr const char *kNvsNs = "mynah";

static bool profile_fields_sane(const PmChartProfile *p) {
  return p && p->name[0] != '\0' && p->year >= 1900 && p->year <= 2100 && p->month >= 1 && p->month <= 12 &&
         p->day >= 1 && p->day <= 31 && p->hour <= 23 && p->minute <= 59;
}

static void key_for_slot(char *out, size_t cap, int slot, const char *suffix) {
  snprintf(out, cap, "ch%d_%s", slot, suffix);
}

static bool load_slot(Preferences &pref, int slot, PmChartProfile *out) {
  char key[16];
  key_for_slot(key, sizeof(key), slot, "ok");
  if (!pref.getBool(key, false)) {
    return false;
  }
  memset(out, 0, sizeof(*out));
  key_for_slot(key, sizeof(key), slot, "role");
  out->role = static_cast<PmChartRole>(pref.getUChar(key, 0));
  key_for_slot(key, sizeof(key), slot, "name");
  {
    const String name = pref.getString(key, "");
    strncpy(out->name, name.c_str(), sizeof(out->name) - 1);
    out->name[sizeof(out->name) - 1] = '\0';
  }
  key_for_slot(key, sizeof(key), slot, "y");
  out->year = static_cast<uint16_t>(pref.getUShort(key, 0));
  key_for_slot(key, sizeof(key), slot, "mo");
  out->month = pref.getUChar(key, 0);
  key_for_slot(key, sizeof(key), slot, "d");
  out->day = pref.getUChar(key, 0);
  key_for_slot(key, sizeof(key), slot, "h");
  out->hour = pref.getUChar(key, 0);
  key_for_slot(key, sizeof(key), slot, "mi");
  out->minute = pref.getUChar(key, 0);
  key_for_slot(key, sizeof(key), slot, "lat");
  out->lat_deg = pref.getFloat(key, 0.f);
  key_for_slot(key, sizeof(key), slot, "lon");
  out->lon_deg = pref.getFloat(key, 0.f);
  key_for_slot(key, sizeof(key), slot, "tz");
  out->tz_offset_sec = pref.getInt(key, 0);
  key_for_slot(key, sizeof(key), slot, "place");
  {
    const String place = pref.getString(key, "");
    strncpy(out->place, place.c_str(), sizeof(out->place) - 1);
    out->place[sizeof(out->place) - 1] = '\0';
  }
  if (!profile_fields_sane(out)) {
    out->valid = false;
    return false;
  }
  out->valid = true;
  return true;
}

const char *pm_chart_role_label(PmChartRole role) {
  return role == PmChartRoleChild ? "child" : "partner";
}

int pm_chart_profile_count(void) {
  Preferences pref;
  if (!pref.begin(kNvsNs, true)) {
    return 0;
  }
  int n = 0;
  for (int i = 0; i < kPmChartProfileSlots; ++i) {
    char key[16];
    key_for_slot(key, sizeof(key), i, "ok");
    if (pref.getBool(key, false)) {
      ++n;
    }
  }
  pref.end();
  return n;
}

bool pm_chart_profile_get(int slot, PmChartProfile *out) {
  if (!out || slot < 0 || slot >= kPmChartProfileSlots) {
    return false;
  }
  Preferences pref;
  if (!pref.begin(kNvsNs, true)) {
    return false;
  }
  const bool ok = load_slot(pref, slot, out);
  pref.end();
  return ok;
}

bool pm_chart_profile_save(int slot, const PmChartProfile *in) {
  if (!in || !in->valid || !profile_fields_sane(in) || slot < 0 || slot >= kPmChartProfileSlots) {
    return false;
  }
  Preferences pref;
  if (!pref.begin(kNvsNs, false)) {
    return false;
  }
  char key[16];
  key_for_slot(key, sizeof(key), slot, "ok");
  pref.putBool(key, true);
  key_for_slot(key, sizeof(key), slot, "role");
  pref.putUChar(key, static_cast<uint8_t>(in->role));
  key_for_slot(key, sizeof(key), slot, "name");
  pref.putString(key, in->name);
  key_for_slot(key, sizeof(key), slot, "y");
  pref.putUShort(key, in->year);
  key_for_slot(key, sizeof(key), slot, "mo");
  pref.putUChar(key, in->month);
  key_for_slot(key, sizeof(key), slot, "d");
  pref.putUChar(key, in->day);
  key_for_slot(key, sizeof(key), slot, "h");
  pref.putUChar(key, in->hour);
  key_for_slot(key, sizeof(key), slot, "mi");
  pref.putUChar(key, in->minute);
  key_for_slot(key, sizeof(key), slot, "lat");
  pref.putFloat(key, in->lat_deg);
  key_for_slot(key, sizeof(key), slot, "lon");
  pref.putFloat(key, in->lon_deg);
  key_for_slot(key, sizeof(key), slot, "tz");
  pref.putInt(key, in->tz_offset_sec);
  key_for_slot(key, sizeof(key), slot, "place");
  pref.putString(key, in->place);
  pref.end();
  return true;
}

void pm_chart_profile_clear(int slot) {
  if (slot < 0 || slot >= kPmChartProfileSlots) {
    return;
  }
  Preferences pref;
  if (!pref.begin(kNvsNs, false)) {
    return;
  }
  char key[16];
  key_for_slot(key, sizeof(key), slot, "ok");
  pref.putBool(key, false);
  pref.end();
}

int pm_chart_profile_first_free_slot(void) {
  for (int i = 0; i < kPmChartProfileSlots; ++i) {
    PmChartProfile tmp = {};
    if (!pm_chart_profile_get(i, &tmp)) {
      return i;
    }
  }
  return -1;
}
