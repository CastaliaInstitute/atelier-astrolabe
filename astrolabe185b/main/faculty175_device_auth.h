#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "esp_http_client.h"

esp_err_t faculty175_device_auth_init(void);
esp_err_t faculty175_device_auth_headers(esp_http_client_handle_t client);
bool faculty175_device_auth_handle(const char *line);
esp_err_t faculty175_device_auth_mac(char *out, size_t cap);
