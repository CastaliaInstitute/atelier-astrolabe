#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "esp_netif_ip_addr.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t faculty175_screen_http_start(const esp_ip4_addr_t *ip);
void faculty175_screen_http_stop(void);
esp_err_t faculty175_screen_http_wake_listener_start(void);
void faculty175_screen_http_wake_listener_stop(void);
bool faculty175_screen_http_take_wake_request(void);

#ifdef __cplusplus
}
#endif
