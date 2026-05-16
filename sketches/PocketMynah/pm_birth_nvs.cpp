#include "pm_birth_nvs.h"

#include <Preferences.h>

static constexpr const char *kNvsNs = "mynah";
static constexpr const char *kKeyOk = "birth_ok";
static constexpr const char *kKeyY = "birth_y";
static constexpr const char *kKeyMo = "birth_mo";
static constexpr const char *kKeyD = "birth_d";
static constexpr const char *kKeyH = "birth_h";
static constexpr const char *kKeyMi = "birth_mi";

bool pm_birth_load(PmBirthSpec *out) {
  if (!out) {
    return false;
  }
  out->valid = false;
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
  pref.end();
  if (out->year < 1900 || out->year > 2100 || out->month < 1 || out->month > 12 || out->day < 1 ||
      out->day > 31) {
    out->valid = false;
    return false;
  }
  out->valid = true;
  return true;
}

void pm_birth_save(const PmBirthSpec *in) {
  if (!in || !in->valid) {
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
