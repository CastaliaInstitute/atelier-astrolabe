#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ASTROLABE_AUDIO_PIPELINE_EVENT_LISTENING = 0,
    ASTROLABE_AUDIO_PIPELINE_EVENT_CAPTURE_START,
    ASTROLABE_AUDIO_PIPELINE_EVENT_CAPTURE_QUEUED,
    ASTROLABE_AUDIO_PIPELINE_EVENT_THINKING,
    ASTROLABE_AUDIO_PIPELINE_EVENT_TRANSCRIPT,
    ASTROLABE_AUDIO_PIPELINE_EVENT_REPLY,
    ASTROLABE_AUDIO_PIPELINE_EVENT_SPEAKING,
    ASTROLABE_AUDIO_PIPELINE_EVENT_TURN_DONE,
    ASTROLABE_AUDIO_PIPELINE_EVENT_ERROR,
} astrolabe_audio_pipeline_event_t;

typedef enum {
    /** VAD writes the whole turn to flash, then posts the PCM file to voice-pipeline. */
    ASTROLABE_AUDIO_PIPELINE_TRANSPORT_FLASH_POST = 0,
    /** VAD writes rolling flash segments and sends each segment over voice-stream WebSocket. */
    ASTROLABE_AUDIO_PIPELINE_TRANSPORT_ROLLING_WEBSOCKET = 1,
} astrolabe_audio_pipeline_transport_t;

typedef esp_err_t (*astrolabe_audio_read_fn)(int16_t *samples,
                                             size_t sample_count,
                                             size_t *out_read,
                                             uint32_t timeout_ms,
                                             void *user);
typedef esp_err_t (*astrolabe_audio_write_fn)(const int16_t *samples,
                                              size_t sample_count,
                                              uint32_t timeout_ms,
                                              void *user);
typedef esp_err_t (*astrolabe_audio_set_rate_fn)(uint32_t sample_rate_hz, void *user);
typedef void (*astrolabe_audio_mute_fn)(bool mute, void *user);
typedef void (*astrolabe_audio_event_fn)(astrolabe_audio_pipeline_event_t event,
                                         const char *detail,
                                         void *user);
typedef void (*astrolabe_audio_result_fn)(const char *transcript,
                                          const char *reply,
                                          const char *faculty_slug,
                                          const char *faculty_name,
                                          void *user);
typedef void (*astrolabe_audio_prepare_context_fn)(void *user);
typedef esp_err_t (*astrolabe_audio_play_mp3_fn)(const uint8_t *mp3, size_t mp3_len, void *user);

typedef struct {
    astrolabe_audio_read_fn read;
    astrolabe_audio_write_fn write;
    astrolabe_audio_set_rate_fn set_rate;
    astrolabe_audio_mute_fn mute;
    void *user;
} astrolabe_audio_io_t;

typedef struct {
    astrolabe_audio_io_t io;
    astrolabe_audio_event_fn on_event;
    astrolabe_audio_result_fn on_result;
    astrolabe_audio_prepare_context_fn prepare_context;
    astrolabe_audio_play_mp3_fn play_mp3;
    void *event_user;

    const char *endpoint_url;
    const char *api_key;
    const char *face;
    const char *faculty_slug;
    const char *faculty_name;
    const char *system_instruction;
    const char *history;
    const char *stream_url;
    const char *interaction_mode;
    const char *commonplace_mode;
    const char *response_format;
    bool skip_llm;
    bool log_to_commonplace;
    bool duplex;
    astrolabe_audio_pipeline_transport_t transport;
    const char *capture_mount_path;
    const char *capture_partition_label;
    const char *capture_file_path;
    bool capture_skip_spiffs_mount;

    uint32_t sample_rate_hz;
    uint32_t stt_sample_rate_hz;
    size_t frame_samples;
    uint32_t rms_start;
    uint32_t rms_end;
    uint32_t start_frames;
    uint32_t silence_frames;
    uint32_t max_seconds;
    uint32_t min_ms;
    uint32_t capture_cooldown_ms;
    uint32_t capture_ring_slots;
    uint32_t capture_segment_ms;
    UBaseType_t listen_priority;
    UBaseType_t voice_priority;
    uint32_t listen_stack;
    uint32_t voice_stack;
} astrolabe_audio_pipeline_config_t;

