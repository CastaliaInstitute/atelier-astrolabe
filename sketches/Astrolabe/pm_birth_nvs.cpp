#include "pm_birth_nvs.h"

#include <Preferences.h>
#include <stdlib.h>
#include <string.h>

static constexpr const char *kNvsNs = "mynah";
static constexpr const char *kKeyOk = "birth_ok";
static constexpr const char *kKeyY = "birth_y";
static constexpr const char *kKeyMo = "birth_mo";
static constexpr const char *kKeyD = "birth_d";
static constexpr const char *kKeyH = "birth_h";
static constexpr const char *kKeyMi = "birth_mi";
static constexpr const char *kKeyLat = "birth_lat";
static constexpr const char *kKeyLon = "birth_lon";
static constexpr const char *kKeyTz = "birth_tz";
static constexpr const char *kKeyPlace = "birth_place";

static bool birth_fields_sane(const PmBirthSpec *b) {
  return b && b->year >= 1900 && b->year <= 2100 && b->month >= 1 && b->month <= 12 && b->day >= 1 &&
         b->day <= 31 && b->hour <= 23 && b->minute <= 59;
}

bool pm_birth_load(PmBirthSpec *out) {
  if (!out) {
    return false;
  }
  memset(out, 0, sizeof(*out));
  Preferences pref;
  if (!pref.begin(kNvsNs, true)) {
    return false;
  }
  const bool ok = pref.getBool(kKeyOk, false);
  if (!ok) {
    pref.end();
    return false;
  }
  out->year = static_cast<uint16_t>(pref.getUShort(kKeyY, 0));
  out->month = pref.getUChar(kKeyMo, 0);
  out->day = pref.getUChar(kKeyD, 0);
  out->hour = pref.getUChar(kKeyH, 0);
  out->minute = pref.getUChar(kKeyMi, 0);
  out->lat_deg = pref.getFloat(kKeyLat, 0.f);
  out->lon_deg = pref.getFloat(kKeyLon, 0.f);
  out->tz_offset_sec = pref.getInt(kKeyTz, 0);
  {
    const String place = pref.getString(kKeyPlace, "");
    strncpy(out->place, place.c_str(), sizeof(out->place) - 1);
    out->place[sizeof(out->place) - 1] = '\0';
  }
  pref.end();
  if (!birth_fields_sane(out)) {
    out->valid = false;
    return false;
  }
  out->valid = true;
  return true;
}

void pm_birth_save(const PmBirthSpec *in) {
  if (!in || !in->valid || !birth_fields_sane(in)) {
    return;
  }
  Preferences pref;
  if (!pref.begin(kNvsNs, false)) {
    return;
  }
  pref.putBool(kKeyOk, true);
  pref.putUShort(kKeyY, in->year);
  pref.putUChar(kKeyMo, in->month);
  pref.putUChar(kKeyD, in->day);
  pref.putUChar(kKeyH, in->hour);
  pref.putUChar(kKeyMi, in->minute);
  pref.putFloat(kKeyLat, in->lat_deg);
  pref.putFloat(kKeyLon, in->lon_deg);
  pref.putInt(kKeyTz, in->tz_offset_sec);
  pref.putString(kKeyPlace, in->place);
  pref.end();
}

void pm_birth_clear(void) {
  Preferences pref;
  if (!pref.begin(kNvsNs, false)) {
    return;
  }
  pref.putBool(kKeyOk, false);
  pref.end();
}

bool pm_birth_to_utc_epoch(const PmBirthSpec *birth, time_t *utc_out) {
  if (!birth || !birth->valid || !utc_out || !birth_fields_sane(birth)) {
    return false;
  }
  struct tm civil = {};
  civil.tm_year = static_cast<int>(birth->year) - 1900;
  civil.tm_mon = static_cast<int>(birth->month) - 1;
  civil.tm_mday = static_cast<int>(birth->day);
  civil.tm_hour = static_cast<int>(birth->hour);
  civil.tm_min = static_cast<int>(birth->minute);
  civil.tm_sec = 0;
  civil.tm_isdst = 0;

  setenv("TZ", "UTC0", 1);
  tzset();
  const time_t civil_as_utc = mktime(&civil);
  if (civil_as_utc == static_cast<time_t>(-1)) {
    return false;
  }
  *utc_out = civil_as_utc - static_cast<time_t>(birth->tz_offset_sec);
  return true;
}

void pm_birth_ensure_demo(void) {
  PmBirthSpec existing = {};
  if (pm_birth_load(&existing)) {
    return;
  }
  PmBirthSpec demo = {};
  demo.year = 1972;
  demo.month = 5;
  demo.day = 6;
  demo.hour = 11;
  demo.minute = 30;
  demo.lat_deg = 30.4383f;
  demo.lon_deg = -84.2807f;
  /** US Eastern Daylight Time on 1972-05-06 (Tallahassee, FL). */
  demo.tz_offset_sec = -4 * 3600;
  strncpy(demo.place, "Tallahassee, FL", sizeof(demo.place) - 1);
  demo.valid = true;
  pm_birth_save(&demo);
}
