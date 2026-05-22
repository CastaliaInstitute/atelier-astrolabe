#include "pm_faculty.h"

#include <Arduino_GFX_Library.h>
#include <JPEGDEC.h>
#include <LittleFS.h>
#include <PNGdec.h>
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
#include "pm_http.h"
#include "pm_nvs.h"
#include "pm_resource.h"
#include "pm_wifi_ntp.h"

static const char *TAG = "pm_faculty";

static constexpr const char *kNvsNs = "mynah";
static constexpr const char *kKeyActive = "fac_active";
static constexpr size_t kBustMaxBytes = 320u * 1024u;
static constexpr size_t kBustFlashMaxBytes = 360u * 1024u;
static constexpr uint32_t kBustTaskStack = 16384;
static constexpr uint32_t kBustPreloadMinIntervalMs = 2500u;

enum class BustVariant : uint8_t { Small = 0, High = 1 };

static TaskHandle_t s_bust_task = nullptr;
static volatile PmFacultyBustStatus s_bust_status = PmFacultyBustStatus::Idle;
static volatile bool s_bust_bg_busy = false;
static volatile bool s_bust_done = false;
static volatile bool s_bust_req_high_only = false;
static char s_bust_req_slug[32] = "";
static char s_bust_slug[32] = "";
static uint8_t *s_bust_bytes = nullptr;
static size_t s_bust_len = 0;
static char s_bust_flash_path[64] = "";
static char s_bust_error[80] = "";
static char s_bust_bearer[1536] = "";
static char s_bust_auth[1560] = "";
static char s_bust_pin_slug[32] = "";

static constexpr int kBustFooterTop = LCD_HEIGHT - 104;
static constexpr int kBustMaxDrawH = LCD_HEIGHT / 2;
static constexpr int kBustFullMaxDrawH = LCD_HEIGHT - 28;
static constexpr int kBustRestBottom = kBustFooterTop - 6;
static constexpr uint32_t kBustRiseMs = 420u;
static constexpr int kBustJpegMaxDim = 512;

static uint16_t *s_decoded_fb = nullptr;
static uint8_t *s_decoded_alpha = nullptr;
static int s_decoded_w = 0;
static int s_decoded_h = 0;
static char s_decoded_slug[32] = "";
static PNG s_bust_png;

static bool s_rise_active = false;
static uint32_t s_rise_start_ms = 0;
static PmFacultyBustStatus s_prev_bust_status = PmFacultyBustStatus::Idle;
static uint32_t s_last_preload_ms = 0;

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

static bool load_slot(int slot, PmFacultyProfile *out) {
  char key[16];
  key_for_slot(key, sizeof(key), slot, "slug");
  char slug[sizeof(out->slug)] = "";
  pm_nvs_get_str(kNvsNs, key, slug, sizeof(slug), "");
  if (slug[0] == '\0') {
    return false;
  }
  memset(out, 0, sizeof(*out));
  strncpy(out->slug, slug, sizeof(out->slug) - 1);
  out->slug[sizeof(out->slug) - 1] = '\0';
  if (!slug_sane(out->slug)) {
    return false;
  }
  key_for_slot(key, sizeof(key), slot, "name");
  pm_nvs_get_str(kNvsNs, key, out->name, sizeof(out->name), "");
  if (out->name[0] == '\0') {
    pm_faculty_label_from_slug(out->slug, out->name, sizeof(out->name));
  }
  out->name[sizeof(out->name) - 1] = '\0';
  key_for_slot(key, sizeof(key), slot, "q");
  pm_nvs_get_str(kNvsNs, key, out->last_user, sizeof(out->last_user), "");
  out->last_user[sizeof(out->last_user) - 1] = '\0';
  key_for_slot(key, sizeof(key), slot, "a");
  pm_nvs_get_str(kNvsNs, key, out->last_reply, sizeof(out->last_reply), "");
  out->last_reply[sizeof(out->last_reply) - 1] = '\0';
  out->valid = true;
  return true;
}

static bool save_slot(int slot, const PmFacultyProfile *in) {
  if (!in || !in->valid || !slug_sane(in->slug) || slot < 0 || slot >= kPmFacultySlots) {
    return false;
  }
  bool ok = true;
  char key[16];
  key_for_slot(key, sizeof(key), slot, "slug");
  ok &= pm_nvs_set_str(kNvsNs, key, in->slug);
  key_for_slot(key, sizeof(key), slot, "name");
  ok &= pm_nvs_set_str(kNvsNs, key, in->name);
  key_for_slot(key, sizeof(key), slot, "q");
  ok &= pm_nvs_set_str(kNvsNs, key, in->last_user);
  key_for_slot(key, sizeof(key), slot, "a");
  ok &= pm_nvs_set_str(kNvsNs, key, in->last_reply);
  return ok;
}

