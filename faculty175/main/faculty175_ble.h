#pragma once

#include <stdbool.h>

#include "esp_err.h"

esp_err_t faculty175_ble_init(void);
bool faculty175_ble_enabled(void);
bool faculty175_ble_advertising(void);
esp_err_t faculty175_ble_set_enabled(bool enabled);
bool faculty175_ble_handle(const char *line);
