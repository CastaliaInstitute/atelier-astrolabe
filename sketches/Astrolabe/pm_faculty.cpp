#include "pm_faculty.h"

#include <Arduino_GFX_Library.h>
#include <HTTPClient.h>
#include <JPEGDEC.h>
#include <LittleFS.h>
#include <PNGdec.h>
#include <Preferences.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pin_config.h"
#include "pm_castalia_auth.h"
#include "pm_config.h"
#include "pm_heap.h"
#include "pm_display.h"
#include "pm_faculty_assets.h"
#include "pm_speaker.h"
#include "pm_wifi_ntp.h"

static const char *TAG = "pm_faculty";

static constexpr const char *kNvsNs = "mynah";
static constexpr const char *kKeyActive = "fac_active";
static constexpr size_t kBustMaxBytes = 256u * 1024u;
static constexpr size_t kBustFlashMaxBytes = 256u * 1024u;
static constexpr uint32_t kBustTaskStack = 12288;
static constexpr uint32_t kBustPreloadMinIntervalMs = 60000u;

#ifndef ASTROLABE_DEFAULT_FACULTY_SLUG
#if defined(ASTROLABE_DEFAULT_FACULTY_TOM_ROBBINS) && ASTROLABE_DEFAULT_FACULTY_TOM_ROBBINS
#define ASTROLABE_DEFAULT_FACULTY_SLUG "a.tomrobbins"
#else
#define ASTROLABE_DEFAULT_FACULTY_SLUG "a.einstein"
#endif
#endif

#ifndef ASTROLABE_DEFAULT_FACULTY_NAME
#if defined(ASTROLABE_DEFAULT_FACULTY_TOM_ROBBINS) && ASTROLABE_DEFAULT_FACULTY_TOM_ROBBINS
#define ASTROLABE_DEFAULT_FACULTY_NAME "Tom Robbins"
#else
#define ASTROLABE_DEFAULT_FACULTY_NAME "Einstein"
#endif
#endif

static TaskHandle_t s_bust_task = nullptr;
static volatile PmFacultyBustStatus s_bust_status = PmFacultyBustStatus::Idle;
static volatile bool s_bust_done = false;
static char s_bust_req_slug[32] = "";
static volatile bool s_bust_req_flash_only = false;
static char s_bust_slug[32] = "";
static uint8_t *s_bust_bytes = nullptr;
static size_t s_bust_len = 0;
static char s_bust_error[80] = "";

static constexpr int kBustFooterTop = LCD_HEIGHT - 104;
static constexpr int kBustMaxDrawH = LCD_HEIGHT / 2;
static constexpr int kBustFullMaxDrawH = LCD_HEIGHT - 28;
static constexpr int kBustRestBottom = kBustFooterTop - 6;
static constexpr uint32_t kBustRiseMs = 420u;
static constexpr int kBustJpegMaxDim = 512;
static constexpr int kBustPngMaxSrcDim = 1200;
static constexpr int kBustPngMaxLineW = 1024;

static uint16_t *s_decoded_fb = nullptr;
static uint8_t *s_decoded_opaque = nullptr;
static int s_decoded_w = 0;
static int s_decoded_h = 0;
static char s_decoded_slug[32] = "";
static PNG s_bust_png;

static bool s_rise_active = false;
static uint32_t s_rise_start_ms = 0;
static PmFacultyBustStatus s_prev_bust_status = PmFacultyBustStatus::Idle;
static uint32_t s_last_preload_ms = 0;
static int s_preload_slot = -1;
static bool s_bust_fs_checked = false;
static bool s_bust_fs_available = false;
static uint32_t s_bust_fetch_retry_ms = 0;
static uint32_t s_bust_low_mem_log_ms = 0;
static char s_flash_miss_slug[32] = "";

static void key_for_slot(char *out, size_t cap, int slot, const char *suffix) {
  snprintf(out, cap, "fac%d_%s", slot, suffix);
}

static bool slug_sane(const char *slug) {
  if (!slug || slug[0] == '\0') {
    return false;
  }
  for (const char *p = slug; *p; ++p) {
    const unsigned char c = static_cast<unsigned char>(*p);
    if (!(isalnum(c) || c == '-' || c == '_' || c == '.')) {
      return false;
    }
  }
  return true;
}

static void sanitize_slug(const char *in, char *out, size_t cap) {
  if (!out || cap == 0) {
    return;
  }
  size_t o = 0;
  if (in) {
    for (const char *p = in; *p && o + 1 < cap; ++p) {
      const unsigned char c = static_cast<unsigned char>(*p);
      if (isalnum(c)) {
        out[o++] = static_cast<char>(tolower(c));
      } else if (c == '.' && o > 0 && out[o - 1] != '.') {
        out[o++] = '.';
      } else if ((c == '-' || c == '_' || c == ' ') && o > 0 && out[o - 1] != '-') {
        out[o++] = '-';
      }
    }
  }
  while (o > 0 && out[o - 1] == '-') {
    --o;
  }
  out[o] = '\0';
}

static bool extract_json_string_field(const char *json, const char *key, char *out, size_t out_cap) {
  if (!json || !key || !out || out_cap == 0) {
    return false;
  }
  char pat[48];
  snprintf(pat, sizeof(pat), "\"%s\":\"", key);
  const char *p = strstr(json, pat);
  if (!p) {
    return false;
  }
  p += strlen(pat);
  size_t o = 0;
  while (*p && *p != '"' && o + 1 < out_cap) {
    if (*p == '\\' && p[1]) {
      ++p;
    }
    out[o++] = *p++;
  }
  out[o] = '\0';
  return o > 0;
}

static void pm_faculty_normalize_slug(const char *in, char *out, size_t cap) {
  sanitize_slug(in, out, cap);
  if (strcmp(out, "einstein") == 0) {
    strncpy(out, "a.einstein", cap - 1);
    out[cap - 1] = '\0';
  }
}

void pm_faculty_label_from_slug(const char *slug, char *out, size_t cap) {
  if (!out || cap == 0) {
    return;
  }
  out[0] = '\0';
  if (!slug || slug[0] == '\0') {
    return;
  }
  size_t o = 0;
  bool word_start = true;
  for (const char *p = slug; *p && o + 1 < cap; ++p) {
    unsigned char c = static_cast<unsigned char>(*p);
    if (c == '-' || c == '_' || c == '.') {
      if (c != '.' && o > 0 && out[o - 1] != ' ') {
        out[o++] = ' ';
      }
      word_start = true;
      continue;
    }
    if (word_start && isalpha(c)) {
      c = static_cast<unsigned char>(toupper(c));
    }
    out[o++] = static_cast<char>(c);
    word_start = false;
  }
  out[o] = '\0';
}

static bool load_slot(Preferences &pref, int slot, PmFacultyProfile *out) {
  char key[16];
  key_for_slot(key, sizeof(key), slot, "slug");
  const String slug_s = pref.getString(key, "");
  if (slug_s.length() <= 0) {
    return false;
  }
  memset(out, 0, sizeof(*out));
  strncpy(out->slug, slug_s.c_str(), sizeof(out->slug) - 1);
  out->slug[sizeof(out->slug) - 1] = '\0';
  if (!slug_sane(out->slug)) {
    return false;
  }
  key_for_slot(key, sizeof(key), slot, "name");
  const String name_s = pref.getString(key, "");
  if (name_s.length() > 0) {
    strncpy(out->name, name_s.c_str(), sizeof(out->name) - 1);
  } else {
    pm_faculty_label_from_slug(out->slug, out->name, sizeof(out->name));
  }
  out->name[sizeof(out->name) - 1] = '\0';
  key_for_slot(key, sizeof(key), slot, "q");
  const String q_s = pref.getString(key, "");
  strncpy(out->last_user, q_s.c_str(), sizeof(out->last_user) - 1);
  out->last_user[sizeof(out->last_user) - 1] = '\0';
  key_for_slot(key, sizeof(key), slot, "a");
  const String a_s = pref.getString(key, "");
  strncpy(out->last_reply, a_s.c_str(), sizeof(out->last_reply) - 1);
  out->last_reply[sizeof(out->last_reply) - 1] = '\0';
  out->valid = true;
  return true;
}

