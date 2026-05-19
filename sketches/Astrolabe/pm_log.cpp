#include "pm_log.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include <Arduino.h>
#include <esp_heap_caps.h>

#include "pm_config.h"
#include "pm_wifi_ntp.h"

#ifndef MYNAH_LOG_RING_BYTES
#define MYNAH_LOG_RING_BYTES (24 * 1024)
#endif

static char *g_buf = nullptr;
static size_t g_cap = 0;
static size_t g_w = 0;
static size_t g_size = 0;
static portMUX_TYPE g_log_mux = portMUX_INITIALIZER_UNLOCKED;

void pm_log_begin(void) {
  if (g_buf) {
    return;
  }
  g_cap = MYNAH_LOG_RING_BYTES;
  g_buf = static_cast<char *>(heap_caps_malloc(g_cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!g_buf) {
    g_cap = 8192;
    g_buf = static_cast<char *>(malloc(g_cap));
  }
  if (!g_buf) {
    g_cap = 0;
    return;
  }
  pm_log_clear();
  pm_log_printf(true, "log: ring %u bytes\n", static_cast<unsigned>(g_cap));
}

void pm_log_clear(void) {
  portENTER_CRITICAL(&g_log_mux);
  g_w = 0;
  g_size = 0;
  if (g_buf && g_cap > 0) {
    memset(g_buf, 0, g_cap);
  }
  portEXIT_CRITICAL(&g_log_mux);
}

size_t pm_log_size(void) {
  portENTER_CRITICAL(&g_log_mux);
  const size_t size = g_size;
  portEXIT_CRITICAL(&g_log_mux);
  return size;
}

void pm_log_append(const char *data, size_t len) {
  if (!g_buf || !data || len == 0) {
    return;
  }
  portENTER_CRITICAL(&g_log_mux);
  for (size_t i = 0; i < len; ++i) {
    g_buf[g_w] = data[i];
    g_w = (g_w + 1) % g_cap;
    if (g_size < g_cap) {
      ++g_size;
    }
  }
  portEXIT_CRITICAL(&g_log_mux);
}

size_t pm_log_format_prefix(char *buf, size_t cap) {
  if (!buf || cap < 8) {
    return 0;
  }
  int n = snprintf(buf, cap, "[%lu", static_cast<unsigned long>(millis()));
  if (n < 0) {
    return 0;
  }
  size_t off = static_cast<size_t>(n);
  if (off >= cap) {
    return cap - 1;
  }
  if (pm_time_valid()) {
    struct tm tm = {};
    pm_time_local(&tm);
    n = snprintf(buf + off, cap - off, " %02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
    if (n > 0) {
      off += static_cast<size_t>(n);
    }
  }
  if (off + 2 < cap) {
    buf[off++] = ']';
    buf[off++] = ' ';
    buf[off] = '\0';
  }
  return off;
}

void pm_log_printf(bool mirror_serial, const char *fmt, ...) {
  char line[256];
  size_t off = pm_log_format_prefix(line, sizeof(line));
  va_list ap;
  va_start(ap, fmt);
  const int n = vsnprintf(line + off, sizeof(line) - off, fmt, ap);
  va_end(ap);
  if (n < 0) {
    return;
  }
  off += static_cast<size_t>(n);
  if (off >= sizeof(line)) {
    off = sizeof(line) - 1;
  }
  if (off > 0 && line[off - 1] != '\n') {
    if (off + 1 < sizeof(line)) {
      line[off++] = '\n';
      line[off] = '\0';
    }
  }
  pm_log_append(line, off);
  if (mirror_serial) {
    Serial.print(line);
  }
}

void pm_log_stream_to_http(PmLogHttpChunkFn send_chunk) {
  if (!g_buf || g_size == 0 || !send_chunk) {
    return;
  }
  portENTER_CRITICAL(&g_log_mux);
  const size_t size = g_size;
  const size_t cap = g_cap;
  portEXIT_CRITICAL(&g_log_mux);

  char *snapshot = static_cast<char *>(heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!snapshot) {
    snapshot = static_cast<char *>(malloc(size));
  }
  if (snapshot) {
    portENTER_CRITICAL(&g_log_mux);
    const size_t copy_size = g_size < size ? g_size : size;
    const size_t start = (g_w + cap - copy_size) % cap;
    size_t copied = 0;
    size_t pos = start;
    while (copied < copy_size) {
      size_t chunk = cap - pos;
      if (chunk > copy_size - copied) {
        chunk = copy_size - copied;
      }
      memcpy(snapshot + copied, g_buf + pos, chunk);
      copied += chunk;
      pos = (pos + chunk) % cap;
    }
    portEXIT_CRITICAL(&g_log_mux);
    send_chunk(snapshot, copy_size);
    free(snapshot);
    return;
  }

  const size_t start = (g_w + g_cap - g_size) % g_cap;
  size_t sent = 0;
  size_t pos = start;
  while (sent < g_size) {
    size_t chunk = g_cap - pos;
    if (chunk > g_size - sent) {
      chunk = g_size - sent;
    }
    send_chunk(g_buf + pos, chunk);
    sent += chunk;
    pos = (pos + chunk) % g_cap;
  }
}
