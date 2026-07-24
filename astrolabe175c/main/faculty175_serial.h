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
/** Queue one authenticated Wi-Fi-console command for normal serial dispatch. */
esp_err_t faculty175_serial_submit_remote(const char *line);

typedef struct {
    uint32_t sequence;
    bool busy;
    uint32_t capture_ms;
    uint32_t started_ms;
    uint32_t completed_ms;
    esp_err_t err;
    char transcript[192];
    char reply[320];
} faculty175_qa_voice_status_t;

void faculty175_qa_voice_status(faculty175_qa_voice_status_t *out);

typedef struct {
    uint32_t sequence;
    bool busy;
    uint32_t started_ms;
    uint32_t completed_ms;
    esp_err_t err;
    char slug[32];
} faculty175_face_tts_status_t;

/** Snapshot the last/current per-face TTS request for remote tour verification. */
void faculty175_face_tts_status(faculty175_face_tts_status_t *out);

/** Request a TTS reading of the current face. */
bool faculty175_request_current_face_tts(void);
/** Run the concise LunaSay showcase faces in sequence, reading each one aloud. */
bool faculty175_request_face_tour(void);
/** A tour stops after its current spoken face; it never interrupts audio mid-sentence. */
void faculty175_request_face_tour_stop(void);
bool faculty175_face_tour_active(void);
esp_err_t faculty175_request_qa_stt(uint32_t capture_ms);
/** Queue STT so app_main can release the HTTP server's internal-RAM stack first. */
esp_err_t faculty175_request_qa_stt_deferred(uint32_t capture_ms);
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
