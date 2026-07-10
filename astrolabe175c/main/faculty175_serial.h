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
void faculty175_streaming_pipeline_diag(bool *out_speech_active,
                                        bool *out_manual_pending,
                                        bool *out_manual_active,
                                        uint32_t *out_last_rms,
                                        uint32_t *out_noise_rms,
                                        uint32_t *out_start_threshold,
                                        uint32_t *out_capture_bytes,
                                        uint32_t *out_queued_segments,
                                        uint32_t *out_turn_segments,
                                        uint32_t *out_read_ok,
                                        uint32_t *out_read_zero,
                                        uint32_t *out_read_err,
                                        esp_err_t *out_last_read_err);

#ifdef __cplusplus
}
#endif
