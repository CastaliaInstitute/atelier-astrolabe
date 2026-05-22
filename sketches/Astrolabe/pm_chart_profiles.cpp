#include "pm_chart_profiles.h"

#include "pm_nvs.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

static constexpr const char *kNvsNs = "mynah";
static constexpr const char *kKeyActive = "ch_active";

static bool profile_fields_sane(const PmChartProfile *p) {
  return p && p->name[0] != '\0' && p->year >= 1900 && p->year <= 2100 && p->month >= 1 &&
         p->month <= 12 && p->day >= 1 && p->day <= 31 && p->hour <= 23 && p->minute <= 59 &&
         p->lat_deg >= -90.f && p->lat_deg <= 90.f && p->lon_deg >= -180.f && p->lon_deg <= 180.f;
}

static void key_for_slot(char *out, size_t cap, int slot, const char *suffix) {
  snprintf(out, cap, "ch%d_%s", slot, suffix);
}

static bool load_slot(int slot, PmChartProfile *out) {
  char key[16];
  key_for_slot(key, sizeof(key), slot, "ok");
  if (!pm_nvs_get_bool(kNvsNs, key, false)) {
    return false;
  }
  memset(out, 0, sizeof(*out));
  key_for_slot(key, sizeof(key), slot, "role");
  out->role = static_cast<PmChartRole>(pm_nvs_get_u8(kNvsNs, key, 0));
  key_for_slot(key, sizeof(key), slot, "name");
  pm_nvs_get_str(kNvsNs, key, out->name, sizeof(out->name), "");
  key_for_slot(key, sizeof(key), slot, "y");
  out->year = pm_nvs_get_u16(kNvsNs, key, 0);
  key_for_slot(key, sizeof(key), slot, "mo");
  out->month = pm_nvs_get_u8(kNvsNs, key, 0);
  key_for_slot(key, sizeof(key), slot, "d");
  out->day = pm_nvs_get_u8(kNvsNs, key, 0);
  key_for_slot(key, sizeof(key), slot, "h");
  out->hour = pm_nvs_get_u8(kNvsNs, key, 0);
  key_for_slot(key, sizeof(key), slot, "mi");
  out->minute = pm_nvs_get_u8(kNvsNs, key, 0);
  key_for_slot(key, sizeof(key), slot, "lat");
  out->lat_deg = pm_nvs_get_float(kNvsNs, key, 0.f);
  key_for_slot(key, sizeof(key), slot, "lon");
  out->lon_deg = pm_nvs_get_float(kNvsNs, key, 0.f);
  key_for_slot(key, sizeof(key), slot, "tz");
  out->tz_offset_sec = pm_nvs_get_i32(kNvsNs, key, 0);
  key_for_slot(key, sizeof(key), slot, "place");
  pm_nvs_get_str(kNvsNs, key, out->place, sizeof(out->place), "");
  if (!profile_fields_sane(out)) {
    out->valid = false;
    return false;
  }
  out->valid = true;
  return true;
}

static bool profile_name_exists(const char *name) {
  if (!name || name[0] == '\0') {
    return false;
  }
  for (int i = 0; i < kPmChartProfileSlots; ++i) {
    PmChartProfile tmp = {};
    if (pm_chart_profile_get(i, &tmp) && strcasecmp(tmp.name, name) == 0) {
      return true;
    }
  }
  return false;
}

const char *pm_chart_role_label(PmChartRole role) {
  return role == PmChartRoleChild ? "child" : "partner";
}

int pm_chart_profile_count(void) {
  int n = 0;
  for (int i = 0; i < kPmChartProfileSlots; ++i) {
    char key[16];
    key_for_slot(key, sizeof(key), i, "ok");
    if (pm_nvs_get_bool(kNvsNs, key, false)) {
      ++n;
    }
  }
  return n;
}

bool pm_chart_profile_get(int slot, PmChartProfile *out) {
  if (!out || slot < 0 || slot >= kPmChartProfileSlots) {
    return false;
  }
  return load_slot(slot, out);
}

bool pm_chart_profile_save(int slot, const PmChartProfile *in) {
  if (!in || !in->valid || !profile_fields_sane(in) || slot < 0 || slot >= kPmChartProfileSlots) {
    return false;
  }
  bool ok = true;
  char key[16];
  key_for_slot(key, sizeof(key), slot, "ok");
  ok &= pm_nvs_set_bool(kNvsNs, key, true);
  key_for_slot(key, sizeof(key), slot, "role");
  ok &= pm_nvs_set_u8(kNvsNs, key, static_cast<uint8_t>(in->role));
  key_for_slot(key, sizeof(key), slot, "name");
  ok &= pm_nvs_set_str(kNvsNs, key, in->name);
  key_for_slot(key, sizeof(key), slot, "y");
  ok &= pm_nvs_set_u16(kNvsNs, key, in->year);
  key_for_slot(key, sizeof(key), slot, "mo");
  ok &= pm_nvs_set_u8(kNvsNs, key, in->month);
  key_for_slot(key, sizeof(key), slot, "d");
  ok &= pm_nvs_set_u8(kNvsNs, key, in->day);
  key_for_slot(key, sizeof(key), slot, "h");
  ok &= pm_nvs_set_u8(kNvsNs, key, in->hour);
  key_for_slot(key, sizeof(key), slot, "mi");
  ok &= pm_nvs_set_u8(kNvsNs, key, in->minute);
  key_for_slot(key, sizeof(key), slot, "lat");
  ok &= pm_nvs_set_float(kNvsNs, key, in->lat_deg);
  key_for_slot(key, sizeof(key), slot, "lon");
  ok &= pm_nvs_set_float(kNvsNs, key, in->lon_deg);
  key_for_slot(key, sizeof(key), slot, "tz");
  ok &= pm_nvs_set_i32(kNvsNs, key, in->tz_offset_sec);
  key_for_slot(key, sizeof(key), slot, "place");
  ok &= pm_nvs_set_str(kNvsNs, key, in->place);
  return ok;
}

