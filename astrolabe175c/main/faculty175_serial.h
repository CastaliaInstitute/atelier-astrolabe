#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Start USB serial command reader (`screen`, `qa status`, …). */
void faculty175_serial_init(void);
TaskHandle_t faculty175_serial_task_handle(void);

/** Request a TTS reading of the current face. */
bool faculty175_request_current_face_tts(void);
esp_err_t faculty175_request_qa_stt(uint32_t capture_ms);
esp_err_t faculty175_request_streaming_capture(uint32_t capture_ms);
esp_err_t faculty175_request_streaming_pipeline_stop(void);
esp_err_t faculty175_request_streaming_pipeline_restart(void);
void faculty175_streaming_pipeline_status(bool *out_configured,
                                          bool *out_created,
                                          bool *out_started,
                                          bool *out_starting);

#ifdef __cplusplus
}
#endif
