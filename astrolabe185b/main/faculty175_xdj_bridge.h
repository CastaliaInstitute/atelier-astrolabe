#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef enum {
    FACULTY175_XDJ_USB_DISCONNECTED = 0,
    FACULTY175_XDJ_USB_ENUMERATING,
    FACULTY175_XDJ_USB_READY,
    FACULTY175_XDJ_USB_ERROR,
} faculty175_xdj_usb_state_t;

esp_err_t faculty175_xdj_bridge_start(void);
esp_err_t faculty175_xdj_bridge_stop(void);
bool faculty175_xdj_bridge_running(void);
faculty175_xdj_usb_state_t faculty175_xdj_bridge_usb_state(void);
uint16_t faculty175_xdj_bridge_stream_port(void);
const char *faculty175_xdj_bridge_usb_state_name(void);