void pm_chart_profile_clear(int slot) {
  if (slot < 0 || slot >= kPmChartProfileSlots) {
    return;
  }
  char key[16];
  key_for_slot(key, sizeof(key), slot, "ok");
  (void)pm_nvs_set_bool(kNvsNs, key, false);
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

void pm_chart_profiles_ensure_demo_seed(void) {
  static const PmChartProfile kSeeds[] = {
      {"Camille", PmChartRolePartner, 1984, 9, 23, 12, 0, 42.9814f, -70.9478f, -4 * 3600,
       "Exeter, NH", true},
      {"Aidan", PmChartRoleChild, 2003, 9, 12, 12, 0, 39.6133f, -105.0166f, -6 * 3600,
       "Littleton, CO", true},
      {"Finn", PmChartRoleChild, 2024, 4, 30, 12, 0, 39.0917f, -104.8728f, -6 * 3600,
       "Monument, CO", true},
      {"Aleia", PmChartRoleChild, 2025, 5, 4, 12, 0, 39.0917f, -104.8728f, -6 * 3600,
       "Monument, CO", true},
  };
  for (const auto &seed : kSeeds) {
    if (profile_name_exists(seed.name)) {
      continue;
    }
    const int slot = pm_chart_profile_first_free_slot();
    if (slot < 0) {
      break;
    }
    (void)pm_chart_profile_save(slot, &seed);
  }
  PmChartProfile active = {};
  if (!pm_chart_profiles_active(&active)) {
    int first = -1;
    for (int i = 0; i < kPmChartProfileSlots; ++i) {
      if (pm_chart_profile_get(i, &active)) {
        first = i;
        break;
      }
    }
    if (first >= 0) {
      (void)pm_chart_profiles_set_active_slot(first);
    }
  }
}

void pm_chart_profiles_load_family_demo(void) {
  pm_chart_profiles_ensure_demo_seed();
  for (int i = 0; i < kPmChartProfileSlots; ++i) {
    PmChartProfile profile = {};
    if (pm_chart_profile_get(i, &profile) && strcasecmp(profile.name, "Camille") == 0) {
      (void)pm_chart_profiles_set_active_slot(i);
      return;
    }
  }
}

int pm_chart_profiles_active_slot(void) {
  const int slot = pm_nvs_get_i32(kNvsNs, kKeyActive, -1);
  if (slot < 0 || slot >= kPmChartProfileSlots) {
    return -1;
  }
  PmChartProfile tmp = {};
  return pm_chart_profile_get(slot, &tmp) ? slot : -1;
}

bool pm_chart_profiles_set_active_slot(int slot) {
  PmChartProfile tmp = {};
  if (!pm_chart_profile_get(slot, &tmp)) {
    return false;
  }
  return pm_nvs_set_i32(kNvsNs, kKeyActive, slot);
}

bool pm_chart_profiles_cycle_active(int delta, int *slot_out, PmChartProfile *profile_out) {
  int slots[kPmChartProfileSlots];
  int n = 0;
  for (int i = 0; i < kPmChartProfileSlots; ++i) {
    PmChartProfile tmp = {};
    if (pm_chart_profile_get(i, &tmp)) {
      slots[n++] = i;
    }
  }
  if (n <= 0) {
    return false;
  }
  int cur = pm_chart_profiles_active_slot();
  int pos = 0;
  for (int i = 0; i < n; ++i) {
    if (slots[i] == cur) {
      pos = i;
      break;
    }
  }
  const int next_pos = (pos + delta % n + n) % n;
  const int next_slot = slots[next_pos];
  if (!pm_chart_profiles_set_active_slot(next_slot)) {
    return false;
  }
  if (slot_out) {
    *slot_out = next_slot;
  }
  if (profile_out) {
    return pm_chart_profile_get(next_slot, profile_out);
  }
  return true;
}

bool pm_chart_profiles_active(PmChartProfile *out) {
  int slot = pm_chart_profiles_active_slot();
  if (slot < 0) {
    for (int i = 0; i < kPmChartProfileSlots; ++i) {
      PmChartProfile tmp = {};
      if (pm_chart_profile_get(i, &tmp)) {
        slot = i;
        break;
      }
    }
  }
  return slot >= 0 && pm_chart_profile_get(slot, out);
}

bool pm_chart_profile_to_birth(const PmChartProfile *profile, PmBirthSpec *out) {
  if (!profile || !profile->valid || !out || !profile_fields_sane(profile)) {
    return false;
  }
  memset(out, 0, sizeof(*out));
  out->year = profile->year;
  out->month = profile->month;
  out->day = profile->day;
  out->hour = profile->hour;
  out->minute = profile->minute;
  out->lat_deg = profile->lat_deg;
  out->lon_deg = profile->lon_deg;
  out->tz_offset_sec = profile->tz_offset_sec;
  strncpy(out->place, profile->place, sizeof(out->place) - 1);
  out->place[sizeof(out->place) - 1] = '\0';
  out->valid = true;
  return true;
}
