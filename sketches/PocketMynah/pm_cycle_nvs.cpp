#include "pm_cycle_nvs.h"

#include <Preferences.h>
#include <stdlib.h>
#include <string.h>

static constexpr const char *kNvsNs = "mynah";
static constexpr const char *kKeyOk = "cyc_ok";
static constexpr const char *kKeyY = "cyc_y";
static constexpr const char *kKeyMo = "cyc_mo";
static constexpr const char *kKeyD = "cyc_d";
static constexpr const char *kKeyLen = "cyc_len";
static constexpr const char *kKeyPeriod = "cyc_per";

static uint8_t clamp_cycle_len(uint8_t days) {
  if (days < PM_CYCLE_MIN_LENGTH_DAYS) {
    return PM_CYCLE_DEFAULT_LENGTH_DAYS;
  }
  if (days > PM_CYCLE_MAX_LENGTH_DAYS) {
    return PM_CYCLE_DEFAULT_LENGTH_DAYS;
  }
  return days;
}

static uint8_t clamp_period_len(uint8_t days, uint8_t cycle_len) {
  if (days < 1 || days > 10 || days >= cycle_len) {
    return PM_CYCLE_DEFAULT_PERIOD_DAYS;
  }
  return days;
}

static bool leap_year(uint16_t y) {
  return (y % 4u == 0u && y % 100u != 0u) || (y % 400u == 0u);
}

bool pm_cycle_ymd_sane(uint16_t year, uint8_t month, uint8_t day) {
  if (year < 2000 || year > 2100 || month < 1 || month > 12 || day < 1) {
    return false;
  }
  static const uint8_t kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  uint8_t max_day = kDays[month - 1];
  if (month == 2 && leap_year(year)) {
    max_day = 29;
  }
  return day <= max_day;
}

static int32_t days_from_civil(uint16_t year, uint8_t month, uint8_t day) {
  int y = static_cast<int>(year);
  const unsigned m = static_cast<unsigned>(month);
  const unsigned d = static_cast<unsigned>(day);
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned mp = m + (m > 2 ? -3u : 9u);
  const unsigned doy = (153u * mp + 2u) / 5u + d - 1u;
  const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
  return era * 146097 + static_cast<int>(doe) - 719468;
}

int32_t pm_cycle_days_between(uint16_t start_year, uint8_t start_month, uint8_t start_day,
                              uint16_t end_year, uint8_t end_month, uint8_t end_day) {
  if (!pm_cycle_ymd_sane(start_year, start_month, start_day) ||
      !pm_cycle_ymd_sane(end_year, end_month, end_day)) {
    return INT32_MIN;
  }
  return days_from_civil(end_year, end_month, end_day) -
         days_from_civil(start_year, start_month, start_day);
}

bool pm_cycle_load(PmCycleProfile *out) {
  if (!out) {
    return false;
  }
  memset(out, 0, sizeof(*out));
  out->cycle_length_days = PM_CYCLE_DEFAULT_LENGTH_DAYS;
  out->period_length_days = PM_CYCLE_DEFAULT_PERIOD_DAYS;

  Preferences pref;
  if (!pref.begin(kNvsNs, true)) {
    return false;
  }
  out->cycle_length_days = clamp_cycle_len(pref.getUChar(kKeyLen, PM_CYCLE_DEFAULT_LENGTH_DAYS));
  out->period_length_days =
      clamp_period_len(pref.getUChar(kKeyPeriod, PM_CYCLE_DEFAULT_PERIOD_DAYS), out->cycle_length_days);

  const bool ok = pref.getBool(kKeyOk, false);
  out->last_period_year = static_cast<uint16_t>(pref.getUShort(kKeyY, 0));
  out->last_period_month = pref.getUChar(kKeyMo, 0);
  out->last_period_day = pref.getUChar(kKeyD, 0);
  pref.end();

  out->has_last_period = ok && pm_cycle_ymd_sane(out->last_period_year, out->last_period_month,
                                                 out->last_period_day);
  if (!out->has_last_period) {
    out->last_period_year = 0;
    out->last_period_month = 0;
    out->last_period_day = 0;
  }
  return true;
}

