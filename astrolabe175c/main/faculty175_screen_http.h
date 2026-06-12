#pragma once

#include "esp_err.h"
#include "esp_netif_ip_addr.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t faculty175_screen_http_start(const esp_ip4_addr_t *ip);
void faculty175_screen_http_stop(void);

#ifdef __cplusplus
}
#endif
