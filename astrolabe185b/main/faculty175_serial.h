#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Start USB serial command reader (`screen`, `qa status`, …). */
void faculty175_serial_init(void);

/** Request a TTS reading of the current face. */
bool faculty175_request_current_face_tts(void);
esp_err_t faculty175_request_qa_stt(uint32_t capture_ms);

#ifdef __cplusplus
}
#endif
