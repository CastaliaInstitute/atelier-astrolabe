#pragma once

#include <cstddef>
#include <cstdint>

/** Allocate ring buffer; call once from setup. */
void pm_log_begin(void);

void pm_log_clear(void);
size_t pm_log_size(void);

/** Append bytes to the ring (always on after pm_log_begin). */
void pm_log_append(const char *data, size_t len);

/** Timestamped line to ring; mirrors to Serial when `mirror_serial`. */
void pm_log_printf(bool mirror_serial, const char *fmt, ...);

/** Write `[millis HH:MM:SS] ` into buf; returns bytes written. */
size_t pm_log_format_prefix(char *buf, size_t cap);

using PmLogHttpChunkFn = void (*)(const char *data, size_t len);
/** Stream ring contents oldest-first (for HTTP). */
void pm_log_stream_to_http(PmLogHttpChunkFn send_chunk);
