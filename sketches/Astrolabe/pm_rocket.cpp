#include "pm_rocket.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <cstdio>
#include <cstring>
#include <time.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "pm_config.h"

static const char *TAG = "pm_rocket";

static const char *kLl2UpcomingUrl =
    "https://ll.thespacedevs.com/2.2.0/launch/upcoming/?limit=20";

static constexpr int64_t kHorizonSec = 14 * 24 * 3600;

static bool extract_json_string_field(const char *json, const char *key, char *out, size_t out_cap) {
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

static time_t utc_iso8601_to_epoch(const char *iso) {
  if (!iso || !iso[0]) {
    return 0;
  }
  int y = 0;
  int mo = 0;
  int d = 0;
  int h = 0;
  int mi = 0;
  int s = 0;
  if (sscanf(iso, "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &s) < 6) {
    return 0;
  }
  struct tm u = {};
  u.tm_year = y - 1900;
  u.tm_mon = mo - 1;
  u.tm_mday = d;
  u.tm_hour = h;
  u.tm_min = mi;
  u.tm_sec = s;
  u.tm_isdst = 0;
  const char *prev = getenv("TZ");
  char saved[48] = {};
  if (prev) {
    strncpy(saved, prev, sizeof(saved) - 1);
  }
  setenv("TZ", "UTC0", 1);
  tzset();
  const time_t e = mktime(&u);
  if (prev) {
    setenv("TZ", saved, 1);
  } else {
    unsetenv("TZ");
  }
  tzset();
  return e;
}

static bool launch_block_is_past_success(const char *block, time_t net_epoch, time_t now_epoch) {
  char abbrev[16];
  abbrev[0] = '\0';
  (void)extract_json_string_field(block, "abbrev", abbrev, sizeof(abbrev));
  if (strcmp(abbrev, "Success") == 0 && net_epoch > 0 && net_epoch <= now_epoch) {
    return true;
  }
  if (net_epoch > 0 && net_epoch < now_epoch - 1800) {
    return true;
  }
  return false;
}

static bool parse_launch_block(const char *block, size_t block_len, time_t now_epoch, PmRocketLaunch *out) {
  if (!block || block_len < 32 || !out) {
    return false;
  }
  char *slice = static_cast<char *>(malloc(block_len + 1));
  if (!slice) {
    return false;
  }
  memcpy(slice, block, block_len);
  slice[block_len] = '\0';

  char net_iso[32];
  net_iso[0] = '\0';
  if (!extract_json_string_field(slice, "net", net_iso, sizeof(net_iso))) {
    free(slice);
    return false;
  }
  const time_t net_epoch = utc_iso8601_to_epoch(net_iso);
  if (net_epoch <= 0) {
    free(slice);
    return false;
  }
  if (launch_block_is_past_success(slice, net_epoch, now_epoch)) {
    free(slice);
    return false;
  }

  memset(out, 0, sizeof(*out));
  out->net_unix = static_cast<int64_t>(net_epoch);
  (void)extract_json_string_field(slice, "name", out->name, sizeof(out->name));
  (void)extract_json_string_field(slice, "abbrev", out->status_abbrev, sizeof(out->status_abbrev));

  const char *rocket = strstr(slice, "\"rocket\":");
  if (rocket) {
    const char *full = strstr(rocket, "\"full_name\":\"");
    if (full) {
      full += strlen("\"full_name\":\"");
      size_t o = 0;
      while (*full && *full != '"' && o + 1 < sizeof(out->vehicle)) {
        out->vehicle[o++] = *full++;
      }
      out->vehicle[o] = '\0';
    }
    if (!out->vehicle[0]) {
      const char *cfg_name = strstr(rocket, "\"configuration\":");
      if (cfg_name) {
        const char *nm = strstr(cfg_name, "\"name\":\"");
        if (nm) {
          nm += strlen("\"name\":\"");
          size_t o = 0;
          while (*nm && *nm != '"' && o + 1 < sizeof(out->vehicle)) {
            out->vehicle[o++] = *nm++;
          }
          out->vehicle[o] = '\0';
        }
      }
    }
  }

  const char *lsp = strstr(slice, "\"launch_service_provider\":");
  if (lsp) {
    const char *nm = strstr(lsp, "\"name\":\"");
    if (nm) {
      nm += strlen("\"name\":\"");
      size_t o = 0;
      while (*nm && *nm != '"' && o + 1 < sizeof(out->provider)) {
        out->provider[o++] = *nm++;
      }
      out->provider[o] = '\0';
    }
  }

  const char *pad = strstr(slice, "\"pad\":");
  if (pad) {
    const char *nm = strstr(pad, "\"name\":\"");
    if (nm) {
      nm += strlen("\"name\":\"");
      size_t o = 0;
      while (*nm && *nm != '"' && o + 1 < sizeof(out->pad)) {
        out->pad[o++] = *nm++;
      }
      out->pad[o] = '\0';
    }
    const char *loc = strstr(pad, "\"location\":");
    if (loc) {
      const char *ln = strstr(loc, "\"name\":\"");
      if (ln) {
        ln += strlen("\"name\":\"");
        size_t o = 0;
        while (*ln && *ln != '"' && o + 1 < sizeof(out->location)) {
          out->location[o++] = *ln++;
        }
        out->location[o] = '\0';
      }
    }
  }

  free(slice);
  if (!out->name[0]) {
    return false;
  }
  out->valid = true;
  return true;
}

static void insert_launch_sorted(PmRocketLaunch *list, int *count, const PmRocketLaunch *launch) {
  if (!launch || !launch->valid) {
    return;
  }
  int slot = *count;
  for (int i = 0; i < *count; ++i) {
    if (launch->net_unix < list[i].net_unix) {
      slot = i;
      break;
    }
  }
  if (*count < kPmRocketMaxLaunches) {
    for (int i = *count; i > slot; --i) {
      list[i] = list[i - 1];
    }
    (*count)++;
    list[slot] = *launch;
    return;
  }
  if (slot >= kPmRocketMaxLaunches) {
    return;
  }
  for (int i = kPmRocketMaxLaunches - 1; i > slot; --i) {
    list[i] = list[i - 1];
  }
  list[slot] = *launch;
}

static int collect_upcoming_launches(const char *json, PmRocketLaunch *list, int list_cap) {
  const char *results = strstr(json, "\"results\":");
  if (!results || list_cap <= 0) {
    return 0;
  }
  const time_t now_epoch = time(nullptr);
  const int64_t horizon = static_cast<int64_t>(now_epoch) + kHorizonSec;
  int count = 0;

  const char *p = results;
  while ((p = strstr(p, "{\"id\":")) != nullptr) {
    const char *next = strstr(p + 8, "{\"id\":");
    const char *end = next ? next : json + strlen(json);
    const size_t n = static_cast<size_t>(end - p);
    PmRocketLaunch scratch = {};
    if (parse_launch_block(p, n, now_epoch, &scratch) && scratch.net_unix <= horizon) {
      insert_launch_sorted(list, &count, &scratch);
    }
    if (!next) {
      break;
    }
    p = next;
  }
  return count;
}

const PmRocketLaunch *pm_rocket_next(const PmRocketStatus *status) {
  if (!status || !status->ok || status->count <= 0) {
    return nullptr;
  }
  for (int i = 0; i < status->count; ++i) {
    if (status->launches[i].valid) {
      return &status->launches[i];
    }
  }
  return nullptr;
}

bool pm_rocket_fetch(PmRocketStatus *out) {
  if (!out) {
    return false;
  }
  memset(out, 0, sizeof(*out));

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(MYNAH_ROCKET_HTTP_MS);
  if (!http.begin(client, kLl2UpcomingUrl)) {
    snprintf(out->error, sizeof(out->error), "HTTP begin failed");
    return false;
  }

  const int code = http.GET();
  const int streamLen = http.getSize();
  if (code != 200 || streamLen <= 0 || streamLen > MYNAH_ROCKET_MAX_BYTES) {
    ESP_LOGW(TAG, "LL2 HTTP %d len %d", code, streamLen);
    snprintf(out->error, sizeof(out->error), "HTTP %d", code);
    http.end();
    return false;
  }

  char *resp = static_cast<char *>(
      heap_caps_malloc(static_cast<size_t>(streamLen) + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!resp) {
    resp = static_cast<char *>(malloc(static_cast<size_t>(streamLen) + 1));
  }
  if (!resp) {
    http.end();
    snprintf(out->error, sizeof(out->error), "alloc");
    return false;
  }

  WiFiClient *stream = http.getStreamPtr();
  size_t rd = 0;
  const uint32_t deadline = millis() + MYNAH_ROCKET_HTTP_MS;
  while (rd < static_cast<size_t>(streamLen)) {
    if (stream->available() > 0) {
      const int n = stream->readBytes(resp + rd, static_cast<size_t>(streamLen) - rd);
      if (n > 0) {
        rd += static_cast<size_t>(n);
        continue;
      }
    }
    if (static_cast<int32_t>(millis() - deadline) >= 0) {
      break;
    }
    delay(2);
  }
  resp[rd] = '\0';
  http.end();

  if (rd == 0) {
    free(resp);
    snprintf(out->error, sizeof(out->error), "empty body");
    return false;
  }

  out->count = collect_upcoming_launches(resp, out->launches, kPmRocketMaxLaunches);
  if (out->count > 0) {
    out->ok = true;
    ESP_LOGI(TAG, "launch clock: %d upcoming (next %s @ %lld)", out->count, out->launches[0].name,
             static_cast<long long>(out->launches[0].net_unix));
  } else {
    snprintf(out->error, sizeof(out->error), "no upcoming launch");
  }
  free(resp);
  return out->ok;
}
