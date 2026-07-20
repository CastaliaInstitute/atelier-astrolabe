#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define FACULTY175_USB_SCREEN_MAX_JPEG (256u * 1024u)

/** Decode a baseline JPEG (up to 466x466) into the Astrolabe framebuffer. */
esp_err_t faculty175_usb_screen_show_jpeg(const uint8_t *jpeg, size_t length);

/** True when the USB Screen face has a buffered host frame. */
bool faculty175_usb_screen_active(void);

/** Draw the buffered frame, or a waiting state when no frame has arrived. */
void faculty175_usb_screen_draw_face(uint32_t anim_ms);

/** Clear the buffered frame. */
void faculty175_usb_screen_stop(void);
