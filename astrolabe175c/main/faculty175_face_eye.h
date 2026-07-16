#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/** Start the USB host and UVC receive pipeline for the Astrolabe Eye accessory. */
esp_err_t faculty175_face_eye_init(void);

/** Draw the latest camera frame, or connection diagnostics while no frame is available. */
void faculty175_face_eye_draw(uint32_t anim_ms);

/** True after at least one valid camera frame has been decoded. */
bool faculty175_face_eye_has_frame(void);
