#include "pm_rhythms.h"

#include <Arduino.h>
#include <Preferences.h>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "pm_birth_nvs.h"
#include "pm_wifi_ntp.h"

static constexpr const char *kNvsNs = "mynah";
static constexpr const char *kKeyCycleEnabled = "cycle_enabled";
static constexpr const char *kKeyLastPeriodStart = "last_period_start";

static constexpr const char *kKeySchema = "rh_schema";
static constexpr const char *kKeyYmd = "rh_ymd";
static constexpr const char *kKeyTitle = "rh_title";
static constexpr const char *kKeyFavor = "rh_favor";
static constexpr const char *kKeyWatch = "rh_watch";
static constexpr const char *kKeyPractice = "rh_practice";
static constexpr const char *kKeySymbols = "rh_symbols";
static constexpr const char *kKeyPrecision = "rh_precision";

struct BodyKb {
  const char *name;
  const char *favor;
  const char *watch;
  const char *practice;
  const char *safety;
  int weight;
};

struct AspectKb {
  int deg;
  const char *name;
  const char *favor;
  const char *watch;
  const char *practice;
  const char *safety;
  int weight;
};

struct SignKb {
  const char *name;
  const char *tone;
  const char *practice;
};

struct CycleKb {
  const char *name;
  const char *favor;
  const char *watch;
  const char *practice;
  const char *safety;
  int weight;
};

static const BodyKb kBodyKb[kPmBodyCount] = {
    {"Sun", "clear priorities and visible choices", "performing certainty before it is earned",
     "name one honest yes and give it ten quiet minutes", "Keep this reflective, not deterministic.", 18},
    {"Moon", "felt needs, home rhythms, and gentler timing", "reacting from yesterday's weather",
     "take three breaths before answering the next request", "Do not turn moods into fate.", 20},
    {"Mercury", "messages, notes, and useful questions", "rushing from hunch to conclusion",
     "write the question before solving it", "Avoid treating interpretations as facts.", 14},
    {"Venus", "beauty, repair, and warm reciprocity", "overpromising to keep the peace",
     "make one small exchange more gracious", "Consent and boundaries stay primary.", 14},
    {"Mars", "clean action and embodied courage", "forcing a door that needs a hinge",
     "move for two minutes, then choose the next step", "Avoid reckless or coercive advice.", 16},
    {"Jupiter", "perspective, generosity, and wiser scale", "inflating a hope past the evidence",
     "ask what becomes easier if the frame widens", "Do not present luck as a guarantee.", 15},
    {"Saturn", "structure, patience, and good limits", "mistaking pressure for truth",
     "remove one avoidable friction point", "No medical, legal, or financial directives.", 17},
};

static const AspectKb kAspectKb[] = {
    {0, "conjunct", "integration and direct attention", "identifying with one signal too tightly",
     "put the strongest feeling in one plain sentence", "Hold intensity lightly.", 44},
    {60, "sextile", "small openings and cooperative timing", "waiting for permission from the sky",
     "accept the easiest useful invitation", "Opportunity still needs choice.", 31},
    {90, "square", "honest friction and skill-building", "turning tension into urgency",
     "choose the smallest repairable edge", "Do not escalate conflict for symbolism.", 39},
    {120, "trine", "ease, flow, and receptive practice", "sleepwalking through something helpful",
     "let the simple support be simple", "Ease is not proof of destiny.", 35},
    {180, "opposes", "perspective across a polarity", "projecting the whole problem outward",
     "name both sides before choosing", "Avoid fatalistic relationship claims.", 40},
};

static const SignKb kSignKb[12] = {
    {"Aries", "direct", "begin before polishing"},
    {"Taurus", "steady", "return to the body and the useful object"},
    {"Gemini", "curious", "ask one better question"},
    {"Cancer", "protective", "tend the room you are in"},
    {"Leo", "expressive", "make the generous gesture visible"},
    {"Virgo", "discerning", "simplify the next step"},
    {"Libra", "relational", "restore proportion before deciding"},
    {"Scorpio", "truth-seeking", "notice what has charge without forcing it open"},
    {"Sagittarius", "wide-angle", "step back until meaning has room"},
    {"Capricorn", "practical", "choose the durable boundary"},
    {"Aquarius", "pattern-aware", "test the humane alternative"},
    {"Pisces", "porous", "soften the edge and keep a ground cord"},
};

