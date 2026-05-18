#include "pm_rhythms.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "pm_birth_nvs.h"
#include "pm_transit.h"
#include "pm_wifi_ntp.h"

static PmRhythmsDailyCard s_card = {};

struct BodyTone {
  const char *name;
  const char *title_word;
  const char *theme;
  const char *guidance;
  const char *ritual;
};

static const BodyTone kBodyTone[kPmBodyCount] = {
    {"Sun", "Clear Fire", "clarity, visibility, and choosing the center",
     "Let one visible priority receive your full warmth before scattering attention.",
     "Stand in the light for one breath and name the work that matters."},
    {"Moon", "Listening Tide", "feeling, memory, and daily rhythm",
     "Treat mood as weather: useful to notice, not a verdict on the whole sky.",
     "Touch water, tea, or breath before answering the next demand."},
    {"Mercury", "Bright Thread", "messages, pattern, and nimble repair",
     "Make the next exchange smaller, clearer, and kinder than the first draft.",
     "Write one sentence that turns noise into a useful signal."},
    {"Venus", "Soft Compass", "attraction, value, and relational grace",
     "Choose the beautiful constraint: enough ease to stay open, enough form to stay true.",
     "Place one pleasant thing where you will actually see it."},
    {"Mars", "Clean Spark", "courage, boundary, and directed motion",
     "Spend effort where it can move something real; let the rest stop borrowing heat.",
     "Take a short walk and decide what deserves your yes."},
    {"Jupiter", "Wide Gate", "meaning, trust, and generous scale",
     "Let the day be larger than the problem, then make one practical invitation.",
     "Offer help, gratitude, or a question that opens the room."},
    {"Saturn", "True Stone", "structure, patience, and honest limits",
     "A clean boundary can be devotional; make the container before adding more.",
     "Remove one obligation from your mental desk and schedule the real next step."},
};

static const char *const kSignName[12] = {"Aries",       "Taurus",    "Gemini", "Cancer",
                                          "Leo",         "Virgo",     "Libra",  "Scorpio",
                                          "Sagittarius", "Capricorn", "Aquarius", "Pisces"};

static const char *const kSignPhrase[12] = {
    "beginning before the map is complete",     "making the body feel safe enough to continue",
    "following the conversation between facts", "protecting what is tender and alive",
    "letting play restore courage",             "sorting details until care becomes visible",
    "balancing truth with proportion",          "staying present with what is transforming",
    "looking for the horizon inside the task",  "honoring the climb one durable step at a time",
    "giving the strange idea a humane shape",   "softening the edge so intuition can speak"};

static const char *copy_text(char *dst, size_t cap, const char *src) {
  if (!dst || cap == 0) {
    return dst;
  }
  if (!src) {
    src = "";
  }
  strncpy(dst, src, cap - 1);
  dst[cap - 1] = '\0';
  return dst;
}

static double rev360(double v) {
  v = fmod(v, 360.0);
  if (v < 0.0) {
    v += 360.0;
  }
  return v;
}

static int sign_index(double lon_deg) {
  const int idx = static_cast<int>(rev360(lon_deg) / 30.0);
  return idx < 0 ? 0 : (idx > 11 ? 11 : idx);
}

static double angle_sep(double a, double b) {
  double d = fabs(rev360(a) - rev360(b));
  if (d > 180.0) {
    d = 360.0 - d;
  }
  return d;
}

static int local_date_key(const struct tm *loc) {
  return (loc->tm_year + 1900) * 10000 + (loc->tm_mon + 1) * 100 + loc->tm_mday;
}

static const char *phase_name(double elong_deg) {
  int oct = static_cast<int>(rev360(elong_deg) / 45.0) % 8;
  if (oct < 0) {
    oct += 8;
  }
  static const char *const k[] = {"New Moon",      "Waxing Crescent", "First Quarter", "Waxing Gibbous",
                                  "Full Moon",     "Waning Gibbous",  "Last Quarter",  "Waning Crescent"};
  return k[oct];
}

void pm_rhythms_invalidate_daily_card(void) {
  memset(&s_card, 0, sizeof(s_card));
}

const PmRhythmsDailyCard *pm_rhythms_daily_card_cached(void) {
  return s_card.valid ? &s_card : nullptr;
}

bool pm_rhythms_has_cached_daily_card(void) {
  return s_card.valid;
}

