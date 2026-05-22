#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct {
  int status_code;
  size_t bytes_read;
} PmHttpTextResult;

typedef struct {
  const char *name;
  const char *value;
} PmHttpHeader;

typedef struct {
  const char *name;
  char *value;
  size_t value_cap;
} PmHttpResponseHeader;

typedef bool (*PmHttpDataCallback)(const uint8_t *data, size_t len, void *ctx);

bool pm_http_request_stream_bytes(const char *url, const char *method, const uint8_t *body,
                                  size_t body_len, const PmHttpHeader *headers,
                                  size_t header_count, int timeout_ms,
                                  PmHttpDataCallback on_data, void *ctx,
                                  PmHttpTextResult *result = nullptr,
                                  PmHttpResponseHeader *response_headers = nullptr,
                                  size_t response_header_count = 0,
                                  int *content_length = nullptr);
bool pm_http_request_stream(const char *url, const char *method, const char *body,
                            const PmHttpHeader *headers, size_t header_count, int timeout_ms,
                            PmHttpDataCallback on_data, void *ctx,
                            PmHttpTextResult *result = nullptr);
bool pm_http_request_text_bytes(const char *url, const char *method, const uint8_t *body,
                                size_t body_len, const PmHttpHeader *headers,
                                size_t header_count, char *out, size_t out_cap,
                                int timeout_ms, PmHttpTextResult *result = nullptr,
                                PmHttpResponseHeader *response_headers = nullptr,
                                size_t response_header_count = 0,
                                int *content_length = nullptr);
bool pm_http_request_text(const char *url, const char *method, const char *body,
                          const PmHttpHeader *headers, size_t header_count, char *out,
                          size_t out_cap, int timeout_ms, PmHttpTextResult *result = nullptr);
bool pm_http_get_text(const char *url, char *out, size_t out_cap, int timeout_ms,
                      PmHttpTextResult *result = nullptr);