static const char *const kMoonPhaseName[8] = {"New",        "Waxing crescent", "First quarter",
                                              "Waxing gibbous", "Full", "Waning gibbous",
                                              "Last quarter",   "Waning crescent"};
static const char *const kMoonPhaseFavor[8] = {
    "quiet starts and clean intentions",       "experiments that can stay small",
    "a decision that creates momentum",        "refinement before the reveal",
    "honest reflection and visible feeling",   "sharing what has ripened",
    "editing, composting, and course-correction", "rest, closure, and dream logic"};
static const char *const kMoonPhaseWatch[8] = {
    "needing proof before a seed can be planted", "scattering attention across too many starts",
    "mistaking pressure for the only path",       "over-tuning what is ready enough",
    "calling every feeling a final verdict",      "giving away more than the moment asks",
    "cutting away what only needed revision",     "drifting so far inward that basics slip"};
static const char *const kMoonPhasePractice[8] = {
    "write one sentence of intent",       "give a small promise a real container",
    "pick the action that clarifies",     "polish one useful detail",
    "say what is true without making it forever", "share the harvest and keep a boundary",
    "remove one stale obligation",        "close the loop and sleep on the rest"};

static const CycleKb kCycleKb[] = {
    {"menstrual", "rest, warmth, and lower social load", "treating low power as low worth",
     "make the next obligation smaller if you can", "Cycle notes are body-awareness prompts, not medical advice.", 58},
    {"follicular", "fresh inputs, planning, and gentle acceleration", "saying yes to every spark",
     "choose one seed to feed", "Cycle timing varies; listen to the body first.", 49},
    {"ovulatory", "connection, visibility, and decisive asks", "performing availability past your boundary",
     "make the clear ask and keep the clear no", "No fertility or health prediction is implied.", 54},
    {"luteal", "editing, boundaries, and finishing well", "treating irritation as the whole truth",
     "clear one snag before it grows teeth", "Seek care for medical concerns; this is reflective only.", 51},
};

static double norm360(double x) {
  x = fmod(x, 360.0);
  if (x < 0.0) {
    x += 360.0;
  }
  return x;
}

static double aspect_distance(double a, double b) {
  double d = fabs(norm360(a) - norm360(b));
  if (d > 180.0) {
    d = 360.0 - d;
  }
  return d;
}

static int sign_index(double lon) {
  return static_cast<int>(norm360(lon) / 30.0) % 12;
}

static uint32_t ymd_from_tm(const struct tm *t) {
  if (!t) {
    return 0;
  }
  return static_cast<uint32_t>((t->tm_year + 1900) * 10000 + (t->tm_mon + 1) * 100 + t->tm_mday);
}

static bool split_ymd(uint32_t ymd, int *y, int *m, int *d) {
  const int yy = static_cast<int>(ymd / 10000u);
  const int mm = static_cast<int>((ymd / 100u) % 100u);
  const int dd = static_cast<int>(ymd % 100u);
  if (yy < 1900 || yy > 2100 || mm < 1 || mm > 12 || dd < 1 || dd > 31) {
    return false;
  }
  if (y) {
    *y = yy;
  }
  if (m) {
    *m = mm;
  }
  if (d) {
    *d = dd;
  }
  return true;
}

static int32_t days_from_civil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned mp = static_cast<unsigned>(static_cast<int>(m) + (m > 2 ? -3 : 9));
  const unsigned doy = (153u * mp + 2u) / 5u + d - 1u;
  const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
  return era * 146097 + static_cast<int>(doe) - 719468;
}

static bool ymd_days(uint32_t ymd, int32_t *days_out) {
  int y = 0, m = 0, d = 0;
  if (!days_out || !split_ymd(ymd, &y, &m, &d)) {
    return false;
  }
  *days_out = days_from_civil(y, static_cast<unsigned>(m), static_cast<unsigned>(d));
  return true;
}

