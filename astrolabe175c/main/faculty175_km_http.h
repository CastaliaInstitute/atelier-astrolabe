#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

/** Register the paired Wi-Fi keyboard/mouse control surface. */
esp_err_t faculty175_km_http_register(httpd_handle_t server);
