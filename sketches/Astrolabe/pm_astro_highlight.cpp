#include "pm_astro_highlight.h"

#include <ctype.h>
#include <string.h>

typedef struct {
  const char *needle;
  PmAstroMentionKind kind;
  uint8_t id;
} Keyword;

static void to_lower_copy(const char *in, char *out, size_t cap) {
  if (!in || !out || cap == 0) {
    return;
  }
  size_t i = 0;
  for (; in[i] != '\0' && i + 1 < cap; ++i) {
    out[i] = static_cast<char>(tolower(static_cast<unsigned char>(in[i])));
  }
  out[i] = '\0';
}

static bool is_word_char(char c) {
  return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
}

static bool word_boundary_before(const char *hay, const char *pos) {
  if (pos <= hay) {
    return true;
  }
  return !is_word_char(pos[-1]);
}

static bool word_boundary_after(const char *pos) {
  return pos[0] == '\0' || !is_word_char(pos[0]);
}

static void plan_push(PmAstroHighlightPlan *plan, PmAstroMentionKind kind, uint8_t id, size_t pos) {
  if (!plan || plan->count >= sizeof(plan->items) / sizeof(plan->items[0])) {
    return;
  }
  (void)pos;
  const PmAstroMention m = {kind, id};
  if (plan->count > 0) {
    const PmAstroMention &prev = plan->items[plan->count - 1];
    if (prev.kind == m.kind && prev.id == m.id) {
      return;
    }
  }
  plan->items[plan->count++] = m;
}

static void scan_keywords(const char *text, PmAstroHighlightPlan *plan) {
  static const Keyword k_words[] = {
      {"sagittarius", kPmAstroMentionSign, 8},
      {"capricorn", kPmAstroMentionSign, 9},
      {"aquarius", kPmAstroMentionSign, 10},
      {"scorpio", kPmAstroMentionSign, 7},
      {"gemini", kPmAstroMentionSign, 2},
      {"pisces", kPmAstroMentionSign, 11},
      {"taurus", kPmAstroMentionSign, 1},
      {"cancer", kPmAstroMentionSign, 3},
      {"virgo", kPmAstroMentionSign, 5},
      {"libra", kPmAstroMentionSign, 6},
      {"aries", kPmAstroMentionSign, 0},
      {"leo", kPmAstroMentionSign, 4},
      {"jupiter", kPmAstroMentionBody, kPmBodyJupiter},
      {"saturn", kPmAstroMentionBody, kPmBodySaturn},
      {"mercury", kPmAstroMentionBody, kPmBodyMercury},
      {"venus", kPmAstroMentionBody, kPmBodyVenus},
      {"mars", kPmAstroMentionBody, kPmBodyMars},
      {"moon", kPmAstroMentionBody, kPmBodyMoon},
      {"sun", kPmAstroMentionBody, kPmBodySun},
      {"twelfth house", kPmAstroMentionHouse, 11},
      {"12th house", kPmAstroMentionHouse, 11},
      {"eleventh house", kPmAstroMentionHouse, 10},
      {"11th house", kPmAstroMentionHouse, 10},
      {"tenth house", kPmAstroMentionHouse, 9},
      {"10th house", kPmAstroMentionHouse, 9},
      {"ninth house", kPmAstroMentionHouse, 8},
      {"9th house", kPmAstroMentionHouse, 8},
      {"eighth house", kPmAstroMentionHouse, 7},
      {"8th house", kPmAstroMentionHouse, 7},
      {"seventh house", kPmAstroMentionHouse, 6},
      {"7th house", kPmAstroMentionHouse, 6},
      {"sixth house", kPmAstroMentionHouse, 5},
      {"6th house", kPmAstroMentionHouse, 5},
      {"fifth house", kPmAstroMentionHouse, 4},
      {"5th house", kPmAstroMentionHouse, 4},
      {"fourth house", kPmAstroMentionHouse, 3},
      {"4th house", kPmAstroMentionHouse, 3},
      {"third house", kPmAstroMentionHouse, 2},
      {"3rd house", kPmAstroMentionHouse, 2},
      {"second house", kPmAstroMentionHouse, 1},
      {"2nd house", kPmAstroMentionHouse, 1},
      {"first house", kPmAstroMentionHouse, 0},
      {"1st house", kPmAstroMentionHouse, 0},
  };

  typedef struct {
    size_t pos;
    PmAstroMentionKind kind;
    uint8_t id;
  } Hit;

  Hit hits[48];
  size_t hit_n = 0;

  for (size_t wi = 0; wi < sizeof(k_words) / sizeof(k_words[0]); ++wi) {
    const Keyword &kw = k_words[wi];
    const char *p = text;
    while (p && *p && hit_n < sizeof(hits) / sizeof(hits[0])) {
      p = strstr(p, kw.needle);
      if (!p) {
        break;
      }
      if (word_boundary_before(text, p) && word_boundary_after(p + strlen(kw.needle))) {
        hits[hit_n].pos = static_cast<size_t>(p - text);
        hits[hit_n].kind = kw.kind;
        hits[hit_n].id = kw.id;
        ++hit_n;
      }
      ++p;
    }
  }

  for (size_t i = 0; i < hit_n; ++i) {
    for (size_t j = i + 1; j < hit_n; ++j) {
      if (hits[j].pos < hits[i].pos) {
        const Hit tmp = hits[i];
        hits[i] = hits[j];
        hits[j] = tmp;
      }
    }
  }

  plan->count = 0;
  for (size_t i = 0; i < hit_n; ++i) {
    if (hits[i].kind == kPmAstroMentionBody) {
      plan_push(plan, kPmAstroMentionBody, hits[i].id, hits[i].pos);
    } else {
      plan_push(plan, kPmAstroMentionSign, hits[i].id, hits[i].pos);
    }
  }
}

void pm_astro_highlight_build(const char *reply, PmAstroHighlightPlan *plan) {
  if (!plan) {
    return;
  }
  plan->count = 0;
  if (!reply || reply[0] == '\0') {
    return;
  }
  char lower[768];
  to_lower_copy(reply, lower, sizeof(lower));
  scan_keywords(lower, plan);
}

void pm_astro_highlight_at_progress(const PmAstroHighlightPlan *plan, float progress, int *out_body,
                                    int *out_sign) {
  if (out_body) {
    *out_body = -1;
  }
  if (out_sign) {
    *out_sign = -1;
  }
  if (!plan || plan->count == 0) {
    return;
  }
  if (progress < 0.f) {
    progress = 0.f;
  }
  if (progress > 1.f) {
    progress = 1.f;
  }
  const int last = static_cast<int>(plan->count) - 1;
  int idx = static_cast<int>(progress * static_cast<float>(last + 1));
  if (idx > last) {
    idx = last;
  }
  const PmAstroMention &m = plan->items[idx];
  if (m.kind == kPmAstroMentionBody && out_body) {
    *out_body = static_cast<int>(m.id);
    if (out_sign) {
      *out_sign = -1;
    }
  } else if (out_sign) {
    *out_sign = static_cast<int>(m.id);
    if (out_body) {
      *out_body = -1;
    }
  }
}