static bool save_slot(Preferences &pref, int slot, const PmFacultyProfile *in) {
  if (!in || !in->valid || !slug_sane(in->slug) || slot < 0 || slot >= kPmFacultySlots) {
    return false;
  }
  char key[16];
  key_for_slot(key, sizeof(key), slot, "slug");
  pref.putString(key, in->slug);
  key_for_slot(key, sizeof(key), slot, "name");
  pref.putString(key, in->name);
  key_for_slot(key, sizeof(key), slot, "q");
  pref.putString(key, in->last_user);
  key_for_slot(key, sizeof(key), slot, "a");
  pref.putString(key, in->last_reply);
  return true;
}

int pm_faculty_count(void) {
  Preferences pref;
  if (!pref.begin(kNvsNs, true)) {
    return 0;
  }
  int n = 0;
  for (int i = 0; i < kPmFacultySlots; ++i) {
    PmFacultyProfile tmp = {};
    if (load_slot(pref, i, &tmp)) {
      ++n;
    }
  }
  pref.end();
  return n;
}

bool pm_faculty_get_slot(int slot, PmFacultyProfile *out) {
  if (!out || slot < 0 || slot >= kPmFacultySlots) {
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

static int active_slot_raw(Preferences &pref) {
  const int slot = pref.getInt(kKeyActive, 0);
  return (slot >= 0 && slot < kPmFacultySlots) ? slot : 0;
}

bool pm_faculty_active(PmFacultyProfile *out) {
  pm_faculty_ensure_seed();
  Preferences pref;
  if (!pref.begin(kNvsNs, true)) {
    return false;
  }
  int slot = active_slot_raw(pref);
  bool ok = load_slot(pref, slot, out);
  if (!ok) {
    for (int i = 0; i < kPmFacultySlots; ++i) {
      if (load_slot(pref, i, out)) {
        ok = true;
        break;
      }
    }
  }
  pref.end();
  return ok;
}

bool pm_faculty_set_active_slot(int slot) {
  PmFacultyProfile tmp = {};
  if (!pm_faculty_get_slot(slot, &tmp)) {
    return false;
  }
  Preferences pref;
  if (!pref.begin(kNvsNs, false)) {
    return false;
  }
  pref.putInt(kKeyActive, slot);
  pref.end();
  pm_faculty_on_active_changed();
  return true;
}

bool pm_faculty_cycle_active(int delta, PmFacultyProfile *out) {
  pm_faculty_ensure_seed();
  int slots[kPmFacultySlots];
  int n = 0;
  for (int i = 0; i < kPmFacultySlots; ++i) {
    PmFacultyProfile tmp = {};
    if (pm_faculty_get_slot(i, &tmp)) {
      slots[n++] = i;
    }
  }
  if (n <= 0) {
    return false;
  }
  Preferences pref;
  int cur = 0;
  if (pref.begin(kNvsNs, true)) {
    cur = active_slot_raw(pref);
    pref.end();
  }
  int pos = 0;
  for (int i = 0; i < n; ++i) {
    if (slots[i] == cur) {
      pos = i;
      break;
    }
  }
  const int next_pos = (pos + delta % n + n) % n;
  const int next_slot = slots[next_pos];
  if (!pm_faculty_set_active_slot(next_slot)) {
    return false;
  }
  return out ? pm_faculty_get_slot(next_slot, out) : true;
}

int pm_faculty_active_index(void) {
  PmFacultyProfile active = {};
  if (!pm_faculty_active(&active)) {
    return -1;
  }
  int idx = 0;
  for (int i = 0; i < kPmFacultySlots; ++i) {
    PmFacultyProfile f = {};
    if (!pm_faculty_get_slot(i, &f)) {
      continue;
    }
    if (strcasecmp(f.slug, active.slug) == 0) {
      return idx;
    }
    ++idx;
  }
  return -1;
}

static void pm_faculty_clear_nvs_slots(Preferences &pref) {
  for (int i = 0; i < kPmFacultySlots; ++i) {
    char key[16];
    key_for_slot(key, sizeof(key), i, "slug");
    pref.remove(key);
    key_for_slot(key, sizeof(key), i, "name");
    pref.remove(key);
    key_for_slot(key, sizeof(key), i, "q");
    pref.remove(key);
    key_for_slot(key, sizeof(key), i, "a");
    pref.remove(key);
  }
}

static bool pm_faculty_write_roster(const PmFacultyProfile *entries, int count, int active_index) {
  if (!entries || count <= 0 || active_index < 0 || active_index >= count || active_index >= kPmFacultySlots) {
    return false;
  }
  Preferences pref;
  if (!pref.begin(kNvsNs, false)) {
    return false;
  }
  pm_faculty_clear_nvs_slots(pref);
  for (int i = 0; i < count && i < kPmFacultySlots; ++i) {
    if (entries[i].valid) {
      (void)save_slot(pref, i, &entries[i]);
    }
  }
  pref.putInt(kKeyActive, active_index);
  pref.end();
  return true;
}

#if defined(ASTROLABE_DEFAULT_FACULTY_TOM_ROBBINS) && ASTROLABE_DEFAULT_FACULTY_TOM_ROBBINS
static void pm_faculty_ensure_cameo_roster(void) {
  static const PmFacultyProfile kCameoRoster[] = {
      {ASTROLABE_DEFAULT_FACULTY_SLUG, ASTROLABE_DEFAULT_FACULTY_NAME, "", "", true},
      {"a.einstein", "Einstein", "", "", true},
      {"marie-curie", "Marie Curie", "", "", true},
      {"hypatia", "Hypatia", "", "", true},
      {"socrates", "Socrates", "", "", true},
  };
  constexpr int kRosterCount = static_cast<int>(sizeof(kCameoRoster) / sizeof(kCameoRoster[0]));

  int count = 0;
  bool has_default = false;
  for (int i = 0; i < kPmFacultySlots; ++i) {
    PmFacultyProfile f = {};
    if (pm_faculty_get_slot(i, &f)) {
      ++count;
      if (strcasecmp(f.slug, ASTROLABE_DEFAULT_FACULTY_SLUG) == 0) {
        has_default = true;
      }
    }
  }
  if (has_default && count >= kRosterCount - 1) {
    return;
  }
  (void)pm_faculty_write_roster(kCameoRoster, kRosterCount, 0);
}
#endif

bool pm_faculty_remember(const char *slug_in, const char *name_in) {
  char slug[32];
  pm_faculty_normalize_slug(slug_in, slug, sizeof(slug));
  if (!slug_sane(slug)) {
    return false;
  }
  char name[36] = "";
  if (name_in && name_in[0] != '\0') {
    strncpy(name, name_in, sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';
  } else {
    pm_faculty_label_from_slug(slug, name, sizeof(name));
  }

  PmFacultyProfile old[kPmFacultySlots] = {};
  for (int i = 0; i < kPmFacultySlots; ++i) {
    (void)pm_faculty_get_slot(i, &old[i]);
  }

  PmFacultyProfile next[kPmFacultySlots] = {};
  strncpy(next[0].slug, slug, sizeof(next[0].slug) - 1);
  strncpy(next[0].name, name, sizeof(next[0].name) - 1);
  next[0].valid = true;
  int o = 1;
  for (int i = 0; i < kPmFacultySlots && o < kPmFacultySlots; ++i) {
    if (!old[i].valid || strcasecmp(old[i].slug, slug) == 0) {
      if (old[i].valid && strcasecmp(old[i].slug, slug) == 0) {
        strncpy(next[0].last_user, old[i].last_user, sizeof(next[0].last_user) - 1);
        strncpy(next[0].last_reply, old[i].last_reply, sizeof(next[0].last_reply) - 1);
      }
      continue;
    }
    next[o++] = old[i];
  }

  Preferences pref;
  if (!pref.begin(kNvsNs, false)) {
    return false;
  }
  for (int i = 0; i < kPmFacultySlots; ++i) {
    char key[16];
    key_for_slot(key, sizeof(key), i, "slug");
    pref.remove(key);
    key_for_slot(key, sizeof(key), i, "name");
    pref.remove(key);
    key_for_slot(key, sizeof(key), i, "q");
    pref.remove(key);
    key_for_slot(key, sizeof(key), i, "a");
    pref.remove(key);
    if (next[i].valid) {
      (void)save_slot(pref, i, &next[i]);
    }
  }
  pref.putInt(kKeyActive, 0);
  pref.end();
  return true;
}

bool pm_faculty_set_active_slug(const char *slug, const char *name) {
  char clean[32];
  pm_faculty_normalize_slug(slug, clean, sizeof(clean));
  if (!slug_sane(clean)) {
    return false;
  }
  for (int i = 0; i < kPmFacultySlots; ++i) {
    PmFacultyProfile tmp = {};
    if (pm_faculty_get_slot(i, &tmp) && strcasecmp(tmp.slug, clean) == 0) {
      return pm_faculty_set_active_slot(i);
    }
  }
  return pm_faculty_remember(clean, name);
}

void pm_faculty_note_turn(const char *slug, const char *name, const char *transcript, const char *reply) {
  if (!pm_faculty_remember(slug, name)) {
    return;
  }
  PmFacultyProfile cur = {};
  if (!pm_faculty_active(&cur)) {
    return;
  }
  if (transcript) {
    strncpy(cur.last_user, transcript, sizeof(cur.last_user) - 1);
    cur.last_user[sizeof(cur.last_user) - 1] = '\0';
  }
  if (reply) {
    strncpy(cur.last_reply, reply, sizeof(cur.last_reply) - 1);
    cur.last_reply[sizeof(cur.last_reply) - 1] = '\0';
  }
  Preferences pref;
  if (!pref.begin(kNvsNs, false)) {
    return;
  }
  (void)save_slot(pref, 0, &cur);
  pref.putInt(kKeyActive, 0);
  pref.end();
}

bool pm_faculty_build_history(char *out, size_t cap) {
  if (!out || cap == 0) {
    return false;
  }
  out[0] = '\0';
  PmFacultyProfile cur = {};
  if (!pm_faculty_active(&cur)) {
    return false;
  }
  if (cur.last_user[0] == '\0' && cur.last_reply[0] == '\0') {
    snprintf(out, cap,
             "No local turns cached. Ask the server to fetch recent commonplace conversation entries for "
             "facultySlug=%s when a Castalia user is signed in.",
             cur.slug);
    return true;
  }
  snprintf(out, cap,
           "Local recent turn with %s (facultySlug=%s). User: %.140s Faculty: %.210s. Also fetch any "
           "recent Directus commonplace conversation entries for this signed-in user and faculty.",
           cur.name, cur.slug, cur.last_user, cur.last_reply);
  return true;
}

void pm_faculty_ensure_seed(void) {
  static bool s_seed_checked = false;
  if (s_seed_checked) {
    return;
  }
  s_seed_checked = true;
  if (pm_faculty_count() > 0) {
    return;
  }
  static const PmFacultyProfile kSeeds[] = {
      {"a.einstein", "Einstein", "", "", true},
      {"marie-curie", "Marie Curie", "", "", true},
      {"hypatia", "Hypatia", "", "", true},
      {"socrates", "Socrates", "", "", true},
  };
  Preferences pref;
  if (!pref.begin(kNvsNs, false)) {
    return;
  }
  for (int i = 0; i < static_cast<int>(sizeof(kSeeds) / sizeof(kSeeds[0])) && i < kPmFacultySlots; ++i) {
    (void)save_slot(pref, i, &kSeeds[i]);
  }
  pref.putInt(kKeyActive, 0);
  pref.end();
}

void pm_faculty_ensure_demo_seed(void) {
  pm_faculty_ensure_seed();
#if defined(ASTROLABE_DEFAULT_FACULTY_TOM_ROBBINS) && ASTROLABE_DEFAULT_FACULTY_TOM_ROBBINS
  pm_faculty_ensure_cameo_roster();
#endif
  for (int i = 0; i < kPmFacultySlots; ++i) {
    PmFacultyProfile legacy = {};
    if (pm_faculty_get_slot(i, &legacy) && strcmp(legacy.slug, "einstein") == 0) {
      (void)pm_faculty_remember("a.einstein", "Einstein");
      break;
    }
  }
  static const PmFacultyProfile kDemo[] = {
      {"a.einstein", "Einstein", "", "", true},
  };
  for (const auto &seed : kDemo) {
    bool found = false;
    for (int i = 0; i < kPmFacultySlots; ++i) {
      PmFacultyProfile slot = {};
      if (pm_faculty_get_slot(i, &slot) && strcasecmp(slot.slug, seed.slug) == 0) {
        found = true;
        break;
      }
    }
    if (!found) {
      (void)pm_faculty_remember(seed.slug, seed.name);
    }
  }
#if defined(ASTROLABE_FORCE_FACULTY_HOME) && ASTROLABE_FORCE_FACULTY_HOME
  (void)pm_faculty_set_active_slug(ASTROLABE_DEFAULT_FACULTY_SLUG, ASTROLABE_DEFAULT_FACULTY_NAME);
#endif
}

void pm_faculty_prepare_demo_view(void) {
  pm_faculty_ensure_demo_seed();
  static bool s_demo_faculty_started = false;
  if (s_demo_faculty_started) {
    return;
  }
  s_demo_faculty_started = true;
  (void)pm_faculty_set_active_slug("a.einstein", "Einstein");
}

static void trim_base_url(char *url, size_t cap) {
  if (!url || cap == 0) {
    return;
  }
  while (strlen(url) > 0 && url[strlen(url) - 1] == '/') {
    url[strlen(url) - 1] = '\0';
  }
}

static void bust_set_error(const char *msg) {
  if (!msg) {
    s_bust_error[0] = '\0';
    return;
  }
  strncpy(s_bust_error, msg, sizeof(s_bust_error) - 1);
  s_bust_error[sizeof(s_bust_error) - 1] = '\0';
}

static bool bust_cache_fs_begin(void) {
  if (s_bust_fs_checked) {
    return s_bust_fs_available;
  }
  s_bust_fs_checked = true;
  if (!LittleFS.begin(true)) {
    bust_set_error("fs unavailable");
    ESP_LOGW(TAG, "faculty bust flash cache unavailable");
    return false;
  }
  if (!LittleFS.exists("/busts")) {
    (void)LittleFS.mkdir("/busts");
  }
  s_bust_fs_available = true;
  return true;
}

static bool bust_cache_path(const char *slug, char *out, size_t cap) {
  if (!slug_sane(slug) || !out || cap == 0) {
    return false;
  }
  char clean[40];
  sanitize_slug(slug, clean, sizeof(clean));
  if (clean[0] == '\0') {
    return false;
  }
  for (char *p = clean; *p; ++p) {
    if (*p == '.') {
      *p = '-';
    }
  }
  const int n = snprintf(out, cap, "/busts/%s.bin", clean);
  return n > 0 && static_cast<size_t>(n) < cap;
}

static bool bust_flash_cached(const char *slug) {
  char path[64];
  if (!bust_cache_path(slug, path, sizeof(path)) || !bust_cache_fs_begin()) {
    return false;
  }
  File f = LittleFS.open(path, "r");
  if (!f) {
    return false;
  }
  const size_t sz = f.size();
  f.close();
  return sz > 0 && sz <= kBustFlashMaxBytes;
}

static bool load_flash_bust(const char *slug) {
  if (!slug_sane(slug)) {
    return false;
  }
  if (strcmp(s_flash_miss_slug, slug) == 0) {
    return false;
  }
  char path[64];
  if (!bust_cache_path(slug, path, sizeof(path)) || !bust_cache_fs_begin()) {
    return false;
  }
  File f = LittleFS.open(path, "r");
  if (!f) {
    strncpy(s_flash_miss_slug, slug, sizeof(s_flash_miss_slug) - 1);
    s_flash_miss_slug[sizeof(s_flash_miss_slug) - 1] = '\0';
    return false;
  }
  const size_t sz = f.size();
  if (sz == 0 || sz > kBustFlashMaxBytes) {
    f.close();
    bust_set_error("bad cached bust");
    return false;
  }
  uint8_t *buf = static_cast<uint8_t *>(pm_heap_alloc_response(sz));
  if (!buf) {
    f.close();
    bust_set_error("oom cached bust");
    return false;
  }
  const size_t rd = f.read(buf, sz);
  f.close();
  if (rd != sz) {
    free(buf);
    bust_set_error("short cached bust");
    return false;
  }
  const bool png = buf[0] == 0x89 && buf[1] == 'P' && buf[2] == 'N' && buf[3] == 'G';
  const bool jpg = buf[0] == 0xFF && buf[1] == 0xD8;
  if (!png && !jpg) {
    free(buf);
    (void)LittleFS.remove(path);
    bust_set_error("bad cached bust");
    return false;
  }
  free(s_bust_bytes);
  s_bust_bytes = buf;
  s_bust_len = sz;
  strncpy(s_bust_slug, slug, sizeof(s_bust_slug) - 1);
  s_bust_slug[sizeof(s_bust_slug) - 1] = '\0';
  bust_set_error(nullptr);
  s_flash_miss_slug[0] = '\0';
  Serial.printf("pm_faculty: flash bust %s (%u B)\n", s_bust_slug, static_cast<unsigned>(s_bust_len));
  return true;
}

static bool save_flash_bust(const char *slug, const uint8_t *bytes, size_t len) {
  if (!bytes || len == 0 || len > kBustFlashMaxBytes) {
    return false;
  }
  char path[64];
  if (!bust_cache_path(slug, path, sizeof(path)) || !bust_cache_fs_begin()) {
    return false;
  }
  File f = LittleFS.open(path, "w");
  if (!f) {
    return false;
  }
  const size_t wr = f.write(bytes, len);
  f.close();
  if (wr != len) {
    (void)LittleFS.remove(path);
    return false;
  }
  Serial.printf("pm_faculty: flash cached bust %s (%u B)\n", slug, static_cast<unsigned>(len));
  if (slug_sane(slug) && strcmp(s_flash_miss_slug, slug) == 0) {
    s_flash_miss_slug[0] = '\0';
  }
  return true;
}

static bool read_binary_body(HTTPClient *http, uint8_t **out, size_t *out_len) {
  if (!http || !out || !out_len) {
    return false;
  }
  *out = nullptr;
  *out_len = 0;
  const int declared = http->getSize();
  if (declared > 0 && static_cast<size_t>(declared) > kBustMaxBytes) {
    bust_set_error("bust too large");
    return false;
  }
  const size_t cap = declared > 0 ? static_cast<size_t>(declared) : kBustMaxBytes;
  uint8_t *buf = static_cast<uint8_t *>(pm_heap_alloc_response(cap));
  if (!buf) {
    bust_set_error("oom bust");
    return false;
  }
  WiFiClient *stream = http->getStreamPtr();
  if (!stream) {
    free(buf);
    bust_set_error("bad stream");
    return false;
  }
  size_t rd = 0;
  const uint32_t deadline = millis() + 90000u;
  while (rd < cap && static_cast<int32_t>(millis() - deadline) < 0) {
    const int avail = stream->available();
    if (avail > 0) {
      const size_t take = static_cast<size_t>(avail) < (cap - rd) ? static_cast<size_t>(avail) : (cap - rd);
      const int n = stream->readBytes(buf + rd, take);
      if (n > 0) {
        rd += static_cast<size_t>(n);
      }
      if (declared > 0 && rd >= static_cast<size_t>(declared)) {
        break;
      }
    } else if (!http->connected()) {
      break;
    } else {
      delay(5);
    }
  }
  if (rd == 0 || (declared > 0 && rd < static_cast<size_t>(declared))) {
    free(buf);
    bust_set_error("short bust");
    return false;
  }
  *out = buf;
  *out_len = rd;
  return true;
}

static bool build_castalia_bust_url(const char *slug, char *url, size_t cap) {
  if (!url || cap == 0 || strlen(MYNAH_FACULTY_BUST_ORIGIN) == 0) {
    return false;
  }
  char base[160];
  strncpy(base, MYNAH_FACULTY_BUST_ORIGIN, sizeof(base) - 1);
  base[sizeof(base) - 1] = '\0';
  trim_base_url(base, sizeof(base));
  const int n = snprintf(url, cap, "%s/api/faculty-bust/?faculty=%s&w=%d&h=%d&q=%d", base, slug,
                         MYNAH_FACULTY_BUST_WIDTH, MYNAH_FACULTY_BUST_HEIGHT, MYNAH_FACULTY_BUST_QUALITY);
  return n > 0 && static_cast<size_t>(n) < cap;
}

static bool build_static_avatar_url(const char *slug, char *url, size_t cap) {
  if (!slug_sane(slug) || !url || cap == 0 || strlen(MYNAH_CASTALIA_WEB_ORIGIN) == 0) {
    return false;
  }
  char base[160];
  strncpy(base, MYNAH_CASTALIA_WEB_ORIGIN, sizeof(base) - 1);
  base[sizeof(base) - 1] = '\0';
  trim_base_url(base, sizeof(base));

  char path_slug[48];
  if (strncmp(slug, "a.", 2) == 0) {
    snprintf(path_slug, sizeof(path_slug), "a-%s", slug + 2);
  } else if (strncmp(slug, "a-", 2) == 0) {
    snprintf(path_slug, sizeof(path_slug), "%s", slug);
  } else {
    snprintf(path_slug, sizeof(path_slug), "a-%s", slug);
  }
  for (char *p = path_slug; *p; ++p) {
    if (*p == '.' || *p == '_') {
      *p = '-';
    }
  }
  const int n = snprintf(url, cap, "%s/faculty/avatars/%s-sprite.png", base, path_slug);
  return n > 0 && static_cast<size_t>(n) < cap;
}

static bool build_supabase_bust_url(const char *slug, char *url, size_t cap) {
  if (!url || cap == 0 || strlen(MYNAH_SUPABASE_URL) == 0) {
    return false;
  }
  char base[160];
  strncpy(base, MYNAH_SUPABASE_URL, sizeof(base) - 1);
  base[sizeof(base) - 1] = '\0';
  trim_base_url(base, sizeof(base));
  const int n = snprintf(url, cap, "%s/functions/v1/faculty-bust?faculty=%s&w=%d&h=%d&q=%d&view=right", base, slug,
                         MYNAH_FACULTY_BUST_WIDTH, MYNAH_FACULTY_BUST_HEIGHT, MYNAH_FACULTY_BUST_QUALITY);
  return n > 0 && static_cast<size_t>(n) < cap;
}

static void bust_slug_token(const char *slug, char *out, size_t cap) {
  if (!out || cap == 0) {
    return;
  }
  out[0] = '\0';
  if (!slug || slug[0] == '\0') {
    return;
  }
  size_t o = 0;
  for (const char *p = slug; *p && o + 1 < cap; ++p) {
    char c = *p;
    if (c == '.' || c == '_') {
      c = '-';
    }
    out[o++] = static_cast<char>(tolower(static_cast<unsigned char>(c)));
  }
  out[o] = '\0';
}

static int bust_storage_slug_candidates(const char *slug, char out[][32], int max_n) {
  if (!slug || slug[0] == '\0' || !out || max_n <= 0) {
    return 0;
  }
  int n = 0;
  auto add = [&](const char *s) {
    if (!s || s[0] == '\0' || n >= max_n) {
      return;
    }
    for (int i = 0; i < n; ++i) {
      if (strcmp(out[i], s) == 0) {
        return;
      }
    }
    strncpy(out[n], s, 31);
    out[n][31] = '\0';
    ++n;
  };

  char token[32];
  bust_slug_token(slug, token, sizeof(token));
  add(token);
  add(slug);
  if (strncmp(slug, "a.", 2) == 0) {
    add(slug + 2);
    char prefixed[32];
    snprintf(prefixed, sizeof(prefixed), "a-%s", slug + 2);
    bust_slug_token(prefixed, prefixed, sizeof(prefixed));
    add(prefixed);
  } else if (strncmp(slug, "a-", 2) == 0) {
    add(slug + 2);
  }
  return n;
}

static bool build_supabase_storage_bust_url(const char *object_path, char *url, size_t cap) {
  if (!object_path || object_path[0] == '\0' || !url || cap == 0 || strlen(MYNAH_SUPABASE_URL) == 0) {
    return false;
  }
  char base[160];
  strncpy(base, MYNAH_SUPABASE_URL, sizeof(base) - 1);
  base[sizeof(base) - 1] = '\0';
  trim_base_url(base, sizeof(base));
  const int n = snprintf(url, cap, "%s/storage/v1/object/public/busts/%s", base, object_path);
  return n > 0 && static_cast<size_t>(n) < cap;
}

static bool bust_network_fetch_enabled(void) {
  return strlen(MYNAH_SUPABASE_URL) > 0 || strlen(MYNAH_FACULTY_BUST_ORIGIN) > 0 ||
         strlen(MYNAH_CASTALIA_WEB_ORIGIN) > 0;
}

static bool url_is_https(const char *url) {
  return url && strncasecmp(url, "https://", 8) == 0;
}

static bool fetch_bust_url(const char *url, const char *label, uint8_t **bytes, size_t *len, int depth = 0) {
  if (depth > 1) {
    bust_set_error("redirect depth");
    return false;
  }
  WiFiClient plain_client;
  WiFiClientSecure secure_client;
  WiFiClient *client = &plain_client;
  if (url_is_https(url)) {
    secure_client.setInsecure();
    client = &secure_client;
  }
  client->setTimeout(90);
  HTTPClient http;
  http.setTimeout(65535);
  if (!http.begin(*client, url)) {
    bust_set_error("http begin");
    return false;
  }
  pm_castalia_auth_apply_headers(&http);
  http.addHeader("Accept", "image/png,image/jpeg,image/*;q=0.8,*/*;q=0.1");
  const int code = http.GET();
  if (code != 200) {
    ESP_LOGW(TAG, "faculty-bust %s HTTP %d", label ? label : "url", code);
    Serial.printf("pm_faculty: bust %s HTTP %d\n", label ? label : "url", code);
    char errbuf[32];
    snprintf(errbuf, sizeof(errbuf), "bust HTTP %d", code);
    bust_set_error(errbuf);
    http.end();
    return false;
  }

  const bool ok = read_binary_body(&http, bytes, len);
  http.end();
  if (ok) {
    Serial.printf("pm_faculty: bust %s fetched %u B\n", label ? label : "url", static_cast<unsigned>(*len));
    if (*bytes && *len > 2 && (*bytes)[0] == '{') {
      char signed_url[384];
      const bool have_url =
          extract_json_string_field(reinterpret_cast<const char *>(*bytes), "url", signed_url, sizeof(signed_url));
      free(*bytes);
      *bytes = nullptr;
      *len = 0;
      if (!have_url) {
        bust_set_error("json no url");
        return false;
      }
      return fetch_bust_url(signed_url, "signed", bytes, len, depth + 1);
    }
  }
  return ok;
}

static bool fetch_supabase_storage_bust(const char *slug, uint8_t **bytes, size_t *len) {
  if (!slug_sane(slug) || !bytes || !len || strlen(MYNAH_SUPABASE_URL) == 0) {
    return false;
  }
  char slug_candidates[6][32];
  const int slug_n = bust_storage_slug_candidates(slug, slug_candidates, 6);
  static const char *kLeafNames[] = {"bust.png", "bust.webp", "bust.jpg", "bust.jpeg", "bust_frontal.png", "bust_frontal.webp"};
  char object_path[72];
  char url[280];
  for (int si = 0; si < slug_n; ++si) {
    for (const char *leaf : kLeafNames) {
      const int pn = snprintf(object_path, sizeof(object_path), "%s/%s", slug_candidates[si], leaf);
      if (pn <= 0 || static_cast<size_t>(pn) >= sizeof(object_path)) {
        continue;
      }
      if (!build_supabase_storage_bust_url(object_path, url, sizeof(url))) {
        continue;
      }
      bust_set_error(nullptr);
      if (fetch_bust_url(url, "storage", bytes, len)) {
        return true;
      }
    }
  }
  return false;
}

static bool cache_embedded_bust(const char *slug) {
  const uint8_t *embedded = nullptr;
  size_t embedded_len = 0;
  if (!pm_faculty_embedded_bust(slug, &embedded, &embedded_len) || embedded_len == 0) {
    return false;
  }
  uint8_t *copy = static_cast<uint8_t *>(pm_heap_alloc_response(embedded_len));
  if (!copy) {
    bust_set_error("oom embedded bust");
    return false;
  }
  memcpy(copy, embedded, embedded_len);
  free(s_bust_bytes);
  s_bust_bytes = copy;
  s_bust_len = embedded_len;
  strncpy(s_bust_slug, slug, sizeof(s_bust_slug) - 1);
  s_bust_slug[sizeof(s_bust_slug) - 1] = '\0';
  bust_set_error(nullptr);
  Serial.printf("pm_faculty: embedded bust %s (%u B)\n", s_bust_slug, static_cast<unsigned>(s_bust_len));
  return true;
}

static bool has_embedded_bust(const char *slug) {
  const uint8_t *embedded = nullptr;
  size_t embedded_len = 0;
  return pm_faculty_embedded_bust(slug, &embedded, &embedded_len) && embedded_len > 0;
}

static bool fetch_bust_network_bytes(const char *slug, uint8_t **bytes, size_t *len) {
  if (!slug_sane(slug) || !bytes || !len) {
    return false;
  }
  if (!pm_wifi_connected()) {
    bust_set_error("no wifi");
    return false;
  }
  if (!bust_network_fetch_enabled()) {
    bust_set_error("no bust host");
    return false;
  }
  (void)pm_speaker_release_idle_task();
  pm_heap_prepare_tls();
  (void)pm_castalia_auth_prepare_for_voice();

  char url[240];
  if (fetch_supabase_storage_bust(slug, bytes, len)) {
    return true;
  }
  if (build_static_avatar_url(slug, url, sizeof(url)) && fetch_bust_url(url, "avatar", bytes, len)) {
    return true;
  }
  if (build_castalia_bust_url(slug, url, sizeof(url)) && fetch_bust_url(url, "castalia", bytes, len)) {
    return true;
  }
  if (build_supabase_bust_url(slug, url, sizeof(url)) && fetch_bust_url(url, "supabase", bytes, len)) {
    return true;
  }
  return false;
}

static void bust_release_bytes(void) {
  free(s_bust_bytes);
  s_bust_bytes = nullptr;
  s_bust_len = 0;
  s_bust_slug[0] = '\0';
  s_bust_status = PmFacultyBustStatus::Idle;
  s_bust_done = false;
}

static bool cache_bust_to_flash_inner(const char *slug) {
  if (!slug_sane(slug)) {
    bust_set_error("bad slug");
    return false;
  }
  if (has_embedded_bust(slug) || bust_flash_cached(slug)) {
    return true;
  }
  uint8_t *bytes = nullptr;
  size_t len = 0;
  if (!fetch_bust_network_bytes(slug, &bytes, &len)) {
    return false;
  }
  if (!save_flash_bust(slug, bytes, len)) {
    free(bytes);
    bust_set_error("flash write");
    return false;
  }
  PmFacultyProfile active = {};
  if (pm_faculty_active(&active) && strcasecmp(active.slug, slug) == 0) {
    free(s_bust_bytes);
    s_bust_bytes = bytes;
    s_bust_len = len;
    strncpy(s_bust_slug, slug, sizeof(s_bust_slug) - 1);
    s_bust_slug[sizeof(s_bust_slug) - 1] = '\0';
    bust_set_error(nullptr);
  } else {
    free(bytes);
  }
  Serial.printf("pm_faculty: roster flash cached %s (%u B)\n", slug, static_cast<unsigned>(len));
  return true;
}

static bool cache_bust_to_flash_inner(const char *slug);
static void bust_task_ensure(void);

static bool fetch_bust_inner(const char *slug) {
  if (!slug_sane(slug)) {
    bust_set_error("bad slug");
    return false;
  }
  if (cache_embedded_bust(slug)) {
    return true;
  }
  if (load_flash_bust(slug)) {
    return true;
  }
  uint8_t *bytes = nullptr;
  size_t len = 0;
  if (!fetch_bust_network_bytes(slug, &bytes, &len)) {
    return false;
  }

  free(s_bust_bytes);
  s_bust_bytes = bytes;
  s_bust_len = len;
  strncpy(s_bust_slug, slug, sizeof(s_bust_slug) - 1);
  s_bust_slug[sizeof(s_bust_slug) - 1] = '\0';
  (void)save_flash_bust(slug, s_bust_bytes, s_bust_len);
  bust_set_error(nullptr);
  Serial.printf("pm_faculty: cached bust %s (%u B)\n", s_bust_slug, static_cast<unsigned>(s_bust_len));
  return true;
}

static bool pm_faculty_request_bust_cache(const char *slug) {
  if (!slug_sane(slug)) {
    return false;
  }
  if (s_bust_status == PmFacultyBustStatus::Working) {
    return false;
  }
  if (millis() < s_bust_fetch_retry_ms) {
    return false;
  }
  if (has_embedded_bust(slug) || bust_flash_cached(slug)) {
    return false;
  }
  if (!pm_wifi_connected()) {
    return false;
  }
  if (!pm_heap_bust_fetch_ready(nullptr)) {
    s_bust_fetch_retry_ms = millis() + 8000u;
    return false;
  }
  bust_task_ensure();
  if (!s_bust_task) {
    return false;
  }
  strncpy(s_bust_req_slug, slug, sizeof(s_bust_req_slug) - 1);
  s_bust_req_slug[sizeof(s_bust_req_slug) - 1] = '\0';
  s_bust_req_flash_only = true;
  s_bust_done = false;
  s_bust_status = PmFacultyBustStatus::Working;
  xTaskNotify(s_bust_task, 1, eSetBits);
  return true;
}

static void bust_task(void *arg) {
  (void)arg;
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    const bool flash_only = s_bust_req_flash_only;
    s_bust_req_flash_only = false;
    const bool ok = flash_only ? cache_bust_to_flash_inner(s_bust_req_slug) : fetch_bust_inner(s_bust_req_slug);
    if (flash_only) {
      s_bust_status = PmFacultyBustStatus::Idle;
    } else {
      s_bust_status = ok ? PmFacultyBustStatus::DoneOk : PmFacultyBustStatus::DoneFail;
    }
    s_bust_done = true;
  }
}

static void bust_task_ensure(void) {
  if (s_bust_task) {
    return;
  }
  xTaskCreatePinnedToCore(bust_task, "faculty_bust", kBustTaskStack, nullptr, 1, &s_bust_task, 1);
}

bool pm_faculty_tick_bust_fetch(void) {
  PmFacultyProfile cur = {};
  if (!pm_faculty_active(&cur)) {
    return false;
  }
  return pm_faculty_request_bust(cur.slug);
}

bool pm_faculty_request_bust(const char *slug) {
  if (!slug_sane(slug)) {
    bust_set_error("bad slug");
    return false;
  }
  if (s_bust_status == PmFacultyBustStatus::Working) {
    return false;
  }
  if (s_bust_len > 0 && strcmp(s_bust_slug, slug) == 0) {
    return false;
  }
  if (cache_embedded_bust(slug)) {
    s_bust_done = true;
    s_bust_status = PmFacultyBustStatus::DoneOk;
    s_rise_active = true;
    s_rise_start_ms = millis();
    return true;
  }
  if (load_flash_bust(slug)) {
    s_bust_done = true;
    s_bust_status = PmFacultyBustStatus::DoneOk;
    s_rise_active = true;
    s_rise_start_ms = millis();
    return true;
  }
  if (!pm_wifi_connected()) {
    return false;
  }
  if (millis() < s_bust_fetch_retry_ms) {
    return false;
  }
  if (!pm_heap_bust_fetch_ready(nullptr)) {
    bust_set_error("low memory");
    s_bust_fetch_retry_ms = millis() + 8000u;
    const uint32_t now = millis();
    if (s_bust_low_mem_log_ms == 0 || now - s_bust_low_mem_log_ms >= 8000u) {
      s_bust_low_mem_log_ms = now;
      Serial.printf("pm_faculty: bust deferred low memory heap=%u largest=%u\n",
                    static_cast<unsigned>(pm_heap_internal_free()),
                    static_cast<unsigned>(pm_heap_internal_largest()));
    }
    return false;
  }
  s_bust_fetch_retry_ms = 0;
  bust_task_ensure();
  if (!s_bust_task) {
    return false;
  }
  strncpy(s_bust_req_slug, slug, sizeof(s_bust_req_slug) - 1);
  s_bust_req_slug[sizeof(s_bust_req_slug) - 1] = '\0';
  s_bust_done = false;
  s_bust_status = PmFacultyBustStatus::Working;
  xTaskNotify(s_bust_task, 1, eSetBits);
  return true;
}

bool pm_faculty_preload_busts(const char *quote_slug) {
  const uint32_t now = millis();
  if (s_bust_status == PmFacultyBustStatus::Working ||
      (s_last_preload_ms != 0 && now - s_last_preload_ms < kBustPreloadMinIntervalMs)) {
    return false;
  }
  s_last_preload_ms = now;

  if (!bust_cache_fs_begin()) {
    return false;
  }

  if (slug_sane(quote_slug) && !has_embedded_bust(quote_slug) && !bust_flash_cached(quote_slug)) {
    return pm_faculty_request_bust_cache(quote_slug);
  }

  constexpr int n = kPmFacultySlots;
  for (int tries = 0; tries < n; ++tries) {
    s_preload_slot = (s_preload_slot + 1 + n) % n;
    PmFacultyProfile f = {};
    if (!pm_faculty_get_slot(s_preload_slot, &f)) {
      continue;
    }
    if (strstr(f.slug, "missing") != nullptr) {
      continue;
    }
    if (has_embedded_bust(f.slug)) {
      continue;
    }
    if (bust_flash_cached(f.slug)) {
      continue;
    }
    return pm_faculty_request_bust_cache(f.slug);
  }

  PmFacultyProfile active = {};
  if (pm_faculty_active(&active) &&
      (s_bust_len == 0 || strcasecmp(s_bust_slug, active.slug) != 0)) {
    return pm_faculty_request_bust(active.slug);
  }
  return false;
}

PmFacultyBustStatus pm_faculty_bust_status(void) { return s_bust_status; }
const char *pm_faculty_bust_slug(void) { return s_bust_slug; }
size_t pm_faculty_bust_size(void) { return s_bust_len; }
const char *pm_faculty_bust_last_error(void) {
  return s_bust_error[0] != '\0' ? s_bust_error : "bust unavailable";
}
bool pm_faculty_bust_ready_for(const char *slug) {
  return slug_sane(slug) && s_bust_len > 0 && strcmp(s_bust_slug, slug) == 0 &&
         ((s_decoded_fb && strcmp(s_decoded_slug, slug) == 0) || s_bust_status == PmFacultyBustStatus::DoneOk);
}

static float bust_rise_ease(float t) {
  if (t <= 0.f) {
    return 0.f;
  }
  if (t >= 1.f) {
    return 1.f;
  }
  const float inv = 1.f - t;
  return 1.f - inv * inv * inv;
}

static void bust_free_decoded(void) {
  if (s_decoded_fb) {
    free(s_decoded_fb);
    s_decoded_fb = nullptr;
  }
  if (s_decoded_opaque) {
    free(s_decoded_opaque);
    s_decoded_opaque = nullptr;
  }
  s_decoded_w = 0;
  s_decoded_h = 0;
  s_decoded_slug[0] = '\0';
}

static void bust_compute_decode_size(int src_w, int src_h, int *out_w, int *out_h) {
  if (!out_w || !out_h || src_w <= 0 || src_h <= 0) {
    return;
  }
  int w = src_w;
  int h = src_h;
  if (w > kBustJpegMaxDim) {
    h = h * kBustJpegMaxDim / w;
    w = kBustJpegMaxDim;
  }
  if (h > kBustJpegMaxDim) {
    w = w * kBustJpegMaxDim / h;
    h = kBustJpegMaxDim;
  }
  *out_w = w;
  *out_h = h;
}

static bool bust_pixel_visible(uint16_t col, uint8_t opaque) {
  if (opaque < 128) {
    return false;
  }
  if (col == 0x0000) {
    return false;
  }
  const uint8_t r = static_cast<uint8_t>((col >> 11) << 3);
  const uint8_t g = static_cast<uint8_t>(((col >> 5) & 0x3f) << 2);
  const uint8_t b = static_cast<uint8_t>((col & 0x1f) << 3);
  if (r < 12 && g < 12 && b < 12) {
    return false;
  }
  // Transparent palette spill from indexed PNGs (e.g. a-tomrobbins uses ~#47704c at alpha 0).
  if (g > r + 24 && g > b + 24 && g > 72) {
    return false;
  }
  return true;
}

struct BustPngCtx {
  int src_w;
  int src_h;
  int out_w;
  int out_h;
  uint16_t *out;
  uint8_t *opaque;
};

static int bust_png_draw(PNGDRAW *pDraw) {
  if (!pDraw || !pDraw->pUser) {
    return 0;
  }
  auto *ctx = static_cast<BustPngCtx *>(pDraw->pUser);
  if (!ctx->out || !ctx->opaque || ctx->out_w <= 0 || ctx->out_h <= 0) {
    return 0;
  }
  const int dst_y =
      (ctx->out_h <= 1 || ctx->src_h <= 1) ? 0 : (pDraw->y * (ctx->out_h - 1)) / (ctx->src_h - 1);
  if (dst_y < 0 || dst_y >= ctx->out_h) {
    return 1;
  }
  if (pDraw->iWidth <= 0 || pDraw->iWidth > kBustPngMaxLineW) {
    return 0;
  }

  static uint16_t line[kBustPngMaxLineW];
  static uint8_t alpha[kBustPngMaxLineW];
  s_bust_png.getLineAsRGB565(pDraw, line, PNG_RGB565_BIG_ENDIAN, 0x000000);
  s_bust_png.getAlphaMask(pDraw, alpha, 128);

  for (int dx = 0; dx < ctx->out_w; ++dx) {
    const int sx = (ctx->out_w <= 1) ? 0 : (dx * (pDraw->iWidth - 1)) / (ctx->out_w - 1);
    const int i = dst_y * ctx->out_w + dx;
    ctx->out[i] = line[sx];
    ctx->opaque[i] = alpha[sx];
  }
  return 1;
}

void pm_faculty_release_bust_cache(void) {
  bust_free_decoded();
  if (s_bust_status == PmFacultyBustStatus::Working) {
    return;
  }
  s_rise_active = false;
}

static int bust_jpeg_draw(JPEGDRAW *pDraw) {
  if (!s_decoded_fb || s_decoded_w <= 0 || s_decoded_h <= 0 || !pDraw) {
    return 0;
  }
  for (int row = 0; row < pDraw->iHeight; ++row) {
    uint16_t *dst = s_decoded_fb + (pDraw->y + row) * s_decoded_w + pDraw->x;
    const uint16_t *src = pDraw->pPixels + row * pDraw->iWidth;
    memcpy(dst, src, static_cast<size_t>(pDraw->iWidth) * sizeof(uint16_t));
  }
  return 1;
}

static bool bust_decode_jpeg_locked(const char *slug) {
  JPEGDEC jpg;
  if (jpg.openRAM(s_bust_bytes, static_cast<int>(s_bust_len), bust_jpeg_draw) != 1) {
    return false;
  }
  const int w = jpg.getWidth();
  const int h = jpg.getHeight();
  if (w <= 0 || h <= 0 || w > kBustJpegMaxDim || h > kBustJpegMaxDim) {
    jpg.close();
    return false;
  }
  s_decoded_w = w;
  s_decoded_h = h;
  const size_t px = static_cast<size_t>(w) * static_cast<size_t>(h);
  s_decoded_fb = static_cast<uint16_t *>(pm_heap_alloc_response(px * sizeof(uint16_t)));
  s_decoded_opaque = static_cast<uint8_t *>(pm_heap_alloc_response(px));
  if (!s_decoded_fb || !s_decoded_opaque) {
    jpg.close();
    bust_free_decoded();
    return false;
  }
  memset(s_decoded_fb, 0, px * sizeof(uint16_t));
  memset(s_decoded_opaque, 0, px);
  jpg.setPixelType(RGB565_BIG_ENDIAN);
  if (jpg.decode(0, 0, 0) != 1) {
    jpg.close();
    bust_free_decoded();
    return false;
  }
  jpg.close();
  for (size_t i = 0; i < px; ++i) {
    s_decoded_opaque[i] = bust_pixel_visible(s_decoded_fb[i], 255) ? 255 : 0;
  }
  strncpy(s_decoded_slug, slug, sizeof(s_decoded_slug) - 1);
  s_decoded_slug[sizeof(s_decoded_slug) - 1] = '\0';
  return true;
}

static bool bust_decode_png_locked(const char *slug) {
  if (s_bust_png.openRAM(s_bust_bytes, static_cast<int>(s_bust_len), bust_png_draw) != PNG_SUCCESS) {
    return false;
  }
  const int src_w = s_bust_png.getWidth();
  const int src_h = s_bust_png.getHeight();
  if (src_w <= 0 || src_h <= 0 || src_w > kBustPngMaxSrcDim || src_h > kBustPngMaxSrcDim ||
      src_w > kBustPngMaxLineW) {
    s_bust_png.close();
    return false;
  }
  int out_w = 0;
  int out_h = 0;
  bust_compute_decode_size(src_w, src_h, &out_w, &out_h);
  if (out_w <= 0 || out_h <= 0) {
    s_bust_png.close();
    return false;
  }
  s_decoded_w = out_w;
  s_decoded_h = out_h;
  const size_t px = static_cast<size_t>(out_w) * static_cast<size_t>(out_h);
  s_decoded_fb = static_cast<uint16_t *>(pm_heap_alloc_response(px * sizeof(uint16_t)));
  s_decoded_opaque = static_cast<uint8_t *>(pm_heap_alloc_response(px));
  if (!s_decoded_fb || !s_decoded_opaque) {
    s_bust_png.close();
    bust_free_decoded();
    return false;
  }
  memset(s_decoded_fb, 0, px * sizeof(uint16_t));
  memset(s_decoded_opaque, 0, px);

  BustPngCtx ctx = {};
  ctx.src_w = src_w;
  ctx.src_h = src_h;
  ctx.out_w = out_w;
  ctx.out_h = out_h;
  ctx.out = s_decoded_fb;
  ctx.opaque = s_decoded_opaque;

  const int rc = s_bust_png.decode(&ctx, 0);
  s_bust_png.close();
  if (rc != PNG_SUCCESS) {
    bust_free_decoded();
    return false;
  }
  for (size_t i = 0; i < px; ++i) {
    if (!bust_pixel_visible(s_decoded_fb[i], s_decoded_opaque[i])) {
      s_decoded_opaque[i] = 0;
      s_decoded_fb[i] = 0x0000;
    }
  }
  strncpy(s_decoded_slug, slug, sizeof(s_decoded_slug) - 1);
  s_decoded_slug[sizeof(s_decoded_slug) - 1] = '\0';
  return true;
}

static bool bust_try_decode_for_slug(const char *slug) {
  if (!slug_sane(slug) || s_bust_len == 0 || strcmp(s_bust_slug, slug) != 0) {
    return false;
  }
  if (s_decoded_fb && strcmp(s_decoded_slug, slug) == 0) {
    return true;
  }
  bust_free_decoded();
  if (s_bust_len >= 8 && s_bust_bytes[0] == 0x89 && s_bust_bytes[1] == 'P' && s_bust_bytes[2] == 'N' &&
      s_bust_bytes[3] == 'G') {
    return bust_decode_png_locked(slug);
  }
  return bust_decode_jpeg_locked(slug);
}

static void bust_compute_draw_size(int src_w, int src_h, int max_w, int max_h, int *out_w, int *out_h) {
  if (!out_w || !out_h || src_w <= 0 || src_h <= 0) {
    return;
  }
  int h = max_h;
  int w = src_w * h / src_h;
  if (w > max_w) {
    w = max_w;
    h = src_h * w / src_w;
  }
  if (h > max_h) {
    h = max_h;
    w = src_w * h / src_h;
  }
  *out_w = w;
  *out_h = h;
}

static int bust_rise_offset_px(int draw_h, float rise_t) {
  const int extra = draw_h + 48;
  return static_cast<int>((1.f - rise_t) * static_cast<float>(extra));
}

static float bust_rise_progress(uint32_t now_ms) {
  if (!s_rise_active) {
    return 1.f;
  }
  const uint32_t elapsed = now_ms - s_rise_start_ms;
  if (elapsed >= kBustRiseMs) {
    s_rise_active = false;
    return 1.f;
  }
  return bust_rise_ease(static_cast<float>(elapsed) / static_cast<float>(kBustRiseMs));
}

static void bust_draw_scaled_jpeg(int cx, int bottom_y, int draw_w, int draw_h, int clip_top, int clip_bottom) {
  if (!pm_gfx || !s_decoded_fb || draw_w <= 0 || draw_h <= 0) {
    return;
  }
  const int left = cx - draw_w / 2;
  const int top = bottom_y - draw_h;
  for (int dy = 0; dy < draw_h; ++dy) {
    const int y = top + dy;
    if (y < clip_top || y >= clip_bottom) {
      continue;
    }
    const int sy = dy * s_decoded_h / draw_h;
    for (int dx = 0; dx < draw_w; ++dx) {
      const int x = left + dx;
      if (x < 0 || x >= LCD_WIDTH) {
        continue;
      }
      const int sx = dx * s_decoded_w / draw_w;
      const int si = sy * s_decoded_w + sx;
      const uint16_t col = s_decoded_fb[si];
      const uint8_t opaque = s_decoded_opaque ? s_decoded_opaque[si] : 255;
      if (!bust_pixel_visible(col, opaque)) {
        continue;
      }
      pm_gfx->writePixel(x, y, col);
    }
  }
}

static void bust_initials_for_name(const char *name, char *out, size_t cap) {
  if (!out || cap == 0) {
    return;
  }
  out[0] = '\0';
  if (!name || name[0] == '\0') {
    strncpy(out, "?", cap - 1);
    out[cap - 1] = '\0';
    return;
  }
  size_t o = 0;
  bool word_start = true;
  for (const char *p = name; *p && o + 1 < cap; ++p) {
    if (*p == ' ' || *p == '-' || *p == '_') {
      word_start = true;
      continue;
    }
    if (word_start) {
      out[o++] = *p;
      word_start = false;
    }
  }
  if (o == 0) {
    out[o++] = name[0];
  }
  out[o] = '\0';
}

static void bust_draw_placeholder(const PmFacultyProfile &faculty, int cx, int bottom_y, int draw_h) {
  if (!pm_gfx) {
    return;
  }
  const int cy = bottom_y - draw_h / 2;
  const int r_outer = draw_h / 2 - 4;
  if (r_outer < 24) {
    return;
  }
  const uint16_t c_outer = pm_gfx->color565(132, 108, 190);
  const uint16_t c_mid = pm_gfx->color565(42, 36, 70);
  const uint16_t c_inner = pm_gfx->color565(18, 18, 34);
  const uint16_t c_line = pm_gfx->color565(210, 196, 255);
  pm_gfx->fillCircle(cx, cy, r_outer, c_outer);
  pm_gfx->fillCircle(cx, cy, r_outer - 4, c_mid);
  pm_gfx->fillCircle(cx, cy, r_outer - 10, c_inner);
  const int head_r = r_outer / 3;
  pm_gfx->drawCircle(cx, cy - head_r, head_r, c_line);
  pm_gfx->drawCircle(cx, cy + r_outer / 3, r_outer / 2, c_line);

  char initials[8];
  bust_initials_for_name(faculty.name, initials, sizeof(initials));
  pm_gfx->setTextSize(2, 2);
  int16_t x1 = 0;
  int16_t y1 = 0;
  uint16_t tw = 0;
  uint16_t th = 0;
  pm_gfx->getTextBounds(initials, 0, 0, &x1, &y1, &tw, &th);
  pm_gfx->setCursor(cx - static_cast<int>(tw) / 2, cy - static_cast<int>(th) / 2 - head_r / 2);
  pm_gfx->setTextColor(pm_gfx->color565(245, 240, 255));
  pm_gfx->print(initials);
}

void pm_faculty_begin_bust_rise(void) {
  s_rise_active = true;
  s_rise_start_ms = millis();
}

bool pm_faculty_bust_animating(void) { return s_rise_active; }

void pm_faculty_draw_bust(void) {
  PmFacultyProfile faculty = {};
  if (!pm_faculty_active(&faculty)) {
    return;
  }
  pm_faculty_draw_bust_for(&faculty);
}

static void pm_faculty_draw_bust_for_mode(const PmFacultyProfile *faculty, bool fullscreen) {
  if (!pm_gfx || !faculty || !faculty->valid) {
    return;
  }
  if (strcmp(s_decoded_slug, faculty->slug) != 0) {
    bust_free_decoded();
  }
  (void)bust_try_decode_for_slug(faculty->slug);

  int draw_w = fullscreen ? (LCD_WIDTH - 24) : ((LCD_WIDTH - 40) * 3 / 4);
  int draw_h = fullscreen ? kBustFullMaxDrawH : kBustMaxDrawH;
  if (s_decoded_fb && strcmp(s_decoded_slug, faculty->slug) == 0) {
    bust_compute_draw_size(s_decoded_w, s_decoded_h, fullscreen ? (LCD_WIDTH - 24) : (LCD_WIDTH - 40),
                           fullscreen ? kBustFullMaxDrawH : kBustMaxDrawH, &draw_w, &draw_h);
  }

  const float rise_t = fullscreen ? 1.f : bust_rise_progress(millis());
  const int bottom_y = fullscreen ? (LCD_HEIGHT + draw_h) / 2 : (kBustRestBottom + bust_rise_offset_px(draw_h, rise_t));
  const int cx = LCD_WIDTH / 2;

  if (s_decoded_fb && strcmp(s_decoded_slug, faculty->slug) == 0) {
    bust_draw_scaled_jpeg(cx, bottom_y, draw_w, draw_h, fullscreen ? 0 : 48, fullscreen ? LCD_HEIGHT : kBustFooterTop);
    return;
  }
  bust_draw_placeholder(*faculty, cx, bottom_y, draw_h);
}

void pm_faculty_draw_bust_for(const PmFacultyProfile *faculty) {
  pm_faculty_draw_bust_for_mode(faculty, false);
}

void pm_faculty_draw_bust_for_at(const PmFacultyProfile *faculty, int cx, int bottom_y, int max_w, int max_h,
                                 int clip_top, int clip_bottom) {
  if (!pm_gfx || !faculty || !faculty->valid || max_w <= 0 || max_h <= 0) {
    return;
  }
  if (strcmp(s_decoded_slug, faculty->slug) != 0) {
    bust_free_decoded();
  }
  (void)bust_try_decode_for_slug(faculty->slug);

  int draw_w = max_w;
  int draw_h = max_h;
  if (s_decoded_fb && strcmp(s_decoded_slug, faculty->slug) == 0) {
    bust_compute_draw_size(s_decoded_w, s_decoded_h, max_w, max_h, &draw_w, &draw_h);
    bust_draw_scaled_jpeg(cx, bottom_y, draw_w, draw_h, clip_top, clip_bottom);
    return;
  }
  bust_draw_placeholder(*faculty, cx, bottom_y, draw_h);
}

void pm_faculty_draw_bust_fullscreen(void) {
  PmFacultyProfile faculty = {};
  if (!pm_faculty_active(&faculty)) {
    return;
  }
  pm_faculty_draw_bust_for_mode(&faculty, true);
}

bool pm_faculty_tick(uint32_t now_ms) {
  bool repaint = false;
  const PmFacultyBustStatus st = s_bust_status;
  if (s_prev_bust_status == PmFacultyBustStatus::Working &&
      (st == PmFacultyBustStatus::DoneOk || st == PmFacultyBustStatus::DoneFail)) {
    if (st == PmFacultyBustStatus::DoneOk) {
      bust_free_decoded();
      (void)bust_try_decode_for_slug(s_bust_slug);
      s_rise_active = true;
      s_rise_start_ms = now_ms;
    }
    repaint = true;
  }
  s_prev_bust_status = st;

  if (s_rise_active) {
    const uint32_t elapsed = now_ms - s_rise_start_ms;
    if (elapsed < kBustRiseMs + 32u) {
      repaint = true;
    } else {
      s_rise_active = false;
    }
  }

  PmFacultyProfile active = {};
  if (pm_faculty_active(&active) && s_bust_status != PmFacultyBustStatus::Working && millis() >= s_bust_fetch_retry_ms &&
      !pm_faculty_bust_ready_for(active.slug)) {
    if (pm_faculty_request_bust(active.slug)) {
      repaint = true;
    }
  }
  return repaint;
}

void pm_faculty_on_active_changed(void) {
  bust_free_decoded();
  bust_release_bytes();
  s_flash_miss_slug[0] = '\0';
  pm_faculty_begin_bust_rise();
  PmFacultyProfile cur = {};
  if (pm_faculty_active(&cur)) {
    (void)pm_faculty_request_bust(cur.slug);
  }
}

void pm_faculty_draw_name_label(void) {
  if (!pm_gfx) {
    return;
  }
  const int count = pm_faculty_count();
  if (count <= 1) {
    return;
  }
  PmFacultyProfile faculty = {};
  if (!pm_faculty_active(&faculty)) {
    return;
  }
  const int idx = pm_faculty_active_index();
  char line[48];
  if (idx >= 0) {
    snprintf(line, sizeof(line), "%s  %d/%d", faculty.name, idx + 1, count);
  } else {
    snprintf(line, sizeof(line), "%s", faculty.name);
  }
  pm_gfx->setTextSize(1, 1);
  pm_gfx->setTextColor(pm_gfx->color565(180, 176, 196));
  int16_t x1 = 0;
  int16_t y1 = 0;
  uint16_t tw = 0;
  uint16_t th = 0;
  pm_gfx->getTextBounds(line, 0, 0, &x1, &y1, &tw, &th);
  pm_gfx->setCursor((LCD_WIDTH - static_cast<int>(tw)) / 2, LCD_HEIGHT - static_cast<int>(th) - 8);
  pm_gfx->print(line);
}