bool pm_rhythms_prefetch_daily_card(void) {
  if (!pm_time_valid()) {
    return false;
  }

  struct tm loc = {};
  struct tm utc = {};
  pm_time_local(&loc);
  pm_time_utc(&utc);
  const int key = local_date_key(&loc);
  if (s_card.valid && s_card.date_key == key) {
    return true;
  }

  PmTransitPositions transits = {};
  pm_transit_compute_utc(&utc, &transits);
  if (!transits.ok) {
    return false;
  }

  PmBirthSpec birth = {};
  const bool have_birth = pm_birth_load(&birth) && birth.valid;
  double natal_sun = 0.0;
  const bool have_natal_sun = have_birth && pm_transit_natal_sun_lon(&birth, &natal_sun);

  const double moon_el = rev360(transits.lon[kPmBodyMoon] - transits.lon[kPmBodySun]);
  const float moon_illum = 0.5f * (1.0f - cosf(static_cast<float>(moon_el * (M_PI / 180.0))));
  const bool moon_waxing = moon_el < 180.0;
  const int moon_sign = sign_index(transits.lon[kPmBodyMoon]);
  const int focus_body =
      (loc.tm_yday + moon_sign + (have_birth ? static_cast<int>(birth.day) : 3)) % kPmBodyCount;
  const int focus_sign = sign_index(transits.lon[focus_body]);
  const BodyTone &tone = kBodyTone[focus_body];
  const char *sign = kSignName[focus_sign];

  PmRhythmsDailyCard next = {};
  next.valid = true;
  next.date_key = key;
  next.generated_at = time(nullptr);
  next.focus_body = static_cast<uint8_t>(focus_body);
  next.focus_sign = static_cast<uint8_t>(focus_sign);
  next.moon_percent = static_cast<uint8_t>(lrintf(moon_illum * 100.0f));
  next.moon_waxing = moon_waxing;
  snprintf(next.date_label, sizeof(next.date_label), "%04d-%02d-%02d", loc.tm_year + 1900, loc.tm_mon + 1,
           loc.tm_mday);
  snprintf(next.title, sizeof(next.title), "%s in %s", tone.title_word, sign);
  snprintf(next.primary_symbol, sizeof(next.primary_symbol), "%s %s", tone.name, sign);
  snprintf(next.secondary_symbol, sizeof(next.secondary_symbol), "%s %u%% %s", phase_name(moon_el),
           static_cast<unsigned>(next.moon_percent), moon_waxing ? "waxing" : "waning");
  snprintf(next.theme, sizeof(next.theme), "%s; %s.", tone.theme, kSignPhrase[focus_sign]);

  if (have_natal_sun) {
    const double sep = angle_sep(transits.lon[focus_body], natal_sun);
    const char *aspect = "background";
    if (sep <= 10.0) {
      aspect = "conjunct";
    } else if (fabs(sep - 60.0) <= 8.0) {
      aspect = "sextile";
    } else if (fabs(sep - 90.0) <= 8.0) {
      aspect = "square";
    } else if (fabs(sep - 120.0) <= 8.0) {
      aspect = "trine";
    } else if (fabs(sep - 180.0) <= 10.0) {
      aspect = "opposite";
    }
    snprintf(next.guidance, sizeof(next.guidance), "%s Natal Sun contact: %s (~%.0f deg).", tone.guidance, aspect,
             sep);
    copy_text(next.precision, sizeof(next.precision),
              "Local ephemeris card; natal Sun only, houses/aspects approximate.");
  } else {
    copy_text(next.guidance, sizeof(next.guidance), tone.guidance);
    copy_text(next.precision, sizeof(next.precision),
              "General card: stored birth data missing; using current sky and lunar rhythm.");
  }
  copy_text(next.ritual, sizeof(next.ritual), tone.ritual);

  s_card = next;
  return true;
}

bool pm_rhythms_build_tts_message(char *buf, size_t cap) {
  if (!buf || cap < 200) {
    return false;
  }
  if (!s_card.valid && !pm_rhythms_prefetch_daily_card()) {
    return false;
  }
  const PmRhythmsDailyCard *c = pm_rhythms_daily_card_cached();
  if (!c) {
    return false;
  }
  const int n = snprintf(buf, cap,
                         "Castalian Rhythms daily card for %s. Title: %s. Theme: %s Guidance: %s "
                         "Practice: %s Symbols: %s; %s. Precision: %s",
                         c->date_label, c->title, c->theme, c->guidance, c->ritual, c->primary_symbol,
                         c->secondary_symbol, c->precision);
  return n > 0 && static_cast<size_t>(n) < cap;
}

bool pm_rhythms_build_tts_system_prompt(char *buf, size_t cap) {
  if (!buf || cap < 32) {
    return false;
  }
  static const char kPrompt[] =
      "You are the BOOT narrator for a tiny round Astrolabe watch. The user message is an already-built "
      "Castalian Rhythms daily card. Read only that card in 4-6 short spoken sentences. Do not invent "
      "extra chart details, do not mention raw longitudes, and do not add medical, legal, or financial advice.";
  copy_text(buf, cap, kPrompt);
  return buf[0] != '\0';
}
