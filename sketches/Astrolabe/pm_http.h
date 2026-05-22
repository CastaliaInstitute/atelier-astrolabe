#pragma once

#include <stddef.h>

typedef struct {
  int status_code;
  size_t bytes_read;
} PmHttpTextResult;

typedef struct {
  const char *name;
  const char *value;
} PmHttpHeader;

bool pm_http_request_text(const char *url, const char *method, const char *body,
                          const PmHttpHeader *headers, size_t header_count, char *out,
                          size_t out_cap, int timeout_ms, PmHttpTextResult *result = nullptr);
bool pm_http_get_text(const char *url, char *out, size_t out_cap, int timeout_ms,
                      PmHttpTextResult *result = nullptr);