int pm_faculty_count(void) {
  int n = 0;
  for (int i = 0; i < kPmFacultySlots; ++i) {
    PmFacultyProfile tmp = {};
    if (load_slot(i, &tmp)) {
      ++n;
    }
  }
  return n;
}

bool pm_faculty_get_slot(int slot, PmFacultyProfile *out) {
  if (!out || slot < 0 || slot >= kPmFacultySlots) {
    return false;
  }
  return load_slot(slot, out);
}

static int active_slot_raw(void) {
  const int slot = pm_nvs_get_i32(kNvsNs, kKeyActive, 0);
  return (slot >= 0 && slot < kPmFacultySlots) ? slot : 0;
}

bool pm_faculty_active(PmFacultyProfile *out) {
  pm_faculty_ensure_seed();
  int slot = active_slot_raw();
  bool ok = load_slot(slot, out);
  if (!ok) {
    for (int i = 0; i < kPmFacultySlots; ++i) {
      if (load_slot(i, out)) {
        ok = true;
        break;
      }
    }
  }
  return ok;
}

bool pm_faculty_set_active_slot(int slot) {
  PmFacultyProfile tmp = {};
  if (!pm_faculty_get_slot(slot, &tmp)) {
    return false;
  }
  if (!pm_nvs_set_i32(kNvsNs, kKeyActive, slot)) {
    return false;
  }
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
  int cur = active_slot_raw();
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

  for (int i = 0; i < kPmFacultySlots; ++i) {
    char key[16];
    key_for_slot(key, sizeof(key), i, "slug");
    (void)pm_nvs_remove(kNvsNs, key);
    key_for_slot(key, sizeof(key), i, "name");
    (void)pm_nvs_remove(kNvsNs, key);
    key_for_slot(key, sizeof(key), i, "q");
    (void)pm_nvs_remove(kNvsNs, key);
    key_for_slot(key, sizeof(key), i, "a");
    (void)pm_nvs_remove(kNvsNs, key);
    if (next[i].valid) {
      (void)save_slot(i, &next[i]);
    }
  }
  return pm_nvs_set_i32(kNvsNs, kKeyActive, 0);
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
  (void)save_slot(0, &cur);
  (void)pm_nvs_set_i32(kNvsNs, kKeyActive, 0);
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
  for (int i = 0; i < static_cast<int>(sizeof(kSeeds) / sizeof(kSeeds[0])) && i < kPmFacultySlots; ++i) {
    (void)save_slot(i, &kSeeds[i]);
  }
  (void)pm_nvs_set_i32(kNvsNs, kKeyActive, 0);
}

void pm_faculty_ensure_demo_seed(void) {
  pm_faculty_ensure_seed();
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
  if (!LittleFS.begin(true)) {
    bust_set_error("fs unavailable");
    return false;
  }
  if (!LittleFS.exists("/busts")) {
    (void)LittleFS.mkdir("/busts");
  }
  return true;
}

static const char *bust_variant_suffix(BustVariant variant) {
  return variant == BustVariant::High ? "400jpg" : "200jpg";
}

static int bust_variant_width(BustVariant variant) {
  return variant == BustVariant::High ? MYNAH_FACULTY_BUST_HI_WIDTH : MYNAH_FACULTY_BUST_WIDTH;
}

static int bust_variant_height(BustVariant variant) {
  return variant == BustVariant::High ? MYNAH_FACULTY_BUST_HI_HEIGHT : MYNAH_FACULTY_BUST_HEIGHT;
}

static int bust_variant_quality(BustVariant variant) {
  return variant == BustVariant::High ? MYNAH_FACULTY_BUST_HI_QUALITY : MYNAH_FACULTY_BUST_QUALITY;
}

static bool bust_cache_path(const char *slug, BustVariant variant, char *out, size_t cap) {
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
  const int n = snprintf(out, cap, "/busts/%s-%s.bin", clean, bust_variant_suffix(variant));
  return n > 0 && static_cast<size_t>(n) < cap;
}