typedef struct astrolabe_audio_pipeline astrolabe_audio_pipeline_t;

#define ASTROLABE_AUDIO_PIPELINE_WAVEFORM_LEN 128

esp_err_t astrolabe_audio_pipeline_create(const astrolabe_audio_pipeline_config_t *config,
                                          astrolabe_audio_pipeline_t **out_pipeline);
esp_err_t astrolabe_audio_pipeline_start(astrolabe_audio_pipeline_t *pipeline);
void astrolabe_audio_pipeline_stop(astrolabe_audio_pipeline_t *pipeline);
void astrolabe_audio_pipeline_destroy(astrolabe_audio_pipeline_t *pipeline);
esp_err_t astrolabe_audio_pipeline_trigger_capture(astrolabe_audio_pipeline_t *pipeline);
esp_err_t astrolabe_audio_pipeline_trigger_capture_for_ms(astrolabe_audio_pipeline_t *pipeline, uint32_t hold_ms);
bool astrolabe_audio_pipeline_unhealthy(const astrolabe_audio_pipeline_t *pipeline);

bool astrolabe_audio_pipeline_speech_active(const astrolabe_audio_pipeline_t *pipeline);
bool astrolabe_audio_pipeline_manual_capture_pending(const astrolabe_audio_pipeline_t *pipeline);
bool astrolabe_audio_pipeline_manual_capture_active(const astrolabe_audio_pipeline_t *pipeline);
uint32_t astrolabe_audio_pipeline_last_rms(const astrolabe_audio_pipeline_t *pipeline);
uint32_t astrolabe_audio_pipeline_noise_rms(const astrolabe_audio_pipeline_t *pipeline);
uint32_t astrolabe_audio_pipeline_start_threshold(const astrolabe_audio_pipeline_t *pipeline);
size_t astrolabe_audio_pipeline_capture_bytes(const astrolabe_audio_pipeline_t *pipeline);
UBaseType_t astrolabe_audio_pipeline_queued_segments(const astrolabe_audio_pipeline_t *pipeline);
uint32_t astrolabe_audio_pipeline_turn_segments(const astrolabe_audio_pipeline_t *pipeline);
uint32_t astrolabe_audio_pipeline_read_ok_count(const astrolabe_audio_pipeline_t *pipeline);
uint32_t astrolabe_audio_pipeline_read_zero_count(const astrolabe_audio_pipeline_t *pipeline);
uint32_t astrolabe_audio_pipeline_read_err_count(const astrolabe_audio_pipeline_t *pipeline);
esp_err_t astrolabe_audio_pipeline_last_read_err(const astrolabe_audio_pipeline_t *pipeline);
TaskHandle_t astrolabe_audio_pipeline_listen_task_handle(const astrolabe_audio_pipeline_t *pipeline);
TaskHandle_t astrolabe_audio_pipeline_voice_task_handle(const astrolabe_audio_pipeline_t *pipeline);
uint32_t astrolabe_audio_pipeline_listen_stack_bytes(const astrolabe_audio_pipeline_t *pipeline);
uint32_t astrolabe_audio_pipeline_voice_stack_bytes(const astrolabe_audio_pipeline_t *pipeline);
void astrolabe_audio_pipeline_waveform_copy(const astrolabe_audio_pipeline_t *pipeline, uint8_t *out, size_t len);
void astrolabe_audio_pipeline_waveform_stream_copy(const astrolabe_audio_pipeline_t *pipeline,
                                                   uint8_t *out,
                                                   size_t len);

#ifdef __cplusplus
}
#endif