static void copy_cstr(char *dst, size_t cap, const char *src) {
  if (!dst || cap == 0) {
    return;
  }
  if (!src) {
    src = "";
  }
  strncpy(dst, src, cap - 1);
  dst[cap - 1] = '\0';
}

static bool pref_get_cstr(Preferences &pref, const char *key, char *dst, size_t cap) {
  if (!dst || cap == 0) {
    return false;
  }
  const String s = pref.getString(key, "");
  copy_cstr(dst, cap, s.c_str());
  return s.length() > 0;
}

static const AspectKb *aspect_kb(int deg) {
  for (const auto &a : kAspectKb) {
    if (a.deg == deg) {
      return &a;
    }
  }
  return &kAspectKb[0];
}

static void insert_signal(PmRhythmsSignal *signals, size_t cap, size_t *count, const PmRhythmsSignal *sig) {
  if (!signals || !count || !sig || cap == 0) {
    return;
  }
  size_t n = *count;
  if (n >= cap && sig->score <= signals[cap - 1].score) {
    return;
  }
  size_t ins = n < cap ? n : cap - 1;
  while (ins > 0 && sig->score > signals[ins - 1].score) {
    if (ins < cap) {
      signals[ins] = signals[ins - 1];
    }
    --ins;
  }
  if (ins < cap) {
    signals[ins] = *sig;
  }
  if (n < cap) {
    *count = n + 1;
  }
}

static void add_transit_aspects(const PmTransitPositions *now, const PmTransitPositions *natal,
                                PmRhythmsSignal *signals, size_t cap, size_t *count) {
  if (!now || !now->ok || !natal || !natal->ok) {
    return;
  }
  for (int bi = 0; bi < kPmBodyCount; ++bi) {
    const double max_orb = (bi == kPmBodyMoon) ? 5.0 : ((bi == kPmBodyJupiter || bi == kPmBodySaturn) ? 4.2 : 6.0);
    for (int ni = 0; ni < kPmBodyCount; ++ni) {
      const double sep = aspect_distance(now->lon[bi], natal->lon[ni]);
      for (const auto &ak : kAspectKb) {
        const double orb = fabs(sep - static_cast<double>(ak.deg));
        if (orb > max_orb) {
          continue;
        }
        PmRhythmsSignal sig = {};
        sig.kind = kPmRhythmsSignalTransitAspect;
        sig.body = static_cast<int8_t>(bi);
        sig.natal_body = static_cast<int8_t>(ni);
        sig.sign = static_cast<int8_t>(sign_index(now->lon[bi]));
        sig.aspect_deg = static_cast<int16_t>(ak.deg);
        sig.orb_deg = static_cast<float>(orb);
        sig.score = static_cast<int16_t>(ak.weight + kBodyKb[bi].weight + (kBodyKb[ni].weight / 2) -
                                         static_cast<int>(orb * 7.5));
        copy_cstr(sig.tag, sizeof(sig.tag), "transit");
        snprintf(sig.label, sizeof(sig.label), "%s %s natal %s", kBodyKb[bi].name, ak.name,
                 kBodyKb[ni].name);
        copy_cstr(sig.safety, sizeof(sig.safety), ak.safety);
        insert_signal(signals, cap, count, &sig);
      }
    }
  }
}