void pm_cycle_save(const PmCycleProfile *in) {
  if (!in) {
    return;
  }
  const uint8_t cycle_len = clamp_cycle_len(in->cycle_length_days);
  const uint8_t period_len = clamp_period_len(in->period_length_days, cycle_len);
  const bool has_last = in->has_last_period && pm_cycle_ymd_sane(in->last_period_year,
                                                                 in->last_period_month,
                                                                 in->last_period_day);

  Preferences pref;
  if (!pref.begin(kNvsNs, false)) {
    return;
  }
  pref.putUChar(kKeyLen, cycle_len);
  pref.putUChar(kKeyPeriod, period_len);
  pref.putBool(kKeyOk, has_last);
  if (has_last) {
    pref.putUShort(kKeyY, in->last_period_year);
    pref.putUChar(kKeyMo, in->last_period_month);
    pref.putUChar(kKeyD, in->last_period_day);
  }
  pref.end();
}

void pm_cycle_clear(void) {
  PmCycleProfile p = {};
  p.cycle_length_days = PM_CYCLE_DEFAULT_LENGTH_DAYS;
  p.period_length_days = PM_CYCLE_DEFAULT_PERIOD_DAYS;
  p.has_last_period = false;
  pm_cycle_save(&p);
}

bool pm_cycle_set_last_period(uint16_t year, uint8_t month, uint8_t day) {
  if (!pm_cycle_ymd_sane(year, month, day)) {
    return false;
  }
  PmCycleProfile p = {};
  (void)pm_cycle_load(&p);
  p.last_period_year = year;
  p.last_period_month = month;
  p.last_period_day = day;
  p.has_last_period = true;
  pm_cycle_save(&p);
  return true;
}

bool pm_cycle_log_period_started_today(const struct tm *local_tm) {
  if (!local_tm) {
    return false;
  }
  return pm_cycle_set_last_period(static_cast<uint16_t>(local_tm->tm_year + 1900),
                                  static_cast<uint8_t>(local_tm->tm_mon + 1),
                                  static_cast<uint8_t>(local_tm->tm_mday));
}

bool pm_cycle_set_cycle_length_days(uint8_t days) {
  if (days < PM_CYCLE_MIN_LENGTH_DAYS || days > PM_CYCLE_MAX_LENGTH_DAYS) {
    return false;
  }
  PmCycleProfile p = {};
  (void)pm_cycle_load(&p);
  p.cycle_length_days = days;
  if (p.period_length_days >= p.cycle_length_days) {
    p.period_length_days = PM_CYCLE_DEFAULT_PERIOD_DAYS;
  }
  pm_cycle_save(&p);
  return true;
}

bool pm_cycle_set_period_length_days(uint8_t days) {
  PmCycleProfile p = {};
  (void)pm_cycle_load(&p);
  if (days < 1 || days > 10 || days >= p.cycle_length_days) {
    return false;
  }
  p.period_length_days = days;
  pm_cycle_save(&p);
  return true;
}

uint8_t pm_cycle_adjust_cycle_length_preset(int delta) {
  static const uint8_t kPresets[] = {24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35};
  PmCycleProfile p = {};
  (void)pm_cycle_load(&p);
  int idx = 0;
  int best_dist = 100;
  for (unsigned i = 0; i < sizeof(kPresets) / sizeof(kPresets[0]); ++i) {
    const int dist = abs(static_cast<int>(kPresets[i]) - static_cast<int>(p.cycle_length_days));
    if (dist < best_dist) {
      best_dist = dist;
      idx = static_cast<int>(i);
    }
  }
  const int count = static_cast<int>(sizeof(kPresets) / sizeof(kPresets[0]));
  idx = (idx + delta) % count;
  if (idx < 0) {
    idx += count;
  }
  p.cycle_length_days = kPresets[idx];
  if (p.period_length_days >= p.cycle_length_days) {
    p.period_length_days = PM_CYCLE_DEFAULT_PERIOD_DAYS;
  }
  pm_cycle_save(&p);
  return p.cycle_length_days;
}

int32_t pm_cycle_day_index_for_date(const PmCycleProfile *profile, uint16_t year, uint8_t month, uint8_t day) {
  if (!profile || !profile->has_last_period || profile->cycle_length_days < 1) {
    return -1;
  }
  const int32_t diff = pm_cycle_days_between(profile->last_period_year, profile->last_period_month,
                                             profile->last_period_day, year, month, day);
  if (diff < 0) {
    return -1;
  }
  return diff % static_cast<int32_t>(profile->cycle_length_days);
}
