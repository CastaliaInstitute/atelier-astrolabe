#pragma once

#include <stddef.h>

typedef struct {
  int status_code;
  size_t bytes_read;
} PmHttpTextResult;

bool pm_http_get_text(const char *url, char *out, size_t out_cap, int timeout_ms,
                      PmHttpTextResult *result = nullptr);