static void add_lunar_signals(const PmTransitPositions *now, PmRhythmsSignal *signals, size_t cap,
                              size_t *count) {
  if (!now || !now->ok) {
    return;
  }
  const double phase = norm360(now->lon[kPmBodyMoon] - now->lon[kPmBodySun]);
  const int phase_idx = static_cast<int>(floor((phase + 22.5) / 45.0)) % 8;
  const double exact = static_cast<double>(phase_idx) * 45.0;
  const double closeness = 22.5 - aspect_distance(phase, exact);
  PmRhythmsSignal phase_sig = {};
  phase_sig.kind = kPmRhythmsSignalLunarPhase;
  phase_sig.body = kPmBodyMoon;
  phase_sig.natal_body = -1;
  phase_sig.sign = static_cast<int8_t>(sign_index(now->lon[kPmBodyMoon]));
  phase_sig.aspect_deg = static_cast<int16_t>(phase_idx);
  phase_sig.orb_deg = static_cast<float>(22.5 - closeness);
  phase_sig.score = static_cast<int16_t>(46 + static_cast<int>(closeness));
  copy_cstr(phase_sig.tag, sizeof(phase_sig.tag), "moon_phase");
  snprintf(phase_sig.label, sizeof(phase_sig.label), "%s Moon", kMoonPhaseName[phase_idx]);
  copy_cstr(phase_sig.safety, sizeof(phase_sig.safety), "Moods are weather, not verdicts.");
  insert_signal(signals, cap, count, &phase_sig);

  PmRhythmsSignal sign_sig = {};
  sign_sig.kind = kPmRhythmsSignalMoonSign;
  sign_sig.body = kPmBodyMoon;
  sign_sig.natal_body = -1;
  sign_sig.sign = static_cast<int8_t>(sign_index(now->lon[kPmBodyMoon]));
  sign_sig.aspect_deg = -1;
  sign_sig.score = 36;
  copy_cstr(sign_sig.tag, sizeof(sign_sig.tag), "moon_sign");
  snprintf(sign_sig.label, sizeof(sign_sig.label), "Moon in %s", kSignKb[sign_sig.sign].name);
  copy_cstr(sign_sig.safety, sizeof(sign_sig.safety), "Use lunar tone as a prompt, not a command.");
  insert_signal(signals, cap, count, &sign_sig);
}

static void add_sun_season_signal(const PmTransitPositions *now, PmRhythmsSignal *signals, size_t cap,
                                  size_t *count) {
  if (!now || !now->ok) {
    return;
  }
  PmRhythmsSignal sig = {};
  sig.kind = kPmRhythmsSignalSunSeason;
  sig.body = kPmBodySun;
  sig.natal_body = -1;
  sig.sign = static_cast<int8_t>(sign_index(now->lon[kPmBodySun]));
  sig.aspect_deg = -1;
  sig.score = 28;
  copy_cstr(sig.tag, sizeof(sig.tag), "sun_season");
  snprintf(sig.label, sizeof(sig.label), "Sun in %s", kSignKb[sig.sign].name);
  copy_cstr(sig.safety, sizeof(sig.safety), "Seasonal tone is background, not fate.");
  insert_signal(signals, cap, count, &sig);
}

static const CycleKb *cycle_kb_for_day(int cycle_day) {
  if (cycle_day <= 5) {
    return &kCycleKb[0];
  }
  if (cycle_day <= 12) {
    return &kCycleKb[1];
  }
  if (cycle_day <= 16) {
    return &kCycleKb[2];
  }
  return &kCycleKb[3];
}

static void add_cycle_signal(const struct tm *local, PmRhythmsSignal *signals, size_t cap, size_t *count) {
  PmRhythmsCycleState cycle = {};
  if (!local || !pm_rhythms_cycle_load(&cycle) || !cycle.enabled || cycle.last_period_start_ymd == 0) {
    return;
  }
  int32_t today = 0;
  int32_t start = 0;
  if (!ymd_days(ymd_from_tm(local), &today) || !ymd_days(cycle.last_period_start_ymd, &start)) {
    return;
  }
  const int32_t diff = today - start;
  if (diff < 0 || diff > 366) {
    return;
  }
  const int cycle_day = static_cast<int>(diff % 28) + 1;
  const CycleKb *ck = cycle_kb_for_day(cycle_day);
  PmRhythmsSignal sig = {};
  sig.kind = kPmRhythmsSignalCycle;
  sig.body = -1;
  sig.natal_body = -1;
  sig.sign = -1;
  sig.aspect_deg = -1;
  sig.cycle_day = static_cast<int16_t>(cycle_day);
  sig.score = static_cast<int16_t>(ck->weight);
  copy_cstr(sig.tag, sizeof(sig.tag), "cycle");
  snprintf(sig.label, sizeof(sig.label), "Cycle day %d: %s", cycle_day, ck->name);
  copy_cstr(sig.safety, sizeof(sig.safety), ck->safety);
  insert_signal(signals, cap, count, &sig);
}

