#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "esp_netif_ip_addr.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t faculty175_usb_ncm_init(void);
esp_err_t faculty175_usb_ncm_wait_for_host(TickType_t timeout);
bool faculty175_usb_ncm_ready(void);
const esp_ip4_addr_t *faculty175_usb_ncm_ip(void);

#ifdef __cplusplus
}
#endif
