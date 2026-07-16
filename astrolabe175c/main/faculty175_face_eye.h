#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    bool initialized;
    bool host_installed;
    bool uvc_installed;
    bool stream_open;
    const char *state;
    esp_err_t last_error;
    uint16_t target_vid;
    uint16_t target_pid;
    uint16_t width;
    uint16_t height;
    uint16_t fps;
    uint32_t open_attempts;
    uint32_t disconnects;
    uint32_t transfer_errors;
    uint32_t frames_received;
    uint32_t frames_decoded;
} faculty175_eye_usb_status_t;

/** Start the USB host and UVC receive pipeline for the Astrolabe Eye accessory. */
esp_err_t faculty175_face_eye_init(void);

/** Draw the latest camera frame, or connection diagnostics while no frame is available. */
void faculty175_face_eye_draw(uint32_t anim_ms);

/** True after at least one valid camera frame has been decoded. */
bool faculty175_face_eye_has_frame(void);

/** Snapshot the USB-host/UVC pipeline for the Wi-Fi `/usb` diagnostic endpoint. */
void faculty175_face_eye_usb_status(faculty175_eye_usb_status_t *out);