bool pm_rhythms_compute_signals(const struct tm *utc, const struct tm *local, PmRhythmsSignal *signals,
                                size_t cap, size_t *count_out) {
  if (!utc || !signals || cap == 0) {
    return false;
  }
  memset(signals, 0, sizeof(PmRhythmsSignal) * cap);
  size_t count = 0;
  PmTransitPositions now = {};
  pm_transit_compute_utc(utc, &now);
  if (!now.ok) {
    if (count_out) {
      *count_out = 0;
    }
    return false;
  }

  PmBirthSpec birth = {};
  PmTransitPositions natal = {};
  if (pm_birth_load(&birth)) {
    (void)pm_transit_birth_positions(&birth, &natal);
  }

  add_transit_aspects(&now, &natal, signals, cap, &count);
  add_lunar_signals(&now, signals, cap, &count);
  add_cycle_signal(local ? local : utc, signals, cap, &count);
  add_sun_season_signal(&now, signals, cap, &count);

  if (count_out) {
    *count_out = count;
  }
  return count > 0;
}

static void signal_symbols(const PmRhythmsSignal *sig, char *out, size_t cap) {
  if (!sig || !out || cap == 0) {
    return;
  }
  switch (sig->kind) {
    case kPmRhythmsSignalTransitAspect:
      snprintf(out, cap, "%s %s natal %s (%s)", pm_ephem_body_label(static_cast<PmEphemBody>(sig->body)),
               aspect_kb(sig->aspect_deg)->name, pm_ephem_body_label(static_cast<PmEphemBody>(sig->natal_body)),
               kSignKb[sig->sign].name);
      break;
    case kPmRhythmsSignalLunarPhase:
      snprintf(out, cap, "%s Moon in %s", kMoonPhaseName[sig->aspect_deg], kSignKb[sig->sign].name);
      break;
    case kPmRhythmsSignalMoonSign:
      snprintf(out, cap, "Moon in %s", kSignKb[sig->sign].name);
      break;
    case kPmRhythmsSignalSunSeason:
      snprintf(out, cap, "Sun in %s", kSignKb[sig->sign].name);
      break;
    case kPmRhythmsSignalCycle:
      snprintf(out, cap, "Cycle day %d", sig->cycle_day);
      break;
  }
}

static void compose_from_signal(const PmRhythmsSignal *sig, char *favor, size_t favor_cap, char *watch,
                                size_t watch_cap, char *practice, size_t practice_cap) {
  if (!sig) {
    return;
  }
  switch (sig->kind) {
    case kPmRhythmsSignalTransitAspect: {
      const AspectKb *ak = aspect_kb(sig->aspect_deg);
      const BodyKb *bk = &kBodyKb[sig->body];
      snprintf(favor, favor_cap, "Favor %s through %s.", bk->favor, ak->favor);
      snprintf(watch, watch_cap, "Watch for %s; %s", bk->watch, ak->safety);
      snprintf(practice, practice_cap, "Practice: %s; then %s.", ak->practice, bk->practice);
      break;
    }
    case kPmRhythmsSignalLunarPhase: {
      const int idx = sig->aspect_deg;
      snprintf(favor, favor_cap, "Favor %s.", kMoonPhaseFavor[idx]);
      snprintf(watch, watch_cap, "Watch for %s; %s", kMoonPhaseWatch[idx], sig->safety);
      snprintf(practice, practice_cap, "Practice: %s.", kMoonPhasePractice[idx]);
      break;
    }
    case kPmRhythmsSignalMoonSign:
      snprintf(favor, favor_cap, "Favor %s attention while the Moon is in %s.", kSignKb[sig->sign].tone,
               kSignKb[sig->sign].name);
      snprintf(watch, watch_cap, "Watch for over-reading a passing mood; %s", sig->safety);
      snprintf(practice, practice_cap, "Practice: %s.", kSignKb[sig->sign].practice);
      break;
    case kPmRhythmsSignalSunSeason:
      snprintf(favor, favor_cap, "Favor %s choices under the %s Sun.", kSignKb[sig->sign].tone,
               kSignKb[sig->sign].name);
      snprintf(watch, watch_cap, "Watch for making seasonal symbolism too literal; %s", sig->safety);
      snprintf(practice, practice_cap, "Practice: %s.", kSignKb[sig->sign].practice);
      break;
    case kPmRhythmsSignalCycle: {
      const CycleKb *ck = cycle_kb_for_day(sig->cycle_day);
      snprintf(favor, favor_cap, "Favor %s.", ck->favor);
      snprintf(watch, watch_cap, "Watch for %s; %s", ck->watch, ck->safety);
      snprintf(practice, practice_cap, "Practice: %s.", ck->practice);
      break;
    }
  }
}