static bool bust_flash_cached(const char *slug, BustVariant variant = BustVariant::Small) {
  char path[64];
  if (!bust_cache_path(slug, variant, path, sizeof(path)) || !bust_cache_fs_begin()) {
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

static bool load_flash_bust(const char *slug, BustVariant variant = BustVariant::Small) {
  char path[64];
  if (!bust_cache_path(slug, variant, path, sizeof(path)) || !bust_cache_fs_begin()) {
    return false;
  }
  File f = LittleFS.open(path, "r");
  if (!f) {
    return false;
  }
  const size_t sz = f.size();
  if (sz == 0 || sz > kBustFlashMaxBytes) {
    f.close();
    bust_set_error("bad cached bust");
    return false;
  }
  uint8_t sig[4] = {};
  const size_t sig_rd = f.read(sig, sizeof(sig));
  if (!f.seek(0)) {
    f.close();
    bust_set_error("seek cached bust");
    return false;
  }
  const bool is_png = sig_rd == sizeof(sig) && sig[0] == 0x89 && sig[1] == 'P' && sig[2] == 'N' && sig[3] == 'G';
  if (!is_png) {
    f.close();
    free(s_bust_bytes);
    s_bust_bytes = nullptr;
    s_bust_len = sz;
    strncpy(s_bust_slug, slug, sizeof(s_bust_slug) - 1);
    s_bust_slug[sizeof(s_bust_slug) - 1] = '\0';
    strncpy(s_bust_flash_path, path, sizeof(s_bust_flash_path) - 1);
    s_bust_flash_path[sizeof(s_bust_flash_path) - 1] = '\0';
    bust_set_error(nullptr);
    Serial.printf("pm_faculty: flash JPEG bust %s/%s (%u B)\n", s_bust_slug, bust_variant_suffix(variant),
                  static_cast<unsigned>(s_bust_len));
    return true;
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
  free(s_bust_bytes);
  s_bust_bytes = buf;
  s_bust_len = sz;
  strncpy(s_bust_slug, slug, sizeof(s_bust_slug) - 1);
  s_bust_slug[sizeof(s_bust_slug) - 1] = '\0';
  s_bust_flash_path[0] = '\0';
  bust_set_error(nullptr);
  Serial.printf("pm_faculty: flash PNG bust %s/%s (%u B)\n", s_bust_slug, bust_variant_suffix(variant),
                static_cast<unsigned>(s_bust_len));
  return true;
}

static bool save_flash_bust(const char *slug, BustVariant variant, const uint8_t *bytes, size_t len) {
  if (!bytes || len == 0 || len > kBustFlashMaxBytes) {
    return false;
  }
  char path[64];
  if (!bust_cache_path(slug, variant, path, sizeof(path)) || !bust_cache_fs_begin()) {
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
  Serial.printf("pm_faculty: flash cached bust %s/%s (%u B)\n", slug, bust_variant_suffix(variant),
                static_cast<unsigned>(len));
  return true;
}

typedef struct {
  uint8_t *buf;
  size_t cap;
  size_t len;
} BustDownload;

typedef struct {
  File file;
  size_t cap;
  size_t len;
  uint8_t first;
} BustFlashDownload;

static bool bust_download_on_data(const uint8_t *data, size_t len, void *ctx) {
  BustDownload *dl = static_cast<BustDownload *>(ctx);
  if (!dl || !data || len == 0) {
    return false;
  }
  if (dl->len + len > dl->cap) {
    bust_set_error("bust too large");
    return false;
  }
  memcpy(dl->buf + dl->len, data, len);
  dl->len += len;
  return true;
}

static bool bust_flash_download_on_data(const uint8_t *data, size_t len, void *ctx) {
  BustFlashDownload *dl = static_cast<BustFlashDownload *>(ctx);
  if (!dl || !data || len == 0 || !dl->file) {
    return false;
  }
  if (dl->len + len > dl->cap) {
    bust_set_error("bust too large");
    return false;
  }
  if (dl->len == 0) {
    dl->first = data[0];
  }
  const size_t wr = dl->file.write(data, len);
  if (wr != len) {
    bust_set_error("flash write failed");
    return false;
  }
  dl->len += len;
  return true;
}

static bool build_castalia_bust_url(const char *slug, BustVariant variant, char *url, size_t cap) {
  if (!url || cap == 0 || strlen(MYNAH_FACULTY_BUST_ORIGIN) == 0) {
    return false;
  }
  char base[160];
  strncpy(base, MYNAH_FACULTY_BUST_ORIGIN, sizeof(base) - 1);
  base[sizeof(base) - 1] = '\0';
  trim_base_url(base, sizeof(base));
  const int n = snprintf(url, cap, "%s/api/faculty-bust/?faculty=%s&w=%d&h=%d&q=%d&format=jpg&pose=right", base, slug,
                         bust_variant_width(variant), bust_variant_height(variant), bust_variant_quality(variant));
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

static bool build_supabase_bust_url(const char *slug, BustVariant variant, char *url, size_t cap) {
  if (!url || cap == 0 || strlen(MYNAH_SUPABASE_URL) == 0) {
    return false;
  }
  char base[160];
  strncpy(base, MYNAH_SUPABASE_URL, sizeof(base) - 1);
  base[sizeof(base) - 1] = '\0';
  trim_base_url(base, sizeof(base));
  const int n = snprintf(url, cap, "%s/functions/v1/faculty-bust?faculty=%s&w=%d&h=%d&q=%d&format=jpg&pose=right", base, slug,
                         bust_variant_width(variant), bust_variant_height(variant), bust_variant_quality(variant));
  return n > 0 && static_cast<size_t>(n) < cap;
}

static bool fetch_bust_url(const char *url, const char *label, uint8_t **bytes, size_t *len, int depth = 0) {
  if (depth > 1) {
    bust_set_error("redirect depth");
    return false;
  }

  pm_castalia_auth_bearer(s_bust_bearer, sizeof(s_bust_bearer));
  snprintf(s_bust_auth, sizeof(s_bust_auth), "Bearer %s", s_bust_bearer);
  const PmHttpHeader headers[] = {
      {"Authorization", s_bust_auth},
      {"apikey", MYNAH_SUPABASE_ANON_KEY},
      {"Accept", "image/png,image/jpeg,image/*;q=0.8,*/*;q=0.1"},
  };

  BustDownload dl = {};
  dl.cap = kBustMaxBytes;
  dl.buf = static_cast<uint8_t *>(pm_heap_alloc_response(dl.cap));
  if (!dl.buf) {
    bust_set_error("oom bust");
    return false;
  }
  PmHttpTextResult result = {};
  const bool ok = pm_http_request_stream(url, "GET", nullptr, headers, sizeof(headers) / sizeof(headers[0]),
                                         90000, bust_download_on_data, &dl, &result);
  if (!ok) {
    ESP_LOGW(TAG, "faculty-bust %s HTTP %d len %u", label ? label : "url", result.status_code,
             static_cast<unsigned>(result.bytes_read));
    Serial.printf("pm_faculty: bust %s HTTP %d\n", label ? label : "url", result.status_code);
    char errbuf[32];
    snprintf(errbuf, sizeof(errbuf), "bust HTTP %d", result.status_code);
    bust_set_error(errbuf);
    free(dl.buf);
    return false;
  }

  *bytes = dl.buf;
  *len = dl.len;
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
  return true;
}

static bool fetch_bust_url_to_flash(const char *url, const char *label, const char *slug, BustVariant variant) {
  char path[64];
  if (!bust_cache_path(slug, variant, path, sizeof(path)) || !bust_cache_fs_begin()) {
    return false;
  }
  char tmp_path[72];
  snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
  (void)LittleFS.remove(tmp_path);

  pm_castalia_auth_bearer(s_bust_bearer, sizeof(s_bust_bearer));
  snprintf(s_bust_auth, sizeof(s_bust_auth), "Bearer %s", s_bust_bearer);
  const PmHttpHeader headers[] = {
      {"Authorization", s_bust_auth},
      {"apikey", MYNAH_SUPABASE_ANON_KEY},
      {"Accept", "image/png,image/jpeg,image/*;q=0.8,*/*;q=0.1"},
  };

  BustFlashDownload dl = {};
  dl.cap = kBustFlashMaxBytes;
  dl.file = LittleFS.open(tmp_path, "w");
  if (!dl.file) {
    bust_set_error("flash open failed");
    return false;
  }
  PmHttpTextResult result = {};
  const bool ok = pm_http_request_stream(url, "GET", nullptr, headers, sizeof(headers) / sizeof(headers[0]),
                                         90000, bust_flash_download_on_data, &dl, &result);
  dl.file.close();
  if (!ok || dl.len == 0 || dl.first == '{') {
    (void)LittleFS.remove(tmp_path);
    char errbuf[32];
    snprintf(errbuf, sizeof(errbuf), "bust HTTP %d", result.status_code);
    bust_set_error(ok && dl.first == '{' ? "json redirect" : errbuf);
    return false;
  }
  (void)LittleFS.remove(path);
  if (!LittleFS.rename(tmp_path, path)) {
    (void)LittleFS.remove(tmp_path);
    bust_set_error("flash rename failed");
    return false;
  }
  Serial.printf("pm_faculty: bust %s streamed to flash %u B\n", label ? label : "url",
                static_cast<unsigned>(dl.len));
  return true;
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
  s_bust_flash_path[0] = '\0';
  bust_set_error(nullptr);
  Serial.printf("pm_faculty: embedded bust %s (%u B)\n", s_bust_slug, static_cast<unsigned>(s_bust_len));
  return true;
}

static bool fetch_bust_variant(const char *slug, BustVariant variant, bool store_ram) {
  if (!slug_sane(slug)) {
    bust_set_error("bad slug");
    return false;
  }
  if (store_ram && load_flash_bust(slug, variant)) {
    return true;
  }
  if (!store_ram && bust_flash_cached(slug, variant)) {
    return true;
  }
  if (!pm_wifi_connected()) {
    bust_set_error("no wifi");
    return store_ram && variant == BustVariant::Small && cache_embedded_bust(slug);
  }
  if (strlen(MYNAH_FACULTY_BUST_ORIGIN) == 0 && strlen(MYNAH_SUPABASE_URL) == 0) {
    bust_set_error("no bust host");
    return store_ram && variant == BustVariant::Small && cache_embedded_bust(slug);
  }
  (void)pm_castalia_auth_prepare_for_voice();

  uint8_t *bytes = nullptr;
  size_t len = 0;
  char url[240];
  if (!store_ram) {
    if (build_castalia_bust_url(slug, variant, url, sizeof(url)) &&
        fetch_bust_url_to_flash(url, variant == BustVariant::High ? "castalia-400" : "castalia-200", slug, variant)) {
      Serial.printf("pm_faculty: background bust %s/%s stored direct\n", slug, bust_variant_suffix(variant));
      return true;
    }
    if (build_supabase_bust_url(slug, variant, url, sizeof(url)) &&
        fetch_bust_url_to_flash(url, variant == BustVariant::High ? "supabase-400" : "supabase-200", slug, variant)) {
      Serial.printf("pm_faculty: background bust %s/%s stored direct\n", slug, bust_variant_suffix(variant));
      return true;
    }
  }
  if (build_castalia_bust_url(slug, variant, url, sizeof(url)) &&
             fetch_bust_url(url, variant == BustVariant::High ? "castalia-400" : "castalia-200", &bytes, &len)) {
    /* ok */
  } else if (build_supabase_bust_url(slug, variant, url, sizeof(url)) &&
             fetch_bust_url(url, variant == BustVariant::High ? "supabase-400" : "supabase-200", &bytes, &len)) {
    /* ok */
  } else if (variant == BustVariant::Small && build_static_avatar_url(slug, url, sizeof(url)) &&
             fetch_bust_url(url, "avatar", &bytes, &len)) {
    /* ok */
  } else {
    return store_ram && variant == BustVariant::Small && cache_embedded_bust(slug);
  }

  if (!store_ram) {
    const bool saved = save_flash_bust(slug, variant, bytes, len);
    free(bytes);
    if (!saved) {
      bust_set_error("flash save failed");
      return false;
    }
    Serial.printf("pm_faculty: background bust %s/%s stored\n", slug, bust_variant_suffix(variant));
    return true;
  }

  free(s_bust_bytes);
  s_bust_bytes = bytes;
  s_bust_len = len;
  strncpy(s_bust_slug, slug, sizeof(s_bust_slug) - 1);
  s_bust_slug[sizeof(s_bust_slug) - 1] = '\0';
  s_bust_flash_path[0] = '\0';
  (void)save_flash_bust(slug, variant, s_bust_bytes, s_bust_len);
  bust_set_error(nullptr);
  Serial.printf("pm_faculty: cached bust %s/%s (%u B)\n", s_bust_slug, bust_variant_suffix(variant),
                static_cast<unsigned>(s_bust_len));
  return true;
}

static bool fetch_bust_inner(const char *slug) {
  return fetch_bust_variant(slug, BustVariant::Small, true);
}

static void bust_task(void *arg) {
  (void)arg;
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  char slug[sizeof(s_bust_req_slug)];
  strncpy(slug, s_bust_req_slug, sizeof(slug) - 1);
  slug[sizeof(slug) - 1] = '\0';
  const bool high_only = s_bust_req_high_only;
  s_bust_req_high_only = false;
  s_bust_bg_busy = true;
  bool ok = false;
  if (pm_resource_acquire(kPmResourceBustFetch, kPmResourceVoice | kPmResourceAnalyzer | kPmResourceMediaStream,
                          "faculty-bust")) {
    ok = high_only ? true : fetch_bust_inner(slug);
    if (ok && !bust_flash_cached(slug, BustVariant::High)) {
      const uint32_t free_i = pm_heap_internal_free();
      const uint32_t largest_i = pm_heap_internal_largest();
      if (free_i >= MYNAH_FACULTY_MIN_FETCH_HEAP && largest_i >= 16000u) {
        (void)fetch_bust_variant(slug, BustVariant::High, false);
      } else {
        Serial.printf("pm_faculty: defer 400 bust %s heap=%u largest=%u\n", slug, static_cast<unsigned>(free_i),
                      static_cast<unsigned>(largest_i));
      }
    }
    pm_resource_release(kPmResourceBustFetch, "faculty-bust");
  } else {
    bust_set_error("resource busy");
  }
  if (!high_only) {
    s_bust_status = ok ? PmFacultyBustStatus::DoneOk : PmFacultyBustStatus::DoneFail;
    s_bust_done = true;
  }
  s_bust_bg_busy = false;
  s_bust_task = nullptr;
  vTaskDelete(nullptr);
}

static void bust_task_ensure(void) {
  if (s_bust_task) {
    return;
  }
  const uint32_t largest_i = pm_heap_internal_largest();
  if (largest_i < kBustTaskStack + 4096u) {
    Serial.printf("pm_faculty: bust task deferred heap=%u largest=%u\n",
                  static_cast<unsigned>(pm_heap_internal_free()), static_cast<unsigned>(largest_i));
    return;
  }
  const BaseType_t ok = xTaskCreatePinnedToCore(bust_task, "faculty_bust", kBustTaskStack, nullptr, 1, &s_bust_task, 1);
  if (ok != pdPASS) {
    s_bust_task = nullptr;
  }
}

static bool queue_high_bust_fetch(const char *slug) {
  if (!slug_sane(slug) || !pm_wifi_connected() || bust_flash_cached(slug, BustVariant::High) || s_bust_bg_busy ||
      s_bust_status == PmFacultyBustStatus::Working) {
    return false;
  }
  const uint32_t free_i = pm_heap_internal_free();
  const uint32_t largest_i = pm_heap_internal_largest();
  if (free_i < MYNAH_FACULTY_MIN_FETCH_HEAP || largest_i < 16000u) {
    return false;
  }
  bust_task_ensure();
  if (!s_bust_task) {
    return false;
  }
  strncpy(s_bust_req_slug, slug, sizeof(s_bust_req_slug) - 1);
  s_bust_req_slug[sizeof(s_bust_req_slug) - 1] = '\0';
  s_bust_req_high_only = true;
  xTaskNotify(s_bust_task, 1, eSetBits);
  return true;
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
  if (s_bust_status == PmFacultyBustStatus::Working || s_bust_bg_busy) {
    return false;
  }
  if (s_bust_len > 0 && strcmp(s_bust_slug, slug) == 0) {
    return false;
  }
  if (load_flash_bust(slug)) {
    s_bust_done = true;
    s_bust_status = PmFacultyBustStatus::DoneOk;
    s_rise_active = true;
    s_rise_start_ms = millis();
    return true;
  }
  if (!pm_wifi_connected()) {
    if (cache_embedded_bust(slug)) {
      s_bust_done = true;
      s_bust_status = PmFacultyBustStatus::DoneOk;
      s_rise_active = true;
      s_rise_start_ms = millis();
      return true;
    }
    return false;
  }
  const uint32_t free_i = pm_heap_internal_free();
  const uint32_t largest_i = pm_heap_internal_largest();
  if (free_i < MYNAH_FACULTY_MIN_FETCH_HEAP || largest_i < 16000u) {
    bust_set_error("low memory");
    Serial.printf("pm_faculty: bust skipped low memory heap=%u largest=%u\n",
                  static_cast<unsigned>(free_i), static_cast<unsigned>(largest_i));
    return false;
  }
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
  if (s_bust_status == PmFacultyBustStatus::Working || s_bust_bg_busy ||
      (s_last_preload_ms != 0 && now - s_last_preload_ms < kBustPreloadMinIntervalMs)) {
    return false;
  }
  s_last_preload_ms = now;

  if (slug_sane(quote_slug)) {
    if (!bust_flash_cached(quote_slug, BustVariant::Small)) {
      return pm_faculty_request_bust(quote_slug);
    }
    if (!bust_flash_cached(quote_slug, BustVariant::High)) {
      return queue_high_bust_fetch(quote_slug);
    }
  }

  PmFacultyProfile active = {};
  if (pm_faculty_active(&active)) {
    const bool active_loaded =
        (s_bust_len > 0 && strcmp(s_bust_slug, active.slug) == 0) || bust_flash_cached(active.slug, BustVariant::Small);
    if (!active_loaded) {
      return pm_faculty_request_bust(active.slug);
    }
    if (!bust_flash_cached(active.slug, BustVariant::High)) {
      return queue_high_bust_fetch(active.slug);
    }
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
  if (s_decoded_alpha) {
    free(s_decoded_alpha);
    s_decoded_alpha = nullptr;
  }
  s_decoded_w = 0;
  s_decoded_h = 0;
  s_decoded_slug[0] = '\0';
}

void pm_faculty_release_bust_cache(void) {
  if (s_bust_status == PmFacultyBustStatus::Working) {
    if (s_bust_pin_slug[0] != '\0' && strcmp(s_bust_pin_slug, s_bust_req_slug) == 0) {
      return;
    }
    return;
  }
  if (s_bust_pin_slug[0] != '\0' && strcmp(s_bust_pin_slug, s_bust_slug) == 0) {
    return;
  }
  bust_free_decoded();
  free(s_bust_bytes);
  s_bust_bytes = nullptr;
  s_bust_len = 0;
  s_bust_slug[0] = '\0';
  s_bust_flash_path[0] = '\0';
  s_rise_active = false;
  s_bust_status = PmFacultyBustStatus::Idle;
  s_bust_done = false;
}

void pm_faculty_pin_bust(const char *slug) {
  if (!slug_sane(slug)) {
    return;
  }
  strncpy(s_bust_pin_slug, slug, sizeof(s_bust_pin_slug) - 1);
  s_bust_pin_slug[sizeof(s_bust_pin_slug) - 1] = '\0';
}

void pm_faculty_unpin_bust(void) {
  s_bust_pin_slug[0] = '\0';
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

static int bust_png_draw(PNGDRAW *pDraw) {
  if (!s_decoded_fb || s_decoded_w <= 0 || s_decoded_h <= 0 || !pDraw || pDraw->y < 0 || pDraw->y >= s_decoded_h) {
    return 0;
  }
  uint16_t *dst = s_decoded_fb + pDraw->y * s_decoded_w;
  s_bust_png.getLineAsRGB565(pDraw, dst, PNG_RGB565_LITTLE_ENDIAN, 0xffffffff);
  if (s_decoded_alpha) {
    uint8_t alpha_mask[(kBustJpegMaxDim + 7) / 8] = {};
    uint8_t *row_alpha = s_decoded_alpha + static_cast<size_t>(pDraw->y) * ((s_decoded_w + 7) / 8);
    if (s_bust_png.getAlphaMask(pDraw, alpha_mask, 8)) {
      for (int x = 0; x < pDraw->iWidth; ++x) {
        if ((alpha_mask[x >> 3] & (0x80u >> (x & 7))) != 0) {
          row_alpha[x >> 3] |= (0x80u >> (x & 7));
        }
      }
    } else {
      for (int x = 0; x < pDraw->iWidth; ++x) {
        row_alpha[x >> 3] |= (0x80u >> (x & 7));
      }
    }
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
  s_decoded_alpha = nullptr;
  const size_t px = static_cast<size_t>(w) * static_cast<size_t>(h);
  s_decoded_fb = static_cast<uint16_t *>(pm_heap_alloc_response(px * sizeof(uint16_t)));
  if (!s_decoded_fb) {
    jpg.close();
    bust_free_decoded();
    return false;
  }
  memset(s_decoded_fb, 0, px * sizeof(uint16_t));
  jpg.setPixelType(RGB565_LITTLE_ENDIAN);
  if (jpg.decode(0, 0, 0) != 1) {
    jpg.close();
    bust_free_decoded();
    return false;
  }
  jpg.close();
  strncpy(s_decoded_slug, slug, sizeof(s_decoded_slug) - 1);
  s_decoded_slug[sizeof(s_decoded_slug) - 1] = '\0';
  return true;
}

static bool bust_decode_flash_jpeg_locked(const char *slug) {
  if (s_bust_flash_path[0] == '\0') {
    return false;
  }
  File f = LittleFS.open(s_bust_flash_path, "r");
  if (!f) {
    return false;
  }
  JPEGDEC jpg;
  if (jpg.open(f, bust_jpeg_draw) != 1) {
    f.close();
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
  s_decoded_alpha = nullptr;
  const size_t px = static_cast<size_t>(w) * static_cast<size_t>(h);
  s_decoded_fb = static_cast<uint16_t *>(pm_heap_alloc_response(px * sizeof(uint16_t)));
  if (!s_decoded_fb) {
    jpg.close();
    bust_free_decoded();
    return false;
  }
  memset(s_decoded_fb, 0, px * sizeof(uint16_t));
  jpg.setPixelType(RGB565_LITTLE_ENDIAN);
  if (jpg.decode(0, 0, 0) != 1) {
    jpg.close();
    bust_free_decoded();
    return false;
  }
  jpg.close();
  strncpy(s_decoded_slug, slug, sizeof(s_decoded_slug) - 1);
  s_decoded_slug[sizeof(s_decoded_slug) - 1] = '\0';
  return true;
}

static bool bust_decode_png_locked(const char *slug) {
  if (s_bust_png.openRAM(s_bust_bytes, static_cast<int>(s_bust_len), bust_png_draw) != PNG_SUCCESS) {
    return false;
  }
  const int w = s_bust_png.getWidth();
  const int h = s_bust_png.getHeight();
  if (w <= 0 || h <= 0 || w > kBustJpegMaxDim || h > kBustJpegMaxDim) {
    s_bust_png.close();
    return false;
  }
  s_decoded_w = w;
  s_decoded_h = h;
  const size_t px = static_cast<size_t>(w) * static_cast<size_t>(h);
  const size_t alpha_bytes = static_cast<size_t>(h) * static_cast<size_t>((w + 7) / 8);
  s_decoded_fb = static_cast<uint16_t *>(pm_heap_alloc_response(px * sizeof(uint16_t)));
  s_decoded_alpha = static_cast<uint8_t *>(pm_heap_alloc_response(alpha_bytes));
  if (!s_decoded_fb || !s_decoded_alpha) {
    s_bust_png.close();
    bust_free_decoded();
    return false;
  }
  memset(s_decoded_fb, 0, px * sizeof(uint16_t));
  memset(s_decoded_alpha, 0, alpha_bytes);
  const int rc = s_bust_png.decode(nullptr, 0);
  s_bust_png.close();
  if (rc != PNG_SUCCESS) {
    bust_free_decoded();
    return false;
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
  if (!s_bust_bytes && s_bust_flash_path[0] != '\0') {
    return bust_decode_flash_jpeg_locked(slug);
  }
  if (s_bust_len >= 8 && s_bust_bytes && s_bust_bytes[0] == 0x89 && s_bust_bytes[1] == 'P' &&
      s_bust_bytes[2] == 'N' && s_bust_bytes[3] == 'G') {
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
      if (s_decoded_alpha) {
        const uint8_t *row_alpha = s_decoded_alpha + static_cast<size_t>(sy) * ((s_decoded_w + 7) / 8);
        if ((row_alpha[sx >> 3] & (0x80u >> (sx & 7))) == 0) {
          continue;
        }
      }
      pm_gfx->writePixel(x, y, s_decoded_fb[sy * s_decoded_w + sx]);
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

void pm_faculty_draw_bust_for_at(const PmFacultyProfile *faculty, int cx, int bottom_y, int max_w, int max_h) {
  if (!pm_gfx || !faculty || !faculty->valid || max_w < 32 || max_h < 32) {
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
    bust_draw_scaled_jpeg(cx, bottom_y, draw_w, draw_h, 0, LCD_HEIGHT);
    return;
  }
  bust_draw_placeholder(*faculty, cx, bottom_y, draw_h);
}

bool pm_faculty_draw_real_bust_for_at(const PmFacultyProfile *faculty, int cx, int bottom_y, int max_w, int max_h) {
  if (!pm_gfx || !faculty || !faculty->valid || max_w < 32 || max_h < 32) {
    return false;
  }
  if (strcmp(s_decoded_slug, faculty->slug) != 0) {
    bust_free_decoded();
  }
  (void)bust_try_decode_for_slug(faculty->slug);
  if (!s_decoded_fb || strcmp(s_decoded_slug, faculty->slug) != 0) {
    return false;
  }

  int draw_w = max_w;
  int draw_h = max_h;
  bust_compute_draw_size(s_decoded_w, s_decoded_h, max_w, max_h, &draw_w, &draw_h);
  bust_draw_scaled_jpeg(cx, bottom_y, draw_w, draw_h, 0, LCD_HEIGHT);
  return true;
}

void pm_faculty_draw_bust_for(const PmFacultyProfile *faculty) {
  pm_faculty_draw_bust_for_mode(faculty, false);
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
  return repaint;
}

void pm_faculty_on_active_changed(void) {
  bust_free_decoded();
  pm_faculty_begin_bust_rise();
}
