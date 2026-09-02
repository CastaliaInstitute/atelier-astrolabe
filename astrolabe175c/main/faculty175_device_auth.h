#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "esp_http_client.h"

esp_err_t faculty175_device_auth_init(void);
esp_err_t faculty175_device_auth_headers(esp_http_client_handle_t client);
/** Build signed HTTP header lines for clients such as esp_websocket_client. */
esp_err_t faculty175_device_auth_header_text(char *out, size_t cap);
bool faculty175_device_auth_handle(const char *line);
esp_err_t faculty175_device_auth_mac(char *out, size_t cap);
/** One-time challenge for the Wi-Fi development console. */
esp_err_t faculty175_device_auth_console_challenge(char *out_nonce, size_t cap);
/** Consume a one-time console challenge signed with the device credential. */
bool faculty175_device_auth_console_verify(const char *nonce, const char *signature);