static void build_precision(char *out, size_t cap) {
  PmBirthSpec birth = {};
  if (pm_birth_load(&birth)) {
    if (birth.place[0] != '\0' || fabs(birth.lat_deg) > 0.01f || fabs(birth.lon_deg) > 0.01f) {
      snprintf(out, cap, "Local ephemeris with NVS birth profile; houses remain approximate in V0.");
    } else {
      snprintf(out, cap, "Local ephemeris with birth time from NVS; birthplace/location may be approximate.");
    }
  } else {
    snprintf(out, cap, "Current-sky card only; add birth data for natal transit weighting.");
  }
}

bool pm_rhythms_build_compact_card(const struct tm *utc, const struct tm *local,
                                   PmRhythmsCompactCard *out) {
  if (!utc || !out) {
    return false;
  }
  const struct tm *lt = local ? local : utc;
  PmRhythmsSignal signals[kPmRhythmsMaxSignals] = {};
  size_t count = 0;
  if (!pm_rhythms_compute_signals(utc, lt, signals, kPmRhythmsMaxSignals, &count) || count == 0) {
    return false;
  }

  memset(out, 0, sizeof(*out));
  copy_cstr(out->schema, sizeof(out->schema), PM_RHYTHMS_CARD_SCHEMA);
  out->local_ymd = ymd_from_tm(lt);
  out->stale = false;

  const PmRhythmsSignal *top = &signals[0];
  copy_cstr(out->title, sizeof(out->title), top->label);
  compose_from_signal(top, out->favor, sizeof(out->favor), out->watch, sizeof(out->watch), out->practice,
                      sizeof(out->practice));
  signal_symbols(top, out->symbols, sizeof(out->symbols));

  if (count > 1 && strlen(out->favor) + strlen(signals[1].label) + 18 < sizeof(out->favor)) {
    const size_t off = strlen(out->favor);
    snprintf(out->favor + off, sizeof(out->favor) - off, " Also notice %s.", signals[1].label);
  }
  if (strlen(out->watch) + 32 < sizeof(out->watch)) {
    const size_t off = strlen(out->watch);
    snprintf(out->watch + off, sizeof(out->watch) - off, " Reflective, not fate.");
  }
  build_precision(out->precision, sizeof(out->precision));
  return true;
}

static bool cache_store(const PmRhythmsCompactCard *card) {
  if (!card || card->title[0] == '\0') {
    return false;
  }
  Preferences pref;
  if (!pref.begin(kNvsNs, false)) {
    return false;
  }
  pref.putString(kKeySchema, card->schema);
  pref.putUInt(kKeyYmd, card->local_ymd);
  pref.putString(kKeyTitle, card->title);
  pref.putString(kKeyFavor, card->favor);
  pref.putString(kKeyWatch, card->watch);
  pref.putString(kKeyPractice, card->practice);
  pref.putString(kKeySymbols, card->symbols);
  pref.putString(kKeyPrecision, card->precision);
  pref.end();
  return true;
}

static bool cache_load(PmRhythmsCompactCard *out) {
  if (!out) {
    return false;
  }
  memset(out, 0, sizeof(*out));
  Preferences pref;
  if (!pref.begin(kNvsNs, true)) {
    return false;
  }
  char schema[32] = {};
  const bool has_schema = pref_get_cstr(pref, kKeySchema, schema, sizeof(schema));
  if (!has_schema || strcmp(schema, PM_RHYTHMS_CARD_SCHEMA) != 0) {
    pref.end();
    return false;
  }
  copy_cstr(out->schema, sizeof(out->schema), schema);
  out->local_ymd = pref.getUInt(kKeyYmd, 0);
  const bool ok = pref_get_cstr(pref, kKeyTitle, out->title, sizeof(out->title)) &&
                  pref_get_cstr(pref, kKeyFavor, out->favor, sizeof(out->favor)) &&
                  pref_get_cstr(pref, kKeyWatch, out->watch, sizeof(out->watch)) &&
                  pref_get_cstr(pref, kKeyPractice, out->practice, sizeof(out->practice));
  (void)pref_get_cstr(pref, kKeySymbols, out->symbols, sizeof(out->symbols));
  (void)pref_get_cstr(pref, kKeyPrecision, out->precision, sizeof(out->precision));
  pref.end();
  return ok && out->local_ymd != 0;
}

bool pm_rhythms_build_compact_card(PmRhythmsCompactCard *out) {
  if (!out || !pm_time_valid()) {
    return false;
  }
  struct tm utc = {};
  struct tm local = {};
  pm_time_utc(&utc);
  pm_time_local(&local);
  return pm_rhythms_build_compact_card(&utc, &local, out);
}

bool pm_rhythms_get_compact_card(PmRhythmsCompactCard *out) {
  if (!out) {
    return false;
  }
  PmRhythmsCompactCard cached = {};
  const bool have_cache = cache_load(&cached);

  if (!pm_time_valid()) {
    if (!have_cache) {
      return false;
    }
    *out = cached;
    out->stale = true;
    copy_cstr(out->stale_ribbon, sizeof(out->stale_ribbon), "cached: set time");
    return true;
  }

  struct tm utc = {};
  struct tm local = {};
  pm_time_utc(&utc);
  pm_time_local(&local);
  const uint32_t today = ymd_from_tm(&local);
  if (have_cache && cached.local_ymd == today) {
    *out = cached;
    out->stale = false;
    out->stale_ribbon[0] = '\0';
    return true;
  }

  PmRhythmsCompactCard fresh = {};
  if (pm_rhythms_build_compact_card(&utc, &local, &fresh)) {
    (void)cache_store(&fresh);
    *out = fresh;
    return true;
  }

  if (have_cache) {
    *out = cached;
    out->stale = true;
    copy_cstr(out->stale_ribbon, sizeof(out->stale_ribbon), "cached daily card");
    return true;
  }
  return false;
}

bool pm_rhythms_cycle_load(PmRhythmsCycleState *out) {
  if (!out) {
    return false;
  }
  memset(out, 0, sizeof(*out));
  Preferences pref;
  if (!pref.begin(kNvsNs, true)) {
    return false;
  }
  out->enabled = pref.getBool(kKeyCycleEnabled, false);
  out->last_period_start_ymd = pref.getUInt(kKeyLastPeriodStart, 0);
  pref.end();
  return true;
}

void pm_rhythms_cycle_save(bool enabled, uint32_t last_period_start_ymd) {
  if (last_period_start_ymd != 0 && !split_ymd(last_period_start_ymd, nullptr, nullptr, nullptr)) {
    return;
  }
  Preferences pref;
  if (!pref.begin(kNvsNs, false)) {
    return;
  }
  pref.putBool(kKeyCycleEnabled, enabled);
  if (last_period_start_ymd != 0) {
    pref.putUInt(kKeyLastPeriodStart, last_period_start_ymd);
  }
  pref.end();
}

void pm_rhythms_cycle_clear(void) {
  Preferences pref;
  if (!pref.begin(kNvsNs, false)) {
    return;
  }
  pref.putBool(kKeyCycleEnabled, false);
  pref.remove(kKeyLastPeriodStart);
  pref.end();
}

void pm_rhythms_cache_clear(void) {
  Preferences pref;
  if (!pref.begin(kNvsNs, false)) {
    return;
  }
  pref.remove(kKeySchema);
  pref.remove(kKeyYmd);
  pref.remove(kKeyTitle);
  pref.remove(kKeyFavor);
  pref.remove(kKeyWatch);
  pref.remove(kKeyPractice);
  pref.remove(kKeySymbols);
  pref.remove(kKeyPrecision);
  pref.end();
}
