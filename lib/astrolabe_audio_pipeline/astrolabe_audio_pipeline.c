#include "astrolabe_audio_pipeline.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_memory_utils.h"
#include "esp_spiffs.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/portmacro.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

static const char *TAG = "ast_audio_pipe";

#define DEFAULT_SAMPLE_RATE_HZ 16000u
#define DEFAULT_FRAME_SAMPLES 320u
#define DEFAULT_RMS_START 550u
#define DEFAULT_RMS_END 280u
#define DEFAULT_START_FRAMES 3u
#define DEFAULT_SILENCE_FRAMES 40u
#define DEFAULT_MAX_SECONDS 15u
#define DEFAULT_SEGMENT_MS 3000u
#define DEFAULT_RING_SLOTS 8u
#define MAX_RING_SLOTS 16u
/* Retain up to 32 seconds for the HTTP recovery path without limiting the
 * primary rolling stream. Longer turns continue streaming; they simply lose
 * whole-turn replay once this diagnostic mirror is full. */
#define MIN_TURN_MIRROR_MAX_BYTES (1024 * 1024u)
#define DEFAULT_MIN_MS 400u
#define DEFAULT_CAPTURE_COOLDOWN_MS 2500u
#define DEFAULT_LISTEN_STACK 4096u
#define DEFAULT_VOICE_STACK 8192u
#define DEFAULT_CAPTURE_MOUNT_PATH "/spiffs"
#define DEFAULT_CAPTURE_FILE_PATH "/spiffs/astrolabe_utterance.pcm"
#define HTTP_TIMEOUT_MS 660000
#define VOICE_RESP_MAX_BYTES (768 * 1024)
#define POST_PCM_CHUNK_BYTES 2048
#define VOICE_STREAM_CT "application/vnd.astrolabe.voice-stream"
/* 250 ms of mono PCM16 at 16 kHz. WebSocket messages are transport frames,
 * not logical turn chunks: audio is appended continuously and committed only
 * once, after VAD marks the end of the speaker's turn. */
#define STREAM_PCM_CHUNK_BYTES 8192
#define STREAM_CONNECT_TIMEOUT_MS 5000
#define STREAM_SEND_TIMEOUT_MS 5000
#define STREAM_FRAME_PACE_MS 0
#define STREAM_AUDIO_SEGMENT_QUEUE_LEN 4
#define STREAM_RESPONSE_TIMEOUT_MS 120000
#define STREAM_EARLY_RESPONSE_TIMEOUT_MS 8000
#define STREAM_WEBSOCKET_TASK_STACK 6144u
#define STREAM_WEBSOCKET_BUFFER_SIZE 16384u
#define STREAM_OPEN_RETRIES 2u
#define MANUAL_CAPTURE_MIN_MS 1500u
#define MANUAL_CAPTURE_DEFAULT_HOLD_MS 2500u
#define MANUAL_CAPTURE_PRESTART_QUIET_FRAMES 4u
#define MANUAL_CAPTURE_PRESTART_FORCE_MS 750u
#define MANUAL_CAPTURE_NO_SPEECH_TIMEOUT_MS 8000u
#define VAD_WARMUP_FRAMES 25u
#define VAD_NOISE_ATTACK_SHIFT 6
#define VAD_NOISE_RELEASE_SHIFT 4
#define VAD_START_MIN_DELTA_RMS 250u
#define VAD_END_MIN_DELTA_RMS 180u
#define VAD_REARM_QUIET_FRAMES 75u
#define VAD_REARM_FORCE_MS 6000u
#define VAD_SILENCE_LEAK_FRAMES 4u
/* Start confirmation is only 60 ms. Four hundred milliseconds protects the
 * leading consonant while avoiding a 38.4 kB burst that can put live upload
 * several seconds behind the microphone at the start of every turn. */
#define VAD_PREROLL_MS 400u

typedef struct {
    char path[128];
    uint8_t *pcm_data;
    size_t byte_count;
    uint32_t sequence;
    uint32_t slot;
    bool final_segment;
} utterance_t;

typedef struct voice_result voice_result_t;
typedef struct stream_response_ctx stream_response_ctx_t;

typedef struct {
    uint8_t *mp3;
    size_t mp3_len;
} stream_audio_segment_t;

struct astrolabe_audio_pipeline {
    astrolabe_audio_pipeline_config_t cfg;
    QueueHandle_t utterance_queue;
    TaskHandle_t listen_task;
    TaskHandle_t voice_task;
    StackType_t *listen_task_stack_storage;
    StaticTask_t *listen_task_tcb_storage;
    uint32_t listen_task_stack_words;
    bool owns_listen_task_storage;
    StackType_t *voice_task_stack_storage;
    StaticTask_t *voice_task_tcb_storage;
    uint32_t voice_task_stack_words;
    int capture_fd;
    char capture_path[128];
    char capture_base_path[128];
    size_t capture_cap_bytes;
    size_t segment_cap_bytes;
    size_t capture_len_bytes;
    uint32_t capture_slot;
    uint32_t capture_sequence;
    uint32_t turn_segment_count;
    uint32_t capture_ring_slots;
    uint32_t capture_started_ms;
    uint32_t manual_capture_hold_ms;
    uint32_t manual_capture_deadline_ms;
    uint32_t manual_capture_armed_ms;
    uint32_t manual_capture_quiet_frames;
    bool capture_slot_busy[MAX_RING_SLOTS];
    bool speech_active;
    volatile bool manual_capture;
    bool manual_capture_active;
    bool unhealthy;
    uint8_t *capture_ram;
    size_t capture_ram_cap;
    uint8_t *turn_pcm;
    size_t turn_pcm_len;
    size_t turn_pcm_cap;
    uint8_t *vad_preroll;
    size_t vad_preroll_cap;
    size_t vad_preroll_len;
    size_t vad_preroll_write;
    bool turn_transport_failed;
    bool turn_pcm_truncated;
    uint32_t silence_frames;
    uint32_t speech_frames;
    uint32_t last_rms;
    uint32_t noise_rms;
    uint32_t active_peak_rms;
    uint32_t vad_frames_seen;
    uint32_t read_ok_count;
    uint32_t read_zero_count;
    uint32_t read_err_count;
    esp_err_t last_read_err;
    uint32_t rearm_quiet_frames;
    uint32_t capture_blocked_until_ms;
    bool capture_needs_quiet;
    portMUX_TYPE waveform_mux;
    uint8_t waveform[ASTROLABE_AUDIO_PIPELINE_WAVEFORM_LEN];
    uint8_t waveform_stream[ASTROLABE_AUDIO_PIPELINE_WAVEFORM_LEN];
    uint16_t waveform_head;
    bool running;
    volatile bool listen_should_run;
    bool spiffs_mounted;
    esp_websocket_client_handle_t rolling_client;
    voice_result_t *rolling_result;
    stream_response_ctx_t *rolling_response;
};

struct voice_result {
    char transcript[320];
    char reply[768];
    char faculty_slug[64];
    char faculty_name[96];
    char session_id[64];
    char expression[16];
    uint8_t *mp3;
    size_t mp3_len;
};

struct stream_response_ctx {
    voice_result_t *result;
    char *message;
    size_t message_cap;
    size_t message_len;
    volatile uint32_t event_count;
    int websocket_status_code;
    esp_err_t err;
    bool response_done;
    bool transport_failed;
    QueueHandle_t audio_segments;
    uint8_t *pending_segment_mp3;
    size_t pending_segment_mp3_len;
    bool segmented_audio_played;
};

static uint32_t ticks_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static bool ticks_reached(uint32_t now_ms, uint32_t deadline_ms)
{
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

static uint32_t dynamic_start_threshold(const astrolabe_audio_pipeline_t *p);
static const char *cfg_interaction_mode(const astrolabe_audio_pipeline_t *p);
static void listen_task(void *arg);
static void voice_task(void *arg);
static void voice_result_free(voice_result_t *r);
static esp_err_t play_mp3(astrolabe_audio_pipeline_t *p, const uint8_t *mp3, size_t mp3_len);
static esp_err_t websocket_send_text_all(esp_websocket_client_handle_t client, const char *text);
static void stream_response_event(void *handler_arg, esp_event_base_t base, int32_t event_id, void *event_data);
static esp_err_t rolling_stream_session_open(astrolabe_audio_pipeline_t *p);
static esp_err_t post_pcm_buffer(astrolabe_audio_pipeline_t *p, const uint8_t *pcm_data, size_t pcm_len, voice_result_t *result);

static uint32_t cfg_stt_sample_rate_hz(const astrolabe_audio_pipeline_t *p)
{
    if (p != NULL && p->cfg.stt_sample_rate_hz > 0) {
        return p->cfg.stt_sample_rate_hz;
    }
    return p != NULL && p->cfg.sample_rate_hz > 0 ? p->cfg.sample_rate_hz : DEFAULT_SAMPLE_RATE_HZ;
}

static bool cfg_synthetic_validation(const astrolabe_audio_pipeline_t *p)
{
    if (p == NULL) return false;
    return p->cfg.synthetic_validation_ref != NULL
               ? *p->cfg.synthetic_validation_ref
               : p->cfg.synthetic_validation;
}

static void start_capture_cooldown(astrolabe_audio_pipeline_t *p)
{
    if (p == NULL || p->cfg.capture_cooldown_ms == 0) {
        return;
    }
    p->capture_blocked_until_ms = ticks_ms() + p->cfg.capture_cooldown_ms;
    p->capture_needs_quiet = true;
    p->rearm_quiet_frames = 0;
    p->speech_frames = 0;
}

static void emit(astrolabe_audio_pipeline_t *p, astrolabe_audio_pipeline_event_t event, const char *detail)
{
    if (p != NULL && p->cfg.on_event != NULL) {
        p->cfg.on_event(event, detail, p->cfg.event_user);
    }
}

static bool rolling_websocket_enabled(const astrolabe_audio_pipeline_t *p)
{
    return p != NULL && p->cfg.transport == ASTROLABE_AUDIO_PIPELINE_TRANSPORT_ROLLING_WEBSOCKET &&
           strcmp(cfg_interaction_mode(p), "conversation") == 0 &&
           p->cfg.face != NULL && strcmp(p->cfg.face, "theritor") == 0 &&
           p->cfg.stream_url != NULL && p->cfg.stream_url[0] != '\0';
}

static bool capture_uses_ram(const astrolabe_audio_pipeline_t *p)
{
    /* Rolling Theritor must never perform SPIFFS close/read/open cycles on the
     * real-time listener. Those flash operations drop more audio than each
     * 500 ms segment contains. Batch faces retain their durable flash spool. */
    return rolling_websocket_enabled(p);
}

static bool should_idle_listen_between_turns(const astrolabe_audio_pipeline_t *p)
{
    return p != NULL && !rolling_websocket_enabled(p);
}

static void prepare_context(astrolabe_audio_pipeline_t *p)
{
    if (p != NULL && p->cfg.prepare_context != NULL) {
        p->cfg.prepare_context(p->cfg.event_user);
    }
}

static void turn_pcm_reset(astrolabe_audio_pipeline_t *p)
{
    if (p == NULL) {
        return;
    }
    p->turn_pcm_len = 0;
    p->turn_transport_failed = false;
    p->turn_pcm_truncated = false;
}

static size_t turn_pcm_mirror_max_bytes(const astrolabe_audio_pipeline_t *p)
{
    if (p == NULL || !rolling_websocket_enabled(p)) {
        return p != NULL ? p->capture_cap_bytes : 0;
    }
    size_t cap = p->segment_cap_bytes * (size_t)p->capture_ring_slots;
    if (cap < MIN_TURN_MIRROR_MAX_BYTES) {
        cap = MIN_TURN_MIRROR_MAX_BYTES;
    }
    return cap;
}

static bool turn_pcm_append(astrolabe_audio_pipeline_t *p, const uint8_t *data, size_t len)
{
    if (p == NULL || data == NULL || len == 0) {
        return true;
    }
    const size_t mirror_max = turn_pcm_mirror_max_bytes(p);
    if (mirror_max == 0 || p->turn_pcm_len >= mirror_max) {
        p->turn_pcm_truncated = true;
        return true;
    }
    if (len > mirror_max - p->turn_pcm_len) {
        len = mirror_max - p->turn_pcm_len;
        p->turn_pcm_truncated = true;
    }
    if (p->turn_pcm_len > SIZE_MAX - len) {
        p->turn_pcm_truncated = true;
        return false;
    }
    const size_t need = p->turn_pcm_len + len;
    if (need > p->turn_pcm_cap) {
        size_t next = p->turn_pcm_cap > 0 ? p->turn_pcm_cap : 8192;
        while (next < need) {
            next *= 2;
        }
        if (next > mirror_max) {
            next = mirror_max;
        }
        uint8_t *grown = heap_caps_realloc(p->turn_pcm, next, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (grown == NULL) {
            grown = realloc(p->turn_pcm, next);
        }
        if (grown == NULL) {
            p->turn_pcm_truncated = true;
            return false;
        }
        p->turn_pcm = grown;
        p->turn_pcm_cap = next;
    }
    memcpy(p->turn_pcm + p->turn_pcm_len, data, len);
    p->turn_pcm_len = need;
    return true;
}

static void wait_for_task_exit(TaskHandle_t *handle, uint32_t timeout_ms)
{
    if (handle == NULL) {
        return;
    }
    const uint32_t deadline = ticks_ms() + timeout_ms;
    while (*handle != NULL && !ticks_reached(ticks_ms(), deadline)) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void *pipeline_calloc_prefer_internal(size_t count, size_t size)
{
    void *ptr = heap_caps_calloc(count, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (ptr == NULL) {
        ptr = heap_caps_calloc(count, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (ptr == NULL) {
        ptr = calloc(count, size);
    }
    return ptr;
}

static void *pipeline_calloc_prefer_psram(size_t count, size_t size)
{
    void *ptr = heap_caps_calloc(count, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ptr == NULL) {
        ptr = heap_caps_calloc(count, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (ptr == NULL) {
        ptr = calloc(count, size);
    }
    return ptr;
}

static StackType_t *pipeline_alloc_stack(size_t words, bool prefer_psram)
{
    const size_t bytes = words * sizeof(StackType_t);
    StackType_t *stack = NULL;
    if (prefer_psram) {
        stack = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (stack == NULL) {
        stack = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (stack == NULL && !prefer_psram) {
        stack = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    return stack;
}

static esp_err_t start_listen_task_if_needed(astrolabe_audio_pipeline_t *p)
{
    if (p == NULL || !p->running) {
        return ESP_ERR_INVALID_STATE;
    }
    if (p->listen_task != NULL) {
        return ESP_OK;
    }
    const uint32_t stack = p->cfg.listen_stack ? p->cfg.listen_stack : DEFAULT_LISTEN_STACK;
    if (p->listen_task_stack_storage == NULL || p->listen_task_tcb_storage == NULL || p->listen_task_stack_words == 0) {
        ESP_LOGE(TAG, "listen task create failed stack=%u internal=%u largest=%u",
                 (unsigned)stack,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        p->listen_should_run = false;
        p->listen_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    p->listen_should_run = true;
    p->listen_task = xTaskCreateStatic(listen_task,
                                       "ast_audio_listen",
                                       p->listen_task_stack_words,
                                       p,
                                       p->cfg.listen_priority ? p->cfg.listen_priority : 5,
                                       p->listen_task_stack_storage,
                                       p->listen_task_tcb_storage);
    if (p->listen_task == NULL) {
        ESP_LOGE(TAG, "listen task create failed stack=%u internal=%u largest=%u",
                 (unsigned)stack,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        p->listen_should_run = false;
        p->listen_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static esp_err_t start_voice_task_if_needed(astrolabe_audio_pipeline_t *p)
{
    if (p == NULL || !p->running) {
        return ESP_ERR_INVALID_STATE;
    }
    if (p->voice_task != NULL) {
        return ESP_OK;
    }
    const uint32_t stack = p->cfg.voice_stack ? p->cfg.voice_stack : DEFAULT_VOICE_STACK;
    if (p->voice_task_stack_storage == NULL || p->voice_task_tcb_storage == NULL || p->voice_task_stack_words == 0) {
        ESP_LOGE(TAG, "voice task create failed stack=%u internal=%u largest=%u",
                 (unsigned)stack,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        return ESP_ERR_NO_MEM;
    }
    p->voice_task = xTaskCreateStatic(voice_task,
                                      "ast_audio_voice",
                                      p->voice_task_stack_words,
                                      p,
                                      p->cfg.voice_priority ? p->cfg.voice_priority : 4,
                                      p->voice_task_stack_storage,
                                      p->voice_task_tcb_storage);
    if (p->voice_task == NULL) {
        ESP_LOGE(TAG, "voice task create failed stack=%u internal=%u largest=%u",
                 (unsigned)stack,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static void stop_listen_task_if_running(astrolabe_audio_pipeline_t *p, uint32_t timeout_ms)
{
    if (p == NULL || p->listen_task == NULL) {
        return;
    }
    p->listen_should_run = false;
    wait_for_task_exit(&p->listen_task, timeout_ms);
}

static char *json_escape_alloc(const char *src)
{
    if (src == NULL) {
        return strdup("");
    }
    size_t cap = strlen(src) * 2 + 8;
    char *out = heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (out == NULL) {
        out = malloc(cap);
    }
    if (out == NULL) {
        return NULL;
    }
    size_t w = 0;
    for (const unsigned char *s = (const unsigned char *)src; *s != '\0' && w + 2 < cap; ++s) {
        if (*s == '"' || *s == '\\') {
            out[w++] = '\\';
        }
        out[w++] = (char)*s;
    }
    out[w] = '\0';
    return out;
}

static const char *cfg_response_format(const astrolabe_audio_pipeline_t *p)
{
    return p != NULL && p->cfg.response_format != NULL && p->cfg.response_format[0] != '\0'
               ? p->cfg.response_format
               : "mp3";
}

static const char *cfg_interaction_mode(const astrolabe_audio_pipeline_t *p)
{
    return p != NULL && p->cfg.interaction_mode != NULL && p->cfg.interaction_mode[0] != '\0'
               ? p->cfg.interaction_mode
               : "conversation";
}

static const char *cfg_commonplace_mode(const astrolabe_audio_pipeline_t *p)
{
    return p != NULL && p->cfg.commonplace_mode != NULL && p->cfg.commonplace_mode[0] != '\0'
               ? p->cfg.commonplace_mode
               : "conversation";
}

static bool cfg_skip_llm(const astrolabe_audio_pipeline_t *p)
{
    const char *mode = cfg_interaction_mode(p);
    return (p != NULL && p->cfg.skip_llm) || strcmp(mode, "journal") == 0 || strcmp(mode, "transcribe") == 0;
}

static bool cfg_log_to_commonplace(const astrolabe_audio_pipeline_t *p)
{
    const char *mode = cfg_commonplace_mode(p);
    return (p != NULL && p->cfg.log_to_commonplace) || strcmp(mode, "journal") == 0 ||
           strcmp(mode, "conversation") == 0;
}

static const char *json_find_string(const char *body, const char *key, char *out, size_t cap)
{
    if (body == NULL || key == NULL || out == NULL || cap == 0) {
        return NULL;
    }
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *start = strstr(body, pattern);
    if (start == NULL) {
        out[0] = '\0';
        return NULL;
    }
    start += strlen(pattern);
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') {
        ++start;
    }
    if (*start++ != ':') {
        out[0] = '\0';
        return NULL;
    }
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') {
        ++start;
    }
    if (*start++ != '"') {
        out[0] = '\0';
        return NULL;
    }
    size_t w = 0;
    while (*start != '\0' && *start != '"' && w + 1 < cap) {
        if (*start == '\\' && start[1] != '\0') {
            ++start;
        }
        out[w++] = *start++;
    }
    out[w] = '\0';
    return out;
}

static bool extract_audio_base64(const char *json, uint8_t **out_bin, size_t *out_len)
{
    const char *key = "\"audioBase64\":\"";
    const char *start = json != NULL ? strstr(json, key) : NULL;
    if (start == NULL || out_bin == NULL || out_len == NULL) {
        return false;
    }
    start += strlen(key);
    const char *end = start;
    while (*end != '\0' && *end != '"') {
        ++end;
    }
    const size_t b64_len = (size_t)(end - start);
    if (b64_len == 0) {
        return false;
    }
    const size_t cap = (b64_len / 4) * 3 + 256;
    uint8_t *buf = heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buf == NULL) {
        buf = malloc(cap);
    }
    if (buf == NULL) {
        return false;
    }
    size_t olen = 0;
    if (mbedtls_base64_decode(buf, cap, &olen, (const unsigned char *)start, b64_len) != 0 || olen == 0) {
        free(buf);
        return false;
    }
    *out_bin = buf;
    *out_len = olen;
    return true;
}

static bool looks_like_mp3(const uint8_t *data, size_t len)
{
    if (data == NULL || len < 3) {
        return false;
    }
    if (data[0] == 0x49 && data[1] == 0x44 && data[2] == 0x33) {
        return true;
    }
    return len >= 2 && data[0] == 0xff && (data[1] & 0xe0u) == 0xe0u;
}

static bool decode_base64_alloc(const char *b64, size_t b64_len, uint8_t **out_bin, size_t *out_len)
{
    if (b64 == NULL || b64_len == 0 || out_bin == NULL || out_len == NULL) {
        return false;
    }
    const size_t cap = (b64_len / 4) * 3 + 256;
    uint8_t *buf = heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buf == NULL) {
        buf = malloc(cap);
    }
    if (buf == NULL) {
        return false;
    }
    size_t olen = 0;
    if (mbedtls_base64_decode(buf, cap, &olen, (const unsigned char *)b64, b64_len) != 0 || olen == 0) {
        free(buf);
        return false;
    }
    *out_bin = buf;
    *out_len = olen;
    return true;
}

static bool json_find_string_alloc(const char *body, const char *key, char **out, size_t *out_len)
{
    if (body == NULL || key == NULL || out == NULL || out_len == NULL) {
        return false;
    }
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
    const char *start = strstr(body, pattern);
    if (start == NULL) {
        return false;
    }
    start += strlen(pattern);
    size_t cap = strlen(start) + 1;
    char *buf = heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buf == NULL) {
        buf = malloc(cap);
    }
    if (buf == NULL) {
        return false;
    }
    size_t w = 0;
    while (*start != '\0' && *start != '"' && w + 1 < cap) {
        if (*start == '\\' && start[1] != '\0') {
            ++start;
        }
        buf[w++] = *start++;
    }
    buf[w] = '\0';
    *out = buf;
    *out_len = w;
    return true;
}

static void url_decode_in_place(char *value)
{
    if (value == NULL) {
        return;
    }
    char *r = value;
    char *w = value;
    while (*r != '\0') {
        if (*r == '%' && r[1] != '\0' && r[2] != '\0') {
            char hex[3] = { r[1], r[2], '\0' };
            char *end = NULL;
            const long v = strtol(hex, &end, 16);
            if (end != NULL && *end == '\0' && v >= 0 && v <= 255) {
                *w++ = (char)v;
                r += 3;
                continue;
            }
        }
        if (*r == '+') {
            *w++ = ' ';
            r++;
            continue;
        }
        *w++ = *r++;
    }
    *w = '\0';
}

static void capture_voice_response_headers(esp_http_client_handle_t client, voice_result_t *result)
{
    if (client == NULL || result == NULL) {
        return;
    }
    const char *header = NULL;
    char *raw_header = NULL;
    if (esp_http_client_get_header(client, "X-Faculty-Slug", &raw_header) == ESP_OK && raw_header != NULL &&
        raw_header[0] != '\0') {
        header = raw_header;
    } else if (esp_http_client_get_header(client, "x-faculty-slug", &raw_header) == ESP_OK && raw_header != NULL &&
               raw_header[0] != '\0') {
        header = raw_header;
    }
    if (header != NULL && result->faculty_slug[0] == '\0') {
        strlcpy(result->faculty_slug, header, sizeof(result->faculty_slug));
        url_decode_in_place(result->faculty_slug);
    }
    header = NULL;
    raw_header = NULL;
    if (esp_http_client_get_header(client, "X-Faculty-Name", &raw_header) == ESP_OK && raw_header != NULL &&
        raw_header[0] != '\0') {
        header = raw_header;
    } else if (esp_http_client_get_header(client, "x-faculty-name", &raw_header) == ESP_OK && raw_header != NULL &&
               raw_header[0] != '\0') {
        header = raw_header;
    }
    if (header != NULL && result->faculty_name[0] == '\0') {
        strlcpy(result->faculty_name, header, sizeof(result->faculty_name));
        url_decode_in_place(result->faculty_name);
    }
    header = NULL;
    raw_header = NULL;
    if (esp_http_client_get_header(client, "X-Voice-Transcript", &raw_header) == ESP_OK && raw_header != NULL &&
        raw_header[0] != '\0') {
        header = raw_header;
    } else if (esp_http_client_get_header(client, "x-voice-transcript", &raw_header) == ESP_OK && raw_header != NULL &&
               raw_header[0] != '\0') {
        header = raw_header;
    }
    if (header != NULL && result->transcript[0] == '\0') {
        strlcpy(result->transcript, header, sizeof(result->transcript));
        url_decode_in_place(result->transcript);
    }
    header = NULL;
    raw_header = NULL;
    if (esp_http_client_get_header(client, "X-Voice-Reply", &raw_header) == ESP_OK && raw_header != NULL &&
        raw_header[0] != '\0') {
        header = raw_header;
    } else if (esp_http_client_get_header(client, "x-voice-reply", &raw_header) == ESP_OK && raw_header != NULL &&
               raw_header[0] != '\0') {
        header = raw_header;
    }
    if (header != NULL && result->reply[0] == '\0') {
        strlcpy(result->reply, header, sizeof(result->reply));
        url_decode_in_place(result->reply);
    }
    header = NULL;
    raw_header = NULL;
    if (esp_http_client_get_header(client, "X-Theritor-Session", &raw_header) == ESP_OK && raw_header != NULL &&
        raw_header[0] != '\0') {
        header = raw_header;
    } else if (esp_http_client_get_header(client, "x-theritor-session", &raw_header) == ESP_OK && raw_header != NULL &&
               raw_header[0] != '\0') {
        header = raw_header;
    }
    if (header != NULL) {
        strlcpy(result->session_id, header, sizeof(result->session_id));
        url_decode_in_place(result->session_id);
    }
    header = NULL;
    raw_header = NULL;
    if (esp_http_client_get_header(client, "X-Theritor-Expression", &raw_header) == ESP_OK && raw_header != NULL &&
        raw_header[0] != '\0') {
        header = raw_header;
    } else if (esp_http_client_get_header(client, "x-theritor-expression", &raw_header) == ESP_OK && raw_header != NULL &&
               raw_header[0] != '\0') {
        header = raw_header;
    }
    if (header != NULL) {
        strlcpy(result->expression, header, sizeof(result->expression));
        url_decode_in_place(result->expression);
    }
}

static void append_text(char *dst, size_t cap, const char *src)
{
    if (dst == NULL || cap == 0 || src == NULL || src[0] == '\0') {
        return;
    }
    const size_t used = strlen(dst);
    if (used + 1 >= cap) {
        return;
    }
    strlcpy(dst + used, src, cap - used);
}

static bool append_bytes(uint8_t **dst, size_t *dst_len, const uint8_t *src, size_t src_len)
{
    if (dst == NULL || dst_len == NULL || src == NULL || src_len == 0) {
        return false;
    }
    const size_t old_len = *dst_len;
    if (old_len > SIZE_MAX - src_len) {
        return false;
    }
    const size_t new_len = old_len + src_len;
    uint8_t *grown = heap_caps_realloc(*dst, new_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (grown == NULL) {
        grown = realloc(*dst, new_len);
    }
    if (grown == NULL) {
        return false;
    }
    memcpy(grown + old_len, src, src_len);
    *dst = grown;
    *dst_len = new_len;
    return true;
}

static void voice_result_free(voice_result_t *r)
{
    if (r != NULL && r->mp3 != NULL) {
        free(r->mp3);
        r->mp3 = NULL;
        r->mp3_len = 0;
    }
}

static esp_err_t http_write_all(esp_http_client_handle_t client, const void *data, size_t len)
{
    size_t written = 0;
    while (written < len) {
        int n = esp_http_client_write(client, (const char *)data + written, (int)(len - written));
        if (n <= 0) {
            return ESP_FAIL;
        }
        written += (size_t)n;
    }
    return ESP_OK;
}

static esp_err_t read_response_body(esp_http_client_handle_t client, uint8_t **out_body, size_t *out_len)
{
    size_t cap = 64 * 1024;
    uint8_t *response = heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (response == NULL) {
        response = malloc(cap);
    }
    if (response == NULL) {
        return ESP_ERR_NO_MEM;
    }
    size_t total = 0;
    while (true) {
        if (total == cap) {
            size_t next = cap * 2;
            if (next > VOICE_RESP_MAX_BYTES) {
                free(response);
                return ESP_ERR_NO_MEM;
            }
            uint8_t *grown = heap_caps_realloc(response, next, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (grown == NULL) {
                grown = realloc(response, next);
            }
            if (grown == NULL) {
                free(response);
                return ESP_ERR_NO_MEM;
            }
            response = grown;
            cap = next;
        }
        int n = esp_http_client_read(client, (char *)response + total, (int)(cap - total));
        if (n < 0) {
            free(response);
            return ESP_FAIL;
        }
        if (n == 0) {
            break;
        }
        total += (size_t)n;
    }
    *out_body = response;
    *out_len = total;
    return ESP_OK;
}

static esp_err_t fetch_tts_for_reply(astrolabe_audio_pipeline_t *p, voice_result_t *result)
{
    if (p == NULL || result == NULL || result->reply[0] == '\0' || p->cfg.endpoint_url == NULL ||
        p->cfg.endpoint_url[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    char *message = json_escape_alloc(result->transcript[0] != '\0' ? result->transcript : result->reply);
    char *reply = json_escape_alloc(result->reply);
    char *face = json_escape_alloc(p->cfg.face);
    char *slug = json_escape_alloc(result->faculty_slug[0] != '\0' ? result->faculty_slug : p->cfg.faculty_slug);
    char *name = json_escape_alloc(result->faculty_name[0] != '\0' ? result->faculty_name : p->cfg.faculty_name);
    if (message == NULL || reply == NULL || face == NULL || slug == NULL || name == NULL) {
        free(message);
        free(reply);
        free(face);
        free(slug);
        free(name);
        return ESP_ERR_NO_MEM;
    }

    const size_t body_cap = strlen(message) + strlen(reply) + strlen(face) + strlen(slug) + strlen(name) + 320;
    char *body = heap_caps_malloc(body_cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (body == NULL) {
        body = malloc(body_cap);
    }
    if (body == NULL) {
        free(message);
        free(reply);
        free(face);
        free(slug);
        free(name);
        return ESP_ERR_NO_MEM;
    }
    int body_len = snprintf(
        body,
        body_cap,
        "{\"ttsText\":\"%s\",\"message\":\"%s\",\"languageCode\":\"en-US\","
        "\"face\":\"%s\",\"facultySlug\":\"%s\",\"facultyName\":\"%s\","
        "\"responseFormat\":\"json\",\"generateTts\":true,\"skipLlm\":true,"
        "\"logToCommonplace\":false,\"commonplaceMode\":\"off\"}",
        reply,
        message,
        face,
        slug,
        name
    );
    free(message);
    free(reply);
    free(face);
    free(slug);
    free(name);
    if (body_len <= 0 || (size_t)body_len >= body_cap) {
        free(body);
        return ESP_ERR_NO_MEM;
    }

    esp_http_client_config_t http_cfg = {
        .url = p->cfg.endpoint_url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 4096,
        .buffer_size_tx = 2048,
    };
    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    if (client == NULL) {
        free(body);
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Accept", "application/json");
    if (p->cfg.api_key != NULL && p->cfg.api_key[0] != '\0') {
        esp_http_client_set_header(client, "apikey", p->cfg.api_key);
        char auth[512];
        snprintf(auth, sizeof(auth), "Bearer %s", p->cfg.api_key);
        esp_http_client_set_header(client, "Authorization", auth);
    }
    if (p->cfg.request_headers != NULL && p->cfg.request_headers(client, p->cfg.event_user) != ESP_OK) {
        esp_http_client_cleanup(client);
        free(body);
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = esp_http_client_open(client, (int)body_len);
    if (err == ESP_OK) {
        err = http_write_all(client, body, (size_t)body_len);
    }
    free(body);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return err;
    }

    const int status = esp_http_client_fetch_headers(client);
    (void)status;
    uint8_t *response = NULL;
    size_t response_len = 0;
    err = read_response_body(client, &response, &response_len);
    if (err == ESP_OK) {
        if (esp_http_client_get_status_code(client) < 200 || esp_http_client_get_status_code(client) >= 300) {
            err = ESP_FAIL;
        }
    }
    esp_http_client_cleanup(client);
    if (err != ESP_OK || response == NULL) {
        free(response);
        return err != ESP_OK ? err : ESP_FAIL;
    }

    char *json = heap_caps_malloc(response_len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (json == NULL) {
        json = malloc(response_len + 1);
    }
    if (json == NULL) {
        free(response);
        return ESP_ERR_NO_MEM;
    }
    memcpy(json, response, response_len);
    json[response_len] = '\0';
    free(response);

    uint8_t *mp3 = NULL;
    size_t mp3_len = 0;
    if (!extract_audio_base64(json, &mp3, &mp3_len) || mp3 == NULL || mp3_len == 0) {
        free(json);
        free(mp3);
        return ESP_FAIL;
    }
    free(json);
    voice_result_free(result);
    result->mp3 = mp3;
    result->mp3_len = mp3_len;
    return ESP_OK;
}

static esp_err_t post_pcm_buffer(astrolabe_audio_pipeline_t *p, const uint8_t *pcm_data, size_t pcm_len, voice_result_t *result)
{
    if (p == NULL || pcm_data == NULL || pcm_len == 0 || result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char *face = json_escape_alloc(p->cfg.face);
    char *slug = json_escape_alloc(p->cfg.faculty_slug);
    char *name = json_escape_alloc(p->cfg.faculty_name);
    char *system = json_escape_alloc(p->cfg.system_instruction);
    char *history = json_escape_alloc(p->cfg.history);
    char *interaction = json_escape_alloc(cfg_interaction_mode(p));
    char *commonplace = json_escape_alloc(cfg_commonplace_mode(p));
    char *response_format = json_escape_alloc(cfg_response_format(p));
    char *respondent = json_escape_alloc(p->cfg.respondent);
    char *mode = json_escape_alloc(p->cfg.mode);
    char *topic = json_escape_alloc(p->cfg.topic);
    char *work_slug = json_escape_alloc(p->cfg.work_slug);
    char *session_id = json_escape_alloc(p->cfg.session_id);
    if (face == NULL || slug == NULL || name == NULL || system == NULL || history == NULL ||
        interaction == NULL || commonplace == NULL || response_format == NULL || respondent == NULL ||
        mode == NULL || topic == NULL || work_slug == NULL || session_id == NULL) {
        free(face);
        free(slug);
        free(name);
        free(system);
        free(history);
        free(interaction);
        free(commonplace);
        free(response_format);
        free(respondent); free(mode); free(topic); free(work_slug); free(session_id);
        return ESP_ERR_NO_MEM;
    }

    const size_t meta_cap = strlen(face) + strlen(slug) + strlen(name) + strlen(history) + strlen(system) +
                            strlen(interaction) + strlen(commonplace) + strlen(response_format) +
                            strlen(respondent) + strlen(mode) + strlen(topic) + strlen(work_slug) +
                            strlen(session_id) + 640;
    char *meta = heap_caps_malloc(meta_cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (meta == NULL) {
        meta = malloc(meta_cap);
    }
    if (meta == NULL) {
        free(face);
        free(slug);
        free(name);
        free(system);
        free(history);
        free(interaction);
        free(commonplace);
        free(response_format);
        free(respondent); free(mode); free(topic); free(work_slug); free(session_id);
        return ESP_ERR_NO_MEM;
    }
    int meta_len = snprintf(meta, meta_cap,
             "{\"languageCode\":\"en-US\",\"sampleRateHertz\":%u,"
             "\"face\":\"%s\",\"facultySlug\":\"%s\",\"facultyName\":\"%s\","
             "\"conversationHistory\":\"%s\",\"systemInstruction\":\"%s\","
             "\"interactionMode\":\"%s\",\"commonplaceMode\":\"%s\","
             "\"responseFormat\":\"%s\",\"respondent\":\"%s\",\"mode\":\"%s\","
             "\"topic\":\"%s\",\"workSlug\":\"%s\",\"sessionId\":\"%s\","
             "\"skipLlm\":%s,\"logToCommonplace\":%s,\"syntheticValidation\":%s}",
             (unsigned)cfg_stt_sample_rate_hz(p), face, slug, name, history, system,
             interaction, commonplace, response_format, respondent, mode, topic, work_slug, session_id,
             cfg_skip_llm(p) ? "true" : "false",
             cfg_log_to_commonplace(p) ? "true" : "false",
             cfg_synthetic_validation(p) ? "true" : "false");
    free(face);
    free(slug);
    free(name);
    free(system);
    free(history);
    free(interaction);
    free(commonplace);
    free(response_format);
    free(respondent); free(mode); free(topic); free(work_slug); free(session_id);
    if (meta_len <= 0 || (size_t)meta_len >= meta_cap) {
        free(meta);
        return ESP_ERR_NO_MEM;
    }

    uint8_t *response = NULL;
    size_t response_len = 0;
    int status = 0;
    const uint32_t t0 = ticks_ms();
    const size_t meta_size = (size_t)meta_len;
    const size_t body_len = 4 + meta_size + pcm_len;
    esp_http_client_config_t http_cfg = {
        .url = p->cfg.endpoint_url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 4096,
        .buffer_size_tx = 2048,
    };
    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    if (client == NULL) {
        free(meta);
        return ESP_FAIL;
    }
    esp_http_client_set_header(client, "Content-Type", VOICE_STREAM_CT);
    esp_http_client_set_header(client, "Accept", strcmp(cfg_response_format(p), "json") == 0 ? "application/json" : "audio/mpeg");
    if (p->cfg.api_key != NULL && p->cfg.api_key[0] != '\0') {
        esp_http_client_set_header(client, "apikey", p->cfg.api_key);
        char auth[512];
        snprintf(auth, sizeof(auth), "Bearer %s", p->cfg.api_key);
        esp_http_client_set_header(client, "Authorization", auth);
    }
    if (p->cfg.request_headers != NULL && p->cfg.request_headers(client, p->cfg.event_user) != ESP_OK) {
        esp_http_client_cleanup(client);
        free(meta);
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = esp_http_client_open(client, body_len);
    uint8_t header[4] = {
        (uint8_t)(meta_size & 0xffu),
        (uint8_t)((meta_size >> 8) & 0xffu),
        (uint8_t)((meta_size >> 16) & 0xffu),
        (uint8_t)((meta_size >> 24) & 0xffu),
    };
    if (err == ESP_OK) {
        err = http_write_all(client, header, sizeof(header));
    }
    if (err == ESP_OK) {
        err = http_write_all(client, meta, meta_size);
    }
    free(meta);

    size_t offset = 0;
    while (err == ESP_OK && offset < pcm_len) {
        const size_t chunk = (pcm_len - offset) > POST_PCM_CHUNK_BYTES ? POST_PCM_CHUNK_BYTES : (pcm_len - offset);
        err = http_write_all(client, pcm_data + offset, chunk);
        offset += chunk;
    }
    if (err == ESP_OK) {
        (void)esp_http_client_fetch_headers(client);
        status = esp_http_client_get_status_code(client);
        if (status < 200 || status >= 300) {
            (void)read_response_body(client, &response, &response_len);
            err = ESP_FAIL;
        } else {
            capture_voice_response_headers(client, result);
            err = read_response_body(client, &response, &response_len);
        }
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    ESP_LOGI(TAG, "voice-pipeline HTTP(buffer) %d body=%uB in %ums", status, (unsigned)response_len,
             (unsigned)(ticks_ms() - t0));
    if (err != ESP_OK) {
        if (response != NULL && response_len > 0) {
            const size_t snippet_len = response_len > 240 ? 240 : response_len;
            char snippet[241];
            memcpy(snippet, response, snippet_len);
            snippet[snippet_len] = '\0';
            ESP_LOGW(TAG, "voice-pipeline buffer error body: %s", snippet);
        }
        free(response);
        return err;
    }
    if (looks_like_mp3(response, response_len)) {
        result->mp3 = response;
        result->mp3_len = response_len;
        return ESP_OK;
    }

    char *body = heap_caps_malloc(response_len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (body == NULL) {
        body = malloc(response_len + 1);
    }
    if (body == NULL) {
        free(response);
        return ESP_ERR_NO_MEM;
    }
    memcpy(body, response, response_len);
    body[response_len] = '\0';
    free(response);

    json_find_string(body, "transcript", result->transcript, sizeof(result->transcript));
    json_find_string(body, "reply", result->reply, sizeof(result->reply));
    json_find_string(body, "facultySlug", result->faculty_slug, sizeof(result->faculty_slug));
    json_find_string(body, "facultyName", result->faculty_name, sizeof(result->faculty_name));
    json_find_string(body, "sessionId", result->session_id, sizeof(result->session_id));
    json_find_string(body, "expression", result->expression, sizeof(result->expression));
    (void)extract_audio_base64(body, &result->mp3, &result->mp3_len);
    free(body);
    return ESP_OK;
}

static esp_err_t post_pcm_file(astrolabe_audio_pipeline_t *p, const char *path, size_t pcm_len, voice_result_t *result)
{
    char *face = json_escape_alloc(p->cfg.face);
    char *slug = json_escape_alloc(p->cfg.faculty_slug);
    char *name = json_escape_alloc(p->cfg.faculty_name);
    char *system = json_escape_alloc(p->cfg.system_instruction);
    char *history = json_escape_alloc(p->cfg.history);
    char *interaction = json_escape_alloc(cfg_interaction_mode(p));
    char *commonplace = json_escape_alloc(cfg_commonplace_mode(p));
    char *response_format = json_escape_alloc(cfg_response_format(p));
    char *respondent = json_escape_alloc(p->cfg.respondent);
    char *mode = json_escape_alloc(p->cfg.mode);
    char *topic = json_escape_alloc(p->cfg.topic);
    char *work_slug = json_escape_alloc(p->cfg.work_slug);
    char *session_id = json_escape_alloc(p->cfg.session_id);
    if (face == NULL || slug == NULL || name == NULL || system == NULL || history == NULL ||
        interaction == NULL || commonplace == NULL || response_format == NULL || respondent == NULL ||
        mode == NULL || topic == NULL || work_slug == NULL || session_id == NULL) {
        free(face);
        free(slug);
        free(name);
        free(system);
        free(history);
        free(interaction);
        free(commonplace);
        free(response_format);
        free(respondent); free(mode); free(topic); free(work_slug); free(session_id);
        return ESP_ERR_NO_MEM;
    }

    const size_t meta_cap = strlen(face) + strlen(slug) + strlen(name) + strlen(history) + strlen(system) +
                            strlen(interaction) + strlen(commonplace) + strlen(response_format) +
                            strlen(respondent) + strlen(mode) + strlen(topic) + strlen(work_slug) +
                            strlen(session_id) + 640;
    char *meta = heap_caps_malloc(meta_cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (meta == NULL) {
        meta = malloc(meta_cap);
    }
    if (meta == NULL) {
        free(face);
        free(slug);
        free(name);
        free(system);
        free(history);
        free(interaction);
        free(commonplace);
        free(response_format);
        free(respondent); free(mode); free(topic); free(work_slug); free(session_id);
        return ESP_ERR_NO_MEM;
    }
    int meta_len = snprintf(meta, meta_cap,
             "{\"languageCode\":\"en-US\",\"sampleRateHertz\":%u,"
             "\"face\":\"%s\",\"facultySlug\":\"%s\",\"facultyName\":\"%s\","
             "\"conversationHistory\":\"%s\",\"systemInstruction\":\"%s\","
             "\"interactionMode\":\"%s\",\"commonplaceMode\":\"%s\","
             "\"responseFormat\":\"%s\",\"respondent\":\"%s\",\"mode\":\"%s\","
             "\"topic\":\"%s\",\"workSlug\":\"%s\",\"sessionId\":\"%s\","
             "\"skipLlm\":%s,\"logToCommonplace\":%s,\"syntheticValidation\":%s}",
             (unsigned)cfg_stt_sample_rate_hz(p), face, slug, name, history, system,
             interaction, commonplace, response_format, respondent, mode, topic, work_slug, session_id,
             cfg_skip_llm(p) ? "true" : "false",
             cfg_log_to_commonplace(p) ? "true" : "false",
             cfg_synthetic_validation(p) ? "true" : "false");
    free(face);
    free(slug);
    free(name);
    free(system);
    free(history);
    free(interaction);
    free(commonplace);
    free(response_format);
    free(respondent); free(mode); free(topic); free(work_slug); free(session_id);
    if (meta_len <= 0 || (size_t)meta_len >= meta_cap) {
        free(meta);
        return ESP_ERR_NO_MEM;
    }
    const size_t meta_size = (size_t)meta_len;
    if (meta_size > 16384) {
        free(meta);
        return ESP_ERR_NO_MEM;
    }

    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        free(meta);
        return ESP_FAIL;
    }

    uint8_t *response = NULL;
    size_t response_len = 0;
    int status = 0;
    const uint32_t t0 = ticks_ms();
    const size_t body_len = 4 + meta_size + pcm_len;
    esp_http_client_config_t http_cfg = {
        .url = p->cfg.endpoint_url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 4096,
        .buffer_size_tx = 2048,
    };
    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    if (client == NULL) {
        fclose(file);
        free(meta);
        return ESP_FAIL;
    }
    esp_http_client_set_header(client, "Content-Type", VOICE_STREAM_CT);
    esp_http_client_set_header(client, "Accept", strcmp(cfg_response_format(p), "json") == 0 ? "application/json" : "audio/mpeg");
    if (p->cfg.api_key != NULL && p->cfg.api_key[0] != '\0') {
        esp_http_client_set_header(client, "apikey", p->cfg.api_key);
        char auth[512];
        snprintf(auth, sizeof(auth), "Bearer %s", p->cfg.api_key);
        esp_http_client_set_header(client, "Authorization", auth);
    }
    if (p->cfg.request_headers != NULL && p->cfg.request_headers(client, p->cfg.event_user) != ESP_OK) {
        esp_http_client_cleanup(client);
        fclose(file);
        free(meta);
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = esp_http_client_open(client, body_len);
    uint8_t header[4] = {
        (uint8_t)(meta_size & 0xffu),
        (uint8_t)((meta_size >> 8) & 0xffu),
        (uint8_t)((meta_size >> 16) & 0xffu),
        (uint8_t)((meta_size >> 24) & 0xffu),
    };
    if (err == ESP_OK) {
        err = http_write_all(client, header, sizeof(header));
    }
    if (err == ESP_OK) {
        err = http_write_all(client, meta, meta_size);
    }
    uint8_t raw[POST_PCM_CHUNK_BYTES];
    size_t remaining = pcm_len;
    while (err == ESP_OK && remaining > 0) {
        const size_t want = remaining > sizeof(raw) ? sizeof(raw) : remaining;
        const size_t got = fread(raw, 1, want, file);
        if (got == 0) {
            err = ESP_FAIL;
            break;
        }
        err = http_write_all(client, raw, got);
        remaining -= got;
    }
    fclose(file);
    free(meta);
    if (err == ESP_OK) {
        (void)esp_http_client_fetch_headers(client);
        status = esp_http_client_get_status_code(client);
        if (status < 200 || status >= 300) {
            (void)read_response_body(client, &response, &response_len);
            err = ESP_FAIL;
        } else {
            capture_voice_response_headers(client, result);
            err = read_response_body(client, &response, &response_len);
        }
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    ESP_LOGI(TAG, "voice-pipeline HTTP %d body=%uB in %ums", status, (unsigned)response_len,
             (unsigned)(ticks_ms() - t0));
    if (err != ESP_OK) {
        if (response != NULL && response_len > 0) {
            const size_t snippet_len = response_len > 240 ? 240 : response_len;
            char snippet[241];
            memcpy(snippet, response, snippet_len);
            snippet[snippet_len] = '\0';
            ESP_LOGW(TAG, "voice-pipeline error body: %s", snippet);
        }
        free(response);
        return err;
    }
    if (looks_like_mp3(response, response_len)) {
        result->mp3 = response;
        result->mp3_len = response_len;
        return ESP_OK;
    }

    char *body = heap_caps_malloc(response_len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (body == NULL) {
        body = malloc(response_len + 1);
    }
    if (body == NULL) {
        free(response);
        return ESP_ERR_NO_MEM;
    }
    memcpy(body, response, response_len);
    body[response_len] = '\0';
    free(response);

    json_find_string(body, "transcript", result->transcript, sizeof(result->transcript));
    json_find_string(body, "reply", result->reply, sizeof(result->reply));
    json_find_string(body, "facultySlug", result->faculty_slug, sizeof(result->faculty_slug));
    json_find_string(body, "facultyName", result->faculty_name, sizeof(result->faculty_name));
    json_find_string(body, "sessionId", result->session_id, sizeof(result->session_id));
    json_find_string(body, "expression", result->expression, sizeof(result->expression));
    (void)extract_audio_base64(body, &result->mp3, &result->mp3_len);
    free(body);
    return ESP_OK;
}

static void put_u32_le(uint8_t *out, uint32_t value)
{
    out[0] = (uint8_t)(value & 0xffu);
    out[1] = (uint8_t)((value >> 8) & 0xffu);
    out[2] = (uint8_t)((value >> 16) & 0xffu);
    out[3] = (uint8_t)((value >> 24) & 0xffu);
}

static esp_err_t websocket_send_text_all(esp_websocket_client_handle_t client, const char *text)
{
    const int len = (int)strlen(text);
    const int sent = esp_websocket_client_send_text(client, text, len, pdMS_TO_TICKS(STREAM_SEND_TIMEOUT_MS));
    return sent == len ? ESP_OK : ESP_FAIL;
}

static esp_err_t websocket_send_binary_all(esp_websocket_client_handle_t client, const uint8_t *data, size_t len)
{
    const int sent = esp_websocket_client_send_bin(client, (const char *)data, (int)len,
                                                  pdMS_TO_TICKS(STREAM_SEND_TIMEOUT_MS));
    return sent == (int)len ? ESP_OK : ESP_FAIL;
}

static void stream_response_handle_json(stream_response_ctx_t *ctx, char *json)
{
    if (ctx == NULL || ctx->result == NULL || json == NULL) {
        return;
    }
    char type[96] = "";
    (void)json_find_string(json, "type", type, sizeof(type));
    ESP_LOGI(TAG, "voice-stream rx type=%s bytes=%u",
             type[0] != '\0' ? type : "?",
             (unsigned)strlen(json));
    if (strstr(json, "\"type\":\"conversation.item.input_audio_transcription.delta\"") != NULL) {
        char interim[320] = "";
        if (json_find_string(json, "delta", interim, sizeof(interim)) != NULL) {
            strlcpy(ctx->result->transcript, interim, sizeof(ctx->result->transcript));
            ESP_LOGI(TAG, "voice-stream interim: %.120s", interim);
        }
    } else if (strstr(json, "\"type\":\"conversation.item.input_audio_transcription.completed\"") != NULL) {
        json_find_string(json, "transcript", ctx->result->transcript, sizeof(ctx->result->transcript));
    } else if (strstr(json, "\"type\":\"response.text.delta\"") != NULL) {
        char delta[256];
        if (json_find_string(json, "delta", delta, sizeof(delta)) != NULL) {
            append_text(ctx->result->reply, sizeof(ctx->result->reply), delta);
        }
    } else if (strstr(json, "\"type\":\"response.audio.delta\"") != NULL) {
        char *audio_b64 = NULL;
        size_t audio_b64_len = 0;
        if (json_find_string_alloc(json, "audio", &audio_b64, &audio_b64_len)) {
            uint8_t *mp3 = NULL;
            size_t mp3_len = 0;
            if (decode_base64_alloc(audio_b64, audio_b64_len, &mp3, &mp3_len)) {
                const bool segmented = strstr(json, "\"segmentIndex\":") != NULL;
                uint8_t **target = segmented ? &ctx->pending_segment_mp3 : &ctx->result->mp3;
                size_t *target_len = segmented ? &ctx->pending_segment_mp3_len : &ctx->result->mp3_len;
                if (!append_bytes(target, target_len, mp3, mp3_len)) {
                    ctx->err = ESP_ERR_NO_MEM;
                    ctx->event_count++;
                } else if (segmented && strstr(json, "\"segmentEnd\":true") != NULL) {
                    stream_audio_segment_t segment = {
                        .mp3 = ctx->pending_segment_mp3,
                        .mp3_len = ctx->pending_segment_mp3_len,
                    };
                    ctx->pending_segment_mp3 = NULL;
                    ctx->pending_segment_mp3_len = 0;
                    if (ctx->audio_segments == NULL || xQueueSend(ctx->audio_segments, &segment, 0) != pdTRUE) {
                        free(segment.mp3);
                        ctx->err = ESP_ERR_NO_MEM;
                        ctx->event_count++;
                    } else {
                        ESP_LOGI(TAG, "voice-stream queued playable audio segment bytes=%u",
                                 (unsigned)segment.mp3_len);
                    }
                }
                free(mp3);
            }
            free(audio_b64);
        }
    } else if (strstr(json, "\"type\":\"response.done\"") != NULL) {
        json_find_string(json, "facultySlug", ctx->result->faculty_slug, sizeof(ctx->result->faculty_slug));
        json_find_string(json, "facultyName", ctx->result->faculty_name, sizeof(ctx->result->faculty_name));
        json_find_string(json, "sessionId", ctx->result->session_id, sizeof(ctx->result->session_id));
        json_find_string(json, "expression", ctx->result->expression, sizeof(ctx->result->expression));
        ctx->response_done = true;
        ctx->event_count++;
    } else if (strstr(json, "\"type\":\"error\"") != NULL) {
        char code[96] = "";
        char message[192] = "";
        (void)json_find_string(json, "code", code, sizeof(code));
        (void)json_find_string(json, "message", message, sizeof(message));
        ESP_LOGW(TAG, "voice-stream error code=%s message=%s",
                 code[0] != '\0' ? code : "?",
                 message[0] != '\0' ? message : "?");
        ctx->err = ESP_FAIL;
        ctx->event_count++;
    }
}

static void stream_response_event(void *handler_arg, esp_event_base_t base, int32_t event_id, void *event_data)
{
    (void)base;
    stream_response_ctx_t *ctx = (stream_response_ctx_t *)handler_arg;
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
    if (ctx == NULL) {
        return;
    }
    if (event_id == WEBSOCKET_EVENT_CONNECTED) {
        ESP_LOGI(TAG, "voice-stream websocket connected");
        return;
    }
    if (event_id == WEBSOCKET_EVENT_DISCONNECTED) {
        ESP_LOGW(TAG, "voice-stream websocket disconnected");
        ctx->transport_failed = true;
        return;
    }
    if (event_id == WEBSOCKET_EVENT_CLOSED) {
        ESP_LOGW(TAG, "voice-stream websocket closed");
        ctx->transport_failed = true;
        return;
    }
    if (event_id == WEBSOCKET_EVENT_ERROR) {
        if (data != NULL) {
            ctx->websocket_status_code = data->error_handle.esp_ws_handshake_status_code;
        }
        ESP_LOGW(TAG, "voice-stream websocket error");
        ctx->err = ESP_FAIL;
        ctx->transport_failed = true;
        ctx->event_count++;
        return;
    }
    if (data == NULL) {
        return;
    }
    if (event_id != WEBSOCKET_EVENT_DATA || data->data_ptr == NULL || data->data_len <= 0) {
        return;
    }

    const size_t payload_len = data->payload_len > 0 ? (size_t)data->payload_len : (size_t)data->data_len;
    const size_t payload_off = data->payload_offset >= 0 ? (size_t)data->payload_offset : 0;
    if (payload_off == 0 || ctx->message == NULL || ctx->message_cap < payload_len + 1) {
        free(ctx->message);
        ctx->message = heap_caps_malloc(payload_len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (ctx->message == NULL) {
            ctx->message = heap_caps_malloc(payload_len + 1, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        }
        if (ctx->message == NULL) {
            ctx->message = malloc(payload_len + 1);
        }
        ctx->message_cap = ctx->message != NULL ? payload_len + 1 : 0;
        ctx->message_len = 0;
        ESP_LOGI(TAG,
                 "voice-stream message alloc ptr=%p ext=%d cap=%u",
                 (void *)ctx->message,
                 ctx->message != NULL && esp_ptr_external_ram(ctx->message),
                 (unsigned)ctx->message_cap);
    }
    if (ctx->message == NULL || payload_off + (size_t)data->data_len > ctx->message_cap - 1) {
        ctx->err = ESP_ERR_NO_MEM;
        ctx->event_count++;
        return;
    }
    memcpy(ctx->message + payload_off, data->data_ptr, (size_t)data->data_len);
    const size_t end = payload_off + (size_t)data->data_len;
    if (end > ctx->message_len) {
        ctx->message_len = end;
    }
    if (ctx->message_len < payload_len || !data->fin) {
        return;
    }
    ctx->message[payload_len] = '\0';
    stream_response_handle_json(ctx, ctx->message);
    ctx->message_len = 0;
}

static void rolling_stream_reset_result(voice_result_t *result)
{
    if (result == NULL) {
        return;
    }
    voice_result_free(result);
    memset(result->transcript, 0, sizeof(result->transcript));
    memset(result->reply, 0, sizeof(result->reply));
    memset(result->faculty_slug, 0, sizeof(result->faculty_slug));
    memset(result->faculty_name, 0, sizeof(result->faculty_name));
}

static void rolling_stream_reset_response(stream_response_ctx_t *ctx)
{
    if (ctx == NULL) {
        return;
    }
    ctx->event_count = 0;
    ctx->websocket_status_code = 0;
    ctx->err = ESP_OK;
    ctx->response_done = false;
    ctx->transport_failed = false;
    ctx->message_len = 0;
    free(ctx->pending_segment_mp3);
    ctx->pending_segment_mp3 = NULL;
    ctx->pending_segment_mp3_len = 0;
    ctx->segmented_audio_played = false;
    if (ctx->audio_segments != NULL) {
        stream_audio_segment_t segment = {};
        while (xQueueReceive(ctx->audio_segments, &segment, 0) == pdTRUE) {
            free(segment.mp3);
        }
    }
}

static bool rolling_stream_wait_response(astrolabe_audio_pipeline_t *p, uint32_t seen_count, uint32_t timeout_ms)
{
    const uint32_t start_ms = ticks_ms();
    stream_response_ctx_t *ctx = p != NULL ? p->rolling_response : NULL;
    bool speaking = false;
    while (ctx != NULL && ticks_ms() - start_ms < timeout_ms) {
        stream_audio_segment_t segment = {};
        if (ctx->audio_segments != NULL && xQueueReceive(ctx->audio_segments, &segment, 0) == pdTRUE) {
            if (!speaking) {
                stop_listen_task_if_running(p, 1000);
                emit(p, ASTROLABE_AUDIO_PIPELINE_EVENT_SPEAKING, p->cfg.faculty_name);
                speaking = true;
            }
            ESP_LOGI(TAG, "voice-stream playing audio segment bytes=%u after %ums",
                     (unsigned)segment.mp3_len, (unsigned)(ticks_ms() - start_ms));
            esp_err_t play_err = p->cfg.play_mp3 != NULL
                                     ? p->cfg.play_mp3(segment.mp3, segment.mp3_len, p->cfg.event_user)
                                     : play_mp3(p, segment.mp3, segment.mp3_len);
            free(segment.mp3);
            if (play_err != ESP_OK) {
                ctx->err = play_err;
                ctx->event_count++;
                return false;
            }
            ctx->segmented_audio_played = true;
            continue;
        }
        if (ctx->event_count != seen_count) {
            if (!ctx->response_done || ctx->pending_segment_mp3_len == 0) {
                return true;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return false;
}

static void rolling_stream_session_close(astrolabe_audio_pipeline_t *p)
{
    if (p == NULL) {
        return;
    }
    esp_websocket_client_handle_t client = p->rolling_client;
    p->rolling_client = NULL;

    if (client != NULL && p->rolling_response != NULL) {
        (void)esp_websocket_unregister_events(client, WEBSOCKET_EVENT_ANY, stream_response_event);
    }
    if (client != NULL) {
        (void)esp_websocket_client_close(client, pdMS_TO_TICKS(1000));
        (void)esp_websocket_client_destroy(client);
    }
    rolling_stream_reset_response(p->rolling_response);
    rolling_stream_reset_result(p->rolling_result);
}

static void rolling_stream_state_free(astrolabe_audio_pipeline_t *p)
{
    if (p == NULL) {
        return;
    }
    if (p->rolling_response != NULL) {
        if (p->rolling_response->audio_segments != NULL) {
            vQueueDelete(p->rolling_response->audio_segments);
            p->rolling_response->audio_segments = NULL;
        }
        free(p->rolling_response->pending_segment_mp3);
        p->rolling_response->pending_segment_mp3 = NULL;
        free(p->rolling_response->message);
        p->rolling_response->message = NULL;
        free(p->rolling_response);
        p->rolling_response = NULL;
    }
    if (p->rolling_result != NULL) {
        rolling_stream_reset_result(p->rolling_result);
        free(p->rolling_result);
        p->rolling_result = NULL;
    }
}

static esp_err_t rolling_stream_session_open(astrolabe_audio_pipeline_t *p)
{
    if (p == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (p->rolling_client != NULL && p->rolling_response != NULL && p->rolling_result != NULL &&
        esp_websocket_client_is_connected(p->rolling_client)) {
        return ESP_OK;
    }
    if (heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) < STREAM_WEBSOCKET_TASK_STACK) {
        ESP_LOGW(TAG,
                 "voice-stream websocket deferred: internal largest=%u need=%u; using flash HTTP fallback",
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                 STREAM_WEBSOCKET_TASK_STACK);
        return ESP_ERR_NO_MEM;
    }

    char *face = json_escape_alloc(p->cfg.face);
    char *slug = json_escape_alloc(p->cfg.faculty_slug);
    char *name = json_escape_alloc(p->cfg.faculty_name);
    char *system = json_escape_alloc(p->cfg.system_instruction);
    char *history = json_escape_alloc(p->cfg.history);
    char *interaction = json_escape_alloc(cfg_interaction_mode(p));
    char *commonplace = json_escape_alloc(cfg_commonplace_mode(p));
    char *response_format = json_escape_alloc(cfg_response_format(p));
    char *respondent = json_escape_alloc(p->cfg.respondent);
    char *mode = json_escape_alloc(p->cfg.mode);
    char *topic = json_escape_alloc(p->cfg.topic);
    char *work_slug = json_escape_alloc(p->cfg.work_slug);
    char *session_id = json_escape_alloc(p->cfg.session_id);
    if (face == NULL || slug == NULL || name == NULL || system == NULL || history == NULL ||
        interaction == NULL || commonplace == NULL || response_format == NULL || respondent == NULL ||
        mode == NULL || topic == NULL || work_slug == NULL || session_id == NULL) {
        free(face);
        free(slug);
        free(name);
        free(system);
        free(history);
        free(interaction);
        free(commonplace);
        free(response_format);
        free(respondent);
        free(mode);
        free(topic);
        free(work_slug);
        free(session_id);
        return ESP_ERR_NO_MEM;
    }

    const size_t session_cap = strlen(face) + strlen(slug) + strlen(name) + strlen(system) + strlen(history) +
                               strlen(interaction) + strlen(commonplace) + strlen(response_format) +
                               strlen(respondent) + strlen(mode) + strlen(topic) + strlen(work_slug) +
                               strlen(session_id) + 768;
    char auth_header[960];
    auth_header[0] = '\0';
    if (p->cfg.api_key != NULL && p->cfg.api_key[0] != '\0') {
        snprintf(auth_header, sizeof(auth_header), "apikey: %s\r\nAuthorization: Bearer %s\r\n",
                 p->cfg.api_key, p->cfg.api_key);
    }
    if (p->cfg.request_header_text != NULL) {
        char device_headers[384] = "";
        const esp_err_t header_err = p->cfg.request_header_text(
            device_headers,
            sizeof(device_headers),
            p->cfg.event_user);
        if (header_err != ESP_OK || strlcat(auth_header, device_headers, sizeof(auth_header)) >= sizeof(auth_header)) {
            free(face);
            free(slug);
            free(name);
            free(system);
            free(history);
            free(interaction);
            free(commonplace);
            free(response_format);
            free(respondent);
            free(mode);
            free(topic);
            free(work_slug);
            free(session_id);
            return header_err != ESP_OK ? header_err : ESP_ERR_INVALID_SIZE;
        }
    }

    esp_err_t err = ESP_FAIL;
    for (uint32_t attempt = 0; attempt < STREAM_OPEN_RETRIES; ++attempt) {
        rolling_stream_session_close(p);

        esp_websocket_client_config_t ws_cfg = {
            .uri = p->cfg.stream_url,
            .headers = auth_header[0] != '\0' ? auth_header : NULL,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .disable_auto_reconnect = true,
            .task_core_id_set = true,
            .task_core_id = 0,
            .task_name = "voice_ws",
            .task_stack = STREAM_WEBSOCKET_TASK_STACK,
            .buffer_size = STREAM_WEBSOCKET_BUFFER_SIZE,
            .network_timeout_ms = STREAM_SEND_TIMEOUT_MS,
        };
        p->rolling_client = esp_websocket_client_init(&ws_cfg);
        if (p->rolling_client == NULL) {
            err = ESP_FAIL;
            continue;
        }

        if (p->rolling_result == NULL) {
            p->rolling_result = pipeline_calloc_prefer_psram(1, sizeof(*p->rolling_result));
        }
        if (p->rolling_response == NULL) {
            p->rolling_response = pipeline_calloc_prefer_internal(1, sizeof(*p->rolling_response));
        }
        if (p->rolling_result == NULL || p->rolling_response == NULL) {
            err = ESP_ERR_NO_MEM;
            break;
        }
        p->rolling_response->result = p->rolling_result;
        if (p->rolling_response->audio_segments == NULL) {
            p->rolling_response->audio_segments = xQueueCreate(
                STREAM_AUDIO_SEGMENT_QUEUE_LEN, sizeof(stream_audio_segment_t));
        }
        if (p->rolling_response->audio_segments == NULL) {
            err = ESP_ERR_NO_MEM;
            break;
        }
        rolling_stream_reset_response(p->rolling_response);
        rolling_stream_reset_result(p->rolling_result);
        ESP_LOGI(TAG,
                 "voice-stream ctx result=%p ext=%d response=%p ext=%d",
                 (void *)p->rolling_result,
                 esp_ptr_external_ram(p->rolling_result),
                 (void *)p->rolling_response,
                 esp_ptr_external_ram(p->rolling_response));
        (void)esp_websocket_register_events(p->rolling_client, WEBSOCKET_EVENT_ANY, stream_response_event, p->rolling_response);

        const uint32_t t0 = ticks_ms();
        err = esp_websocket_client_start(p->rolling_client);
        while (err == ESP_OK && !esp_websocket_client_is_connected(p->rolling_client) &&
               !p->rolling_response->transport_failed &&
               ticks_ms() - t0 < STREAM_CONNECT_TIMEOUT_MS) {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        if (err == ESP_OK && p->rolling_response->transport_failed) {
            err = p->rolling_response->err != ESP_OK ? p->rolling_response->err : ESP_FAIL;
        } else if (err == ESP_OK && !esp_websocket_client_is_connected(p->rolling_client)) {
            err = ESP_ERR_TIMEOUT;
        }

        char *session = heap_caps_malloc(session_cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (session == NULL) {
            session = malloc(session_cap);
        }
        if (err == ESP_OK && session == NULL) {
            err = ESP_ERR_NO_MEM;
        }
        if (err == ESP_OK) {
            ESP_LOGI(TAG,
                     "voice-stream metadata synthetic_validation=%s",
                     cfg_synthetic_validation(p) ? "true" : "false");
            int session_len = snprintf(session, session_cap,
                                       "{\"type\":\"session.update\",\"session\":{\"sampleRateHertz\":%u,"
                                       "\"sample_width_bits\":16,\"channels\":1,\"encoding\":\"pcm16\","
                                       "\"face\":\"%s\",\"facultySlug\":\"%s\",\"facultyName\":\"%s\","
                                       "\"conversationHistory\":\"%s\",\"systemInstruction\":\"%s\","
                                       "\"interactionMode\":\"%s\",\"commonplaceMode\":\"%s\","
                                       "\"responseFormat\":\"%s\",\"respondent\":\"%s\","
                                       "\"mode\":\"%s\",\"topic\":\"%s\",\"workSlug\":\"%s\","
                                       "\"sessionId\":\"%s\",\"skipLlm\":%s,\"logToCommonplace\":%s,"
                                       "\"syntheticValidation\":%s}}",
                                       (unsigned)cfg_stt_sample_rate_hz(p), face, slug, name, history, system,
                                       interaction, commonplace, response_format, respondent, mode, topic, work_slug,
                                       session_id,
                                       cfg_skip_llm(p) ? "true" : "false",
                                       cfg_log_to_commonplace(p) ? "true" : "false",
                                       cfg_synthetic_validation(p) ? "true" : "false");
            if (session_len <= 0 || (size_t)session_len >= session_cap) {
                err = ESP_ERR_NO_MEM;
            } else {
                err = websocket_send_text_all(p->rolling_client, session);
            }
        }
        free(session);
        if (err == ESP_OK) {
            break;
        }
        ESP_LOGW(TAG, "voice-stream session open attempt %u/%u failed: %s",
                 (unsigned)(attempt + 1),
                 (unsigned)STREAM_OPEN_RETRIES,
                 esp_err_to_name(err));
        if (p->rolling_response != NULL && p->rolling_response->websocket_status_code >= 400) {
            ESP_LOGW(TAG, "voice-stream backend refused websocket handshake status=%d",
                     p->rolling_response->websocket_status_code);
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    free(face);
    free(slug);
    free(name);
    free(system);
    free(history);
    free(interaction);
    free(commonplace);
    free(response_format);
    free(respondent);
    free(mode);
    free(topic);
    free(work_slug);
    free(session_id);

    if (err != ESP_OK) {
        rolling_stream_session_close(p);
    }
    return err;
}

static void rolling_stream_copy_snapshot(const voice_result_t *src, voice_result_t *dst, bool include_audio)
{
    if (src == NULL || dst == NULL) {
        return;
    }
    strlcpy(dst->transcript, src->transcript, sizeof(dst->transcript));
    strlcpy(dst->reply, src->reply, sizeof(dst->reply));
    strlcpy(dst->faculty_slug, src->faculty_slug, sizeof(dst->faculty_slug));
    strlcpy(dst->faculty_name, src->faculty_name, sizeof(dst->faculty_name));
    strlcpy(dst->session_id, src->session_id, sizeof(dst->session_id));
    strlcpy(dst->expression, src->expression, sizeof(dst->expression));
    if (include_audio && src->mp3 != NULL && src->mp3_len > 0) {
        dst->mp3 = src->mp3;
        dst->mp3_len = src->mp3_len;
    }
}

static esp_err_t stream_pcm_file(astrolabe_audio_pipeline_t *p, const utterance_t *utt, voice_result_t *result)
{
    if (p->cfg.stream_url == NULL || p->cfg.stream_url[0] == '\0') {
        return ESP_ERR_NOT_SUPPORTED;
    }
    const uint32_t t0 = ticks_ms();
    esp_err_t err = rolling_stream_session_open(p);
    if (err != ESP_OK && !utt->final_segment && p->listen_task != NULL) {
        ESP_LOGW(TAG, "voice-stream reopen for segment #%u failed with listen active; retrying with listen paused",
                 (unsigned)utt->sequence);
        stop_listen_task_if_running(p, 1000);
        err = rolling_stream_session_open(p);
        esp_err_t listen_err = start_listen_task_if_needed(p);
        if (listen_err == ESP_OK) {
            emit(p, ASTROLABE_AUDIO_PIPELINE_EVENT_LISTENING, NULL);
        } else {
            ESP_LOGW(TAG, "listen restart after rolling reopen failed: %s", esp_err_to_name(listen_err));
            if (err == ESP_OK) {
                rolling_stream_session_close(p);
                err = listen_err;
            }
        }
    }
    if (err != ESP_OK) {
        if (utt->final_segment && result != NULL &&
            ((p->turn_pcm != NULL && p->turn_pcm_len > 0 && !p->turn_pcm_truncated) ||
             (utt->pcm_data != NULL && utt->byte_count > 0))) {
            rolling_stream_session_close(p);
            voice_result_free(result);
            memset(result, 0, sizeof(*result));
            if (p->turn_pcm != NULL && p->turn_pcm_len > 0 && !p->turn_pcm_truncated) {
                ESP_LOGW(TAG,
                         "voice-stream session open failed; retrying final turn via HTTP buffer (%uB)",
                         (unsigned)p->turn_pcm_len);
                return post_pcm_buffer(p, p->turn_pcm, p->turn_pcm_len, result);
            }
            ESP_LOGW(TAG,
                     "voice-stream session open failed; retrying final segment mirror via HTTP buffer (%uB)",
                     (unsigned)utt->byte_count);
            return post_pcm_buffer(p, utt->pcm_data, utt->byte_count, result);
        }
        return err;
    }

    uint8_t *packet = NULL;
    uint8_t *pcm_data = NULL;
    size_t sent_bytes = 0;
    if (utt->byte_count > 0) {
        if (utt->pcm_data != NULL) {
            pcm_data = utt->pcm_data;
        } else if (p->cfg.duplex) {
            ESP_LOGE(TAG,
                     "voice-stream segment #%u missing PSRAM mirror; refusing SPIFFS read from duplex voice task",
                     (unsigned)utt->sequence);
            rolling_stream_session_close(p);
            return ESP_ERR_NO_MEM;
        } else {
            FILE *file = fopen(utt->path, "rb");
            if (file == NULL) {
                rolling_stream_session_close(p);
                return ESP_FAIL;
            }
            pcm_data = heap_caps_malloc(utt->byte_count, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (pcm_data == NULL) {
                pcm_data = heap_caps_malloc(utt->byte_count, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            }
            if (pcm_data == NULL) {
                pcm_data = malloc(utt->byte_count);
            }
            if (pcm_data == NULL) {
                fclose(file);
                rolling_stream_session_close(p);
                return ESP_ERR_NO_MEM;
            }
            const size_t loaded = fread(pcm_data, 1, utt->byte_count, file);
            fclose(file);
            if (loaded != utt->byte_count) {
                free(pcm_data);
                rolling_stream_session_close(p);
                return ESP_FAIL;
            }
        }
        // Keep the rolling websocket send packet in internal RAM when possible.
        // This path runs close to flash/cache-disabled work on ESP32-S3, and a
        // PSRAM-backed packet has proven fragile during final-segment commits.
        packet = heap_caps_malloc(STREAM_PCM_CHUNK_BYTES + 9, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (packet == NULL) {
            packet = heap_caps_malloc(STREAM_PCM_CHUNK_BYTES + 9, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        }
        if (packet == NULL) {
            packet = malloc(STREAM_PCM_CHUNK_BYTES + 9);
        }
        if (packet == NULL) {
            if (pcm_data != utt->pcm_data) {
                free(pcm_data);
            }
            rolling_stream_session_close(p);
            return ESP_ERR_NO_MEM;
        }
        while (err == ESP_OK && sent_bytes < utt->byte_count) {
            const size_t want = (utt->byte_count - sent_bytes) > STREAM_PCM_CHUNK_BYTES
                                    ? STREAM_PCM_CHUNK_BYTES
                                    : (utt->byte_count - sent_bytes);
            memcpy(packet + 9, pcm_data + sent_bytes, want);
            const uint32_t capture_ms =
                (uint32_t)(((sent_bytes / sizeof(int16_t)) * 1000u) / cfg_stt_sample_rate_hz(p));
            packet[0] = 0xa1u;
            put_u32_le(packet + 1, utt->sequence);
            put_u32_le(packet + 5, capture_ms);
            err = websocket_send_binary_all(p->rolling_client, packet, want + 9);
            sent_bytes += want;
            if (err == ESP_OK && STREAM_FRAME_PACE_MS > 0) {
                vTaskDelay(pdMS_TO_TICKS(STREAM_FRAME_PACE_MS));
            }
        }
    }
    free(packet);
    if (pcm_data != utt->pcm_data) {
        free(pcm_data);
    }
    if (err == ESP_OK && utt->final_segment && p->rolling_response != NULL) {
        rolling_stream_reset_response(p->rolling_response);
    }
    const uint32_t response_seen_count = p->rolling_response != NULL ? p->rolling_response->event_count : 0;
    if (err == ESP_OK && utt->final_segment) {
        char commit[160];
        snprintf(commit, sizeof(commit),
                 "{\"type\":\"input_audio_buffer.commit\",\"turnId\":\"turn-%u\",\"final\":true,\"bytes\":%u}",
                 (unsigned)utt->sequence, (unsigned)utt->byte_count);
        err = websocket_send_text_all(p->rolling_client, commit);
    }
    if (err == ESP_OK && utt->final_segment && p->rolling_response != NULL) {
        if (!rolling_stream_wait_response(p, response_seen_count, STREAM_RESPONSE_TIMEOUT_MS)) {
            err = p->rolling_response->err != ESP_OK
                      ? p->rolling_response->err
                      : ESP_ERR_TIMEOUT;
        } else if (p->rolling_response->err != ESP_OK) {
            err = p->rolling_response->err;
        } else if (!p->rolling_response->response_done) {
            err = ESP_FAIL;
        } else if (p->rolling_result != NULL && p->rolling_result->transcript[0] == '\0' &&
                   p->rolling_result->reply[0] == '\0' && p->rolling_result->mp3_len == 0) {
            err = ESP_FAIL;
        }
    }

    if (result != NULL && p->rolling_result != NULL && utt->final_segment &&
        p->rolling_result->reply[0] != '\0' &&
        (result->reply[0] == '\0' || result->transcript[0] == '\0')) {
        rolling_stream_copy_snapshot(p->rolling_result, result, false);
    }

    if (err == ESP_OK && result != NULL && p->rolling_result != NULL) {
        rolling_stream_copy_snapshot(p->rolling_result, result, utt->final_segment);
        if (utt->final_segment) {
            p->rolling_result->mp3 = NULL;
            p->rolling_result->mp3_len = 0;
        }
    }

    const bool transport_bad = p->rolling_response != NULL && p->rolling_response->transport_failed;
    const bool client_connected = p->rolling_client != NULL && esp_websocket_client_is_connected(p->rolling_client);
    if (!utt->final_segment && (err != ESP_OK || transport_bad || (p->rolling_client != NULL && !client_connected))) {
        p->turn_transport_failed = true;
    }
    if (utt->final_segment && p->turn_transport_failed && p->turn_pcm != NULL && p->turn_pcm_len > 0 &&
        !p->turn_pcm_truncated) {
        ESP_LOGW(TAG, "voice-stream turn degraded earlier; retrying final turn via HTTP buffer (%uB)",
                 (unsigned)p->turn_pcm_len);
        rolling_stream_session_close(p);
        voice_result_free(result);
        memset(result, 0, sizeof(*result));
        err = post_pcm_buffer(p, p->turn_pcm, p->turn_pcm_len, result);
    } else if (utt->final_segment && err != ESP_OK && p->turn_pcm != NULL && p->turn_pcm_len > 0 &&
               !p->turn_pcm_truncated) {
        ESP_LOGW(TAG, "voice-stream final segment failed; retrying full turn via HTTP buffer (%uB)",
                 (unsigned)p->turn_pcm_len);
        rolling_stream_session_close(p);
        voice_result_free(result);
        memset(result, 0, sizeof(*result));
        err = post_pcm_buffer(p, p->turn_pcm, p->turn_pcm_len, result);
    } else if (utt->final_segment && err != ESP_OK && p->turn_pcm_truncated) {
        ESP_LOGW(TAG,
                 "voice-stream final segment failed; skipping HTTP whole-turn fallback because PSRAM mirror is truncated (%uB)",
                 (unsigned)p->turn_pcm_len);
    }
    if ((err != ESP_OK && transport_bad) || (p->rolling_client != NULL && !client_connected)) {
        rolling_stream_session_close(p);
    } else if (utt->final_segment && p->rolling_response != NULL && p->rolling_result != NULL) {
        rolling_stream_reset_response(p->rolling_response);
        rolling_stream_reset_result(p->rolling_result);
    }
    ESP_LOGI(TAG, "voice-stream WS %s segment #%u bytes=%u sent=%u in %ums", err == ESP_OK ? "ok" : "fail",
             (unsigned)utt->sequence, (unsigned)utt->byte_count, (unsigned)sent_bytes,
             (unsigned)(ticks_ms() - t0));
    return err;
}

static esp_err_t play_mp3(astrolabe_audio_pipeline_t *p, const uint8_t *mp3, size_t mp3_len)
{
    if (mp3 == NULL || mp3_len < 64 || p->cfg.io.write == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    mp3dec_t *dec = heap_caps_malloc(sizeof(mp3dec_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (dec == NULL) {
        dec = malloc(sizeof(mp3dec_t));
    }
    if (dec == NULL) {
        return ESP_ERR_NO_MEM;
    }
    mp3dec_frame_info_t info;
    int16_t *pcm = heap_caps_malloc(MINIMP3_MAX_SAMPLES_PER_FRAME * sizeof(int16_t) * 2,
                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (pcm == NULL) {
        pcm = malloc(MINIMP3_MAX_SAMPLES_PER_FRAME * sizeof(int16_t) * 2);
    }
    if (pcm == NULL) {
        free(dec);
        return ESP_ERR_NO_MEM;
    }
    mp3dec_init(dec);
    if (p->cfg.io.mute != NULL) {
        p->cfg.io.mute(false, p->cfg.io.user);
    }
    size_t offset = 0;
    int current_hz = 0;
    while (offset < mp3_len) {
        memset(&info, 0, sizeof(info));
        int samples = mp3dec_decode_frame(dec, mp3 + offset, (int)(mp3_len - offset), pcm, &info);
        if (info.frame_bytes <= 0) {
            break;
        }
        offset += (size_t)info.frame_bytes;
        if (samples <= 0) {
            continue;
        }
        if (info.hz > 0 && info.hz != current_hz && p->cfg.io.set_rate != NULL) {
            current_hz = info.hz;
            (void)p->cfg.io.set_rate((uint32_t)info.hz, p->cfg.io.user);
        }
        if (info.channels == 1) {
            int16_t *stereo = pcm + MINIMP3_MAX_SAMPLES_PER_FRAME;
            for (int i = samples - 1; i >= 0; --i) {
                stereo[i * 2] = pcm[i];
                stereo[i * 2 + 1] = pcm[i];
            }
            (void)p->cfg.io.write(stereo, (size_t)samples * 2u, 1000, p->cfg.io.user);
        } else {
            const size_t out_samples = (size_t)samples * (size_t)(info.channels > 0 ? info.channels : 1);
            (void)p->cfg.io.write(pcm, out_samples, 1000, p->cfg.io.user);
        }
        vTaskDelay(1);
    }
    if (p->cfg.io.set_rate != NULL) {
        (void)p->cfg.io.set_rate(p->cfg.sample_rate_hz, p->cfg.io.user);
    }
    free(pcm);
    free(dec);
    return ESP_OK;
}

static uint32_t isqrt_u64(uint64_t value)
{
    uint64_t bit = 1ULL << 62;
    while (bit > value) {
        bit >>= 2;
    }
    uint64_t result = 0;
    while (bit != 0) {
        if (value >= result + bit) {
            value -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }
    return (uint32_t)result;
}

static uint32_t frame_rms(const int16_t *frame, size_t count)
{
    uint64_t acc = 0;
    for (size_t i = 0; i < count; ++i) {
        int32_t s = frame[i];
        acc += (uint64_t)(s * s);
    }
    return count == 0 ? 0 : isqrt_u64(acc / count);
}

static uint8_t pipeline_wave_level(const astrolabe_audio_pipeline_t *p, uint32_t rms)
{
    const uint32_t floor = p != NULL && p->noise_rms > 0 ? p->noise_rms : 40u;
    uint32_t ceiling = p != NULL ? dynamic_start_threshold(p) : 2200u;
    if (ceiling < floor + 400u) {
        ceiling = floor + 400u;
    }
    if (rms <= floor) {
        return 0;
    }
    if (rms >= ceiling) {
        return 255;
    }
    return (uint8_t)(((rms - floor) * 255u) / (ceiling - floor));
}

static void waveform_push(astrolabe_audio_pipeline_t *p, uint8_t level, bool streamed)
{
    if (p == NULL) {
        return;
    }
    portENTER_CRITICAL(&p->waveform_mux);
    p->waveform[p->waveform_head] = level;
    p->waveform_stream[p->waveform_head] = streamed ? 255 : 0;
    p->waveform_head = (uint16_t)((p->waveform_head + 1u) % ASTROLABE_AUDIO_PIPELINE_WAVEFORM_LEN);
    portEXIT_CRITICAL(&p->waveform_mux);
}

static void reset_capture(astrolabe_audio_pipeline_t *p)
{
    if (p->capture_fd >= 0) {
        close(p->capture_fd);
        p->capture_fd = -1;
    }
    p->capture_len_bytes = 0;
    p->speech_active = false;
    p->silence_frames = 0;
    p->speech_frames = 0;
    p->last_rms = 0;
    p->active_peak_rms = 0;
}

static void make_capture_slot_path(astrolabe_audio_pipeline_t *p, uint32_t slot, char *out, size_t out_len)
{
    char base[128];
    snprintf(base, sizeof(base), "%s", p->capture_base_path[0] != '\0' ? p->capture_base_path : DEFAULT_CAPTURE_FILE_PATH);
    char *dot = strrchr(base, '.');
    if (dot != NULL) {
        *dot = '\0';
        snprintf(out, out_len, "%s_%02u.%s", base, (unsigned)slot, dot + 1);
    } else {
        snprintf(out, out_len, "%s_%02u.pcm", base, (unsigned)slot);
    }
}

static bool reclaim_oldest_queued_segment(astrolabe_audio_pipeline_t *p)
{
    utterance_t dropped = {};
    if (p->utterance_queue == NULL || xQueueReceive(p->utterance_queue, &dropped, 0) != pdTRUE) {
        return false;
    }
    if (dropped.pcm_data != NULL) {
        free(dropped.pcm_data);
    } else {
        remove(dropped.path);
    }
    if (dropped.slot < p->capture_ring_slots) {
        p->capture_slot_busy[dropped.slot] = false;
    }
    ESP_LOGW(TAG, "capture ring dropped queued segment #%u slot=%u bytes=%u",
             (unsigned)dropped.sequence, (unsigned)dropped.slot, (unsigned)dropped.byte_count);
    return true;
}

static uint32_t drain_queued_segments(astrolabe_audio_pipeline_t *p, const char *reason)
{
    uint32_t dropped_count = 0;
    utterance_t dropped = {};
    if (p == NULL || p->utterance_queue == NULL) {
        return 0;
    }
    while (xQueueReceive(p->utterance_queue, &dropped, 0) == pdTRUE) {
        if (dropped.pcm_data != NULL) {
            free(dropped.pcm_data);
        } else if (dropped.path[0] != '\0') {
            remove(dropped.path);
        }
        if (dropped.slot < p->capture_ring_slots) {
            p->capture_slot_busy[dropped.slot] = false;
        }
        dropped_count++;
    }
    if (dropped_count > 0) {
        ESP_LOGW(TAG, "dropped %u queued segment(s) after %s",
                 (unsigned)dropped_count,
                 reason != NULL ? reason : "turn boundary");
    }
    return dropped_count;
}

static uint32_t drain_queued_segments_keep_files(astrolabe_audio_pipeline_t *p, const char *reason)
{
    uint32_t dropped_count = 0;
    utterance_t dropped = {};
    if (p == NULL || p->utterance_queue == NULL) {
        return 0;
    }
    while (xQueueReceive(p->utterance_queue, &dropped, 0) == pdTRUE) {
        if (dropped.pcm_data != NULL) {
            free(dropped.pcm_data);
        }
        if (dropped.slot < p->capture_ring_slots) {
            p->capture_slot_busy[dropped.slot] = false;
        }
        dropped_count++;
    }
    if (dropped_count > 0) {
        ESP_LOGW(TAG, "dropped %u queued segment(s) after %s",
                 (unsigned)dropped_count,
                 reason != NULL ? reason : "turn boundary");
    }
    return dropped_count;
}

static bool advance_capture_slot(astrolabe_audio_pipeline_t *p)
{
    for (uint32_t pass = 0; pass < 2; ++pass) {
        for (uint32_t i = 0; i < p->capture_ring_slots; ++i) {
            p->capture_slot = (p->capture_slot + 1) % p->capture_ring_slots;
            make_capture_slot_path(p, p->capture_slot, p->capture_path, sizeof(p->capture_path));
            if (!p->capture_slot_busy[p->capture_slot]) {
                return true;
            }
        }
        if (!reclaim_oldest_queued_segment(p)) {
            break;
        }
    }
    return false;
}

static void update_noise_floor(astrolabe_audio_pipeline_t *p, uint32_t rms)
{
    if (p->noise_rms == 0) {
        p->noise_rms = rms;
        return;
    }
    if (rms > p->noise_rms) {
        p->noise_rms += (rms - p->noise_rms) >> VAD_NOISE_ATTACK_SHIFT;
    } else {
        p->noise_rms -= (p->noise_rms - rms) >> VAD_NOISE_RELEASE_SHIFT;
    }
}

static uint32_t dynamic_start_threshold(const astrolabe_audio_pipeline_t *p)
{
    uint32_t threshold = p->cfg.rms_start;
    if (p->noise_rms > 0) {
        const uint32_t adaptive = p->noise_rms + (p->noise_rms >> 4) + VAD_START_MIN_DELTA_RMS;
        if (adaptive > threshold) {
            threshold = adaptive;
        }
    }
    return threshold;
}

static uint32_t dynamic_end_threshold(const astrolabe_audio_pipeline_t *p)
{
    uint32_t threshold = p->cfg.rms_end;
    if (p->noise_rms > 0) {
        const uint32_t adaptive = p->noise_rms + (p->noise_rms >> 1) + VAD_END_MIN_DELTA_RMS;
        if (adaptive > threshold) {
            threshold = adaptive;
        }
    }
    return threshold;
}

static void vad_preroll_push(astrolabe_audio_pipeline_t *p, const int16_t *frame, size_t frame_samples)
{
    if (p == NULL || p->vad_preroll == NULL || p->vad_preroll_cap == 0 || frame == NULL || frame_samples == 0) {
        return;
    }
    const uint8_t *src = (const uint8_t *)frame;
    size_t len = frame_samples * sizeof(int16_t);
    if (len >= p->vad_preroll_cap) {
        src += len - p->vad_preroll_cap;
        len = p->vad_preroll_cap;
        memcpy(p->vad_preroll, src, len);
        p->vad_preroll_len = len;
        p->vad_preroll_write = 0;
        return;
    }
    const size_t first = len < p->vad_preroll_cap - p->vad_preroll_write
                             ? len
                             : p->vad_preroll_cap - p->vad_preroll_write;
    memcpy(p->vad_preroll + p->vad_preroll_write, src, first);
    if (first < len) {
        memcpy(p->vad_preroll, src + first, len - first);
    }
    p->vad_preroll_write = (p->vad_preroll_write + len) % p->vad_preroll_cap;
    p->vad_preroll_len += len;
    if (p->vad_preroll_len > p->vad_preroll_cap) {
        p->vad_preroll_len = p->vad_preroll_cap;
    }
}

static bool vad_preroll_seed_capture(astrolabe_audio_pipeline_t *p)
{
    if (p == NULL || p->vad_preroll == NULL || p->vad_preroll_len == 0) {
        return true;
    }
    size_t oldest = (p->vad_preroll_write + p->vad_preroll_cap - p->vad_preroll_len) % p->vad_preroll_cap;
    size_t remaining = p->vad_preroll_len;
    while (remaining > 0) {
        const size_t chunk = remaining < p->vad_preroll_cap - oldest
                                 ? remaining
                                 : p->vad_preroll_cap - oldest;
        if (capture_uses_ram(p)) {
            if (p->capture_ram == NULL || p->capture_len_bytes + chunk > p->capture_ram_cap) {
                return false;
            }
            memcpy(p->capture_ram + p->capture_len_bytes, p->vad_preroll + oldest, chunk);
        } else {
            const ssize_t wrote = p->capture_fd >= 0 ? write(p->capture_fd, p->vad_preroll + oldest, chunk) : -1;
            if (wrote < 0 || (size_t)wrote != chunk) {
                return false;
            }
        }
        p->capture_len_bytes += chunk;
        remaining -= chunk;
        oldest = 0;
    }
    ESP_LOGI(TAG, "VAD pre-roll seeded bytes=%u", (unsigned)p->vad_preroll_len);
    p->vad_preroll_len = 0;
    p->vad_preroll_write = 0;
    return true;
}

static bool begin_capture_file(astrolabe_audio_pipeline_t *p)
{
    if (capture_uses_ram(p)) {
        const size_t ram_cap = p->vad_preroll_cap > p->segment_cap_bytes
                                   ? p->vad_preroll_cap
                                   : p->segment_cap_bytes;
        if (p->capture_ram == NULL || p->capture_ram_cap < ram_cap) {
            free(p->capture_ram);
            p->capture_ram = heap_caps_malloc(ram_cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (p->capture_ram == NULL) {
                p->capture_ram = heap_caps_malloc(ram_cap, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            }
            if (p->capture_ram == NULL) {
                p->capture_ram = malloc(ram_cap);
            }
            if (p->capture_ram == NULL) {
                return false;
            }
            p->capture_ram_cap = ram_cap;
        }
        p->capture_len_bytes = 0;
        p->capture_started_ms = ticks_ms();
        p->capture_path[0] = '\0';
        return true;
    }
    if (p->capture_fd >= 0) {
        close(p->capture_fd);
        p->capture_fd = -1;
    }
    if (!advance_capture_slot(p)) {
        emit(p, ASTROLABE_AUDIO_PIPELINE_EVENT_ERROR, "capture ring full");
        return false;
    }
    remove(p->capture_path);
    p->capture_fd = open(p->capture_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    p->capture_len_bytes = 0;
    p->capture_started_ms = ticks_ms();
    return p->capture_fd >= 0;
}

static bool queue_utterance(astrolabe_audio_pipeline_t *p, bool final_segment)
{
    if (p->capture_fd >= 0) {
        close(p->capture_fd);
        p->capture_fd = -1;
    }
    const size_t min_bytes = (((size_t)p->cfg.min_ms * cfg_stt_sample_rate_hz(p)) / 1000) * sizeof(int16_t);
    const bool final_commit_only = final_segment && p->capture_len_bytes == 0 && p->turn_segment_count > 0;
    if (final_segment && p->capture_len_bytes < min_bytes && p->turn_segment_count == 0) {
        remove(p->capture_path);
        reset_capture(p);
        return false;
    }
    utterance_t utt = {
        .byte_count = final_commit_only ? 0 : p->capture_len_bytes,
        .sequence = p->capture_sequence++,
        .slot = final_commit_only ? MAX_RING_SLOTS : p->capture_slot,
        .final_segment = final_segment,
    };
    if (capture_uses_ram(p)) {
        utt.path[0] = '\0';
        if (!final_commit_only && p->capture_len_bytes > 0) {
            utt.pcm_data = heap_caps_malloc(p->capture_len_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (utt.pcm_data == NULL) {
                utt.pcm_data = heap_caps_malloc(p->capture_len_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            }
            if (utt.pcm_data == NULL) {
                utt.pcm_data = malloc(p->capture_len_bytes);
            }
            if (utt.pcm_data == NULL) {
                reset_capture(p);
                return false;
            }
            memcpy(utt.pcm_data, p->capture_ram, p->capture_len_bytes);
        }
    } else if (final_commit_only) {
        utt.path[0] = '\0';
        remove(p->capture_path);
    } else {
        snprintf(utt.path, sizeof(utt.path), "%s", p->capture_path);
        if (p->capture_len_bytes > 0) {
            utt.pcm_data = heap_caps_malloc(p->capture_len_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (utt.pcm_data == NULL) {
                utt.pcm_data = malloc(p->capture_len_bytes);
            }
            if (utt.pcm_data != NULL) {
                FILE *f = fopen(utt.path, "rb");
                if (f == NULL) {
                    free(utt.pcm_data);
                    utt.pcm_data = NULL;
                } else {
                    const size_t got = fread(utt.pcm_data, 1, p->capture_len_bytes, f);
                    fclose(f);
                    if (got != p->capture_len_bytes) {
                        free(utt.pcm_data);
                        utt.pcm_data = NULL;
                    }
                }
            }
            if (utt.pcm_data == NULL) {
                ESP_LOGW(TAG,
                         "capture segment PSRAM mirror unavailable final=%s len=%u",
                         final_segment ? "yes" : "no",
                         (unsigned)p->capture_len_bytes);
            }
        }
    }
    if (xQueueSend(p->utterance_queue, &utt, 0) != pdTRUE) {
        if (utt.pcm_data != NULL) {
            free(utt.pcm_data);
        }
        if (!final_commit_only && !capture_uses_ram(p)) {
            remove(p->capture_path);
        }
        if (!final_commit_only && !capture_uses_ram(p)) {
            p->capture_slot_busy[p->capture_slot] = false;
        }
        reset_capture(p);
        return false;
    }
    if (!final_commit_only && capture_uses_ram(p) && p->capture_ram != NULL && p->capture_len_bytes > 0) {
        if (!turn_pcm_append(p, p->capture_ram, p->capture_len_bytes)) {
            ESP_LOGW(TAG, "turn PCM mirror append failed len=%u", (unsigned)p->capture_len_bytes);
            p->turn_transport_failed = true;
        }
    }
    ESP_LOGI(TAG, "queued capture segment #%u final=%s bytes=%u path=%s",
             (unsigned)utt.sequence,
             final_segment ? "yes" : "no",
             (unsigned)utt.byte_count,
             capture_uses_ram(p) ? (final_commit_only ? "<commit-only>" : "<ram>") :
                                   (final_commit_only ? "<commit-only>" : utt.path));
    p->turn_segment_count++;
    if (!final_commit_only && !capture_uses_ram(p)) {
        p->capture_slot_busy[p->capture_slot] = true;
    }
    if (final_segment) {
        p->manual_capture_active = false;
        p->manual_capture_deadline_ms = 0;
        p->turn_segment_count = 0;
        // Block immediate re-trigger from the same utterance tail while the
        // final segment is being processed.
        start_capture_cooldown(p);
    }
    emit(p,
         ASTROLABE_AUDIO_PIPELINE_EVENT_CAPTURE_QUEUED,
         final_segment ? "final" : "segment");
    reset_capture(p);
    return true;
}

static bool rotate_capture_segment(astrolabe_audio_pipeline_t *p)
{
    const uint32_t silence_frames = p->silence_frames;
    const uint32_t active_peak_rms = p->active_peak_rms;
    if (!queue_utterance(p, false)) {
        return false;
    }
    if (!begin_capture_file(p)) {
        reset_capture(p);
        return false;
    }
    p->speech_active = true;
    p->active_peak_rms = active_peak_rms > p->last_rms ? active_peak_rms : p->last_rms;
    p->silence_frames = silence_frames;
    emit(p, ASTROLABE_AUDIO_PIPELINE_EVENT_CAPTURE_START, "segment");
    return true;
}

static bool push_frame(astrolabe_audio_pipeline_t *p, const int16_t *frame, size_t frame_samples)
{
    const uint32_t now_ms = ticks_ms();
    p->last_rms = frame_rms(frame, frame_samples);
    p->vad_frames_seen++;
    waveform_push(p, pipeline_wave_level(p, p->last_rms), p->speech_active);
    if (!p->speech_active) {
        vad_preroll_push(p, frame, frame_samples);
    }
    if (p->manual_capture && !p->speech_active) {
        const uint32_t quiet_threshold = dynamic_end_threshold(p);
        const bool quiet_frame = p->last_rms < quiet_threshold;
        if (quiet_frame) {
            if (p->manual_capture_quiet_frames < UINT32_MAX) {
                p->manual_capture_quiet_frames++;
            }
        } else {
            p->manual_capture_quiet_frames = 0;
        }
        const bool force_start =
            ticks_reached(now_ms, p->manual_capture_armed_ms + MANUAL_CAPTURE_PRESTART_FORCE_MS);
        if (!force_start && p->manual_capture_quiet_frames < MANUAL_CAPTURE_PRESTART_QUIET_FRAMES) {
            update_noise_floor(p, p->last_rms);
            return false;
        }
        const uint32_t quiet_frames = p->manual_capture_quiet_frames;
        p->manual_capture = false;
        p->manual_capture_quiet_frames = 0;
        p->capture_blocked_until_ms = 0;
        p->capture_needs_quiet = false;
        p->rearm_quiet_frames = 0;
        p->speech_frames = 0;
        if (!begin_capture_file(p)) {
            emit(p, ASTROLABE_AUDIO_PIPELINE_EVENT_ERROR, "capture file");
            reset_capture(p);
            return false;
        }
        p->turn_segment_count = 0;
        p->speech_active = true;
        p->manual_capture_active = true;
        p->active_peak_rms = p->last_rms;
        p->silence_frames = 0;
    const uint32_t manual_hold_ms =
        p->manual_capture_hold_ms >= MANUAL_CAPTURE_MIN_MS ? p->manual_capture_hold_ms : MANUAL_CAPTURE_MIN_MS;
    p->manual_capture_deadline_ms = p->manual_capture_hold_ms == 0 ? 0 : ticks_ms() + manual_hold_ms;
        turn_pcm_reset(p);
        ESP_LOGI(TAG, "manual capture start rms=%u noise=%u quiet_frames=%u forced=%d",
                 (unsigned)p->last_rms,
                 (unsigned)p->noise_rms,
                 (unsigned)quiet_frames,
                 force_start ? 1 : 0);
        emit(p, ASTROLABE_AUDIO_PIPELINE_EVENT_CAPTURE_START, "button");
    } else if (p->manual_capture) {
        p->manual_capture = false;
        p->manual_capture_quiet_frames = 0;
    }
    if (!p->speech_active && p->vad_frames_seen <= VAD_WARMUP_FRAMES) {
        update_noise_floor(p, p->last_rms);
        return false;
    }
    if (!p->speech_active && !ticks_reached(now_ms, p->capture_blocked_until_ms)) {
        p->speech_frames = 0;
        return false;
    }
    if (!p->speech_active && p->capture_needs_quiet) {
        if (p->last_rms >= dynamic_end_threshold(p)) {
            if (ticks_reached(now_ms, p->capture_blocked_until_ms + VAD_REARM_FORCE_MS)) {
                p->capture_needs_quiet = false;
                p->rearm_quiet_frames = 0;
            } else {
                p->rearm_quiet_frames = 0;
                p->speech_frames = 0;
                return false;
            }
        }
        if (p->capture_needs_quiet) {
            p->rearm_quiet_frames++;
            p->speech_frames = 0;
            update_noise_floor(p, p->last_rms);
            if (p->rearm_quiet_frames < VAD_REARM_QUIET_FRAMES) {
                return false;
            }
            p->capture_needs_quiet = false;
            p->rearm_quiet_frames = 0;
            return false;
        }
    }
    const uint32_t threshold = p->speech_active ? dynamic_end_threshold(p) : dynamic_start_threshold(p);
    const bool voiced = p->last_rms >= threshold;
    if (!p->speech_active) {
        if (voiced && ++p->speech_frames >= p->cfg.start_frames) {
            if (!begin_capture_file(p)) {
                emit(p, ASTROLABE_AUDIO_PIPELINE_EVENT_ERROR, "capture file");
                reset_capture(p);
                return false;
            }
            if (!vad_preroll_seed_capture(p)) {
                emit(p, ASTROLABE_AUDIO_PIPELINE_EVENT_ERROR, "capture pre-roll");
                reset_capture(p);
                return false;
            }
            p->turn_segment_count = 0;
            p->speech_active = true;
            p->manual_capture_active = false;
            p->active_peak_rms = p->last_rms;
            p->silence_frames = 0;
            turn_pcm_reset(p);
            ESP_LOGI(TAG, "VAD start rms=%u threshold=%u noise=%u frames=%u",
                     (unsigned)p->last_rms,
                     (unsigned)threshold,
                     (unsigned)p->noise_rms,
                     (unsigned)p->speech_frames);
            emit(p, ASTROLABE_AUDIO_PIPELINE_EVENT_CAPTURE_START, NULL);
        } else if (!voiced) {
            p->speech_frames = 0;
            update_noise_floor(p, p->last_rms);
        }
        return false;
    }
    if (p->last_rms > p->active_peak_rms) {
        p->active_peak_rms = p->last_rms;
    }

    const size_t frame_bytes = frame_samples * sizeof(int16_t);
    if (!rolling_websocket_enabled(p) && p->capture_len_bytes + frame_bytes > p->capture_cap_bytes) {
        return queue_utterance(p, true);
    }
    if (rolling_websocket_enabled(p) && p->capture_len_bytes + frame_bytes > p->segment_cap_bytes) {
        if (!rotate_capture_segment(p)) {
            return false;
        }
    }
    if (capture_uses_ram(p)) {
        if (p->capture_ram == NULL || p->capture_len_bytes + frame_bytes > p->capture_ram_cap) {
            emit(p, ASTROLABE_AUDIO_PIPELINE_EVENT_ERROR, "capture ram");
            reset_capture(p);
            return false;
        }
        memcpy(p->capture_ram + p->capture_len_bytes, frame, frame_bytes);
    } else {
        const ssize_t wrote = p->capture_fd >= 0 ? write(p->capture_fd, frame, frame_bytes) : -1;
        if (wrote < 0 || (size_t)wrote != frame_bytes) {
            ESP_LOGE(TAG, "capture write failed path=%s len=%u frame=%u wrote=%u errno=%d",
                     p->capture_path, (unsigned)p->capture_len_bytes, (unsigned)frame_bytes,
                     (unsigned)(wrote > 0 ? wrote : 0), errno);
            emit(p, ASTROLABE_AUDIO_PIPELINE_EVENT_ERROR, "capture write");
            remove(p->capture_path);
            reset_capture(p);
            return false;
        }
    }
    p->capture_len_bytes += frame_bytes;
    const uint32_t manual_hold_ms =
        p->manual_capture_hold_ms >= MANUAL_CAPTURE_MIN_MS ? p->manual_capture_hold_ms : MANUAL_CAPTURE_MIN_MS;
    if (p->manual_capture_active && p->manual_capture_deadline_ms != 0 &&
        ticks_reached(now_ms, p->manual_capture_deadline_ms)) {
        ESP_LOGI(TAG, "manual end bytes=%u hold_ms=%u rms=%u noise=%u peak=%u",
                 (unsigned)p->capture_len_bytes,
                 (unsigned)manual_hold_ms,
                 (unsigned)p->last_rms,
                 (unsigned)p->noise_rms,
                 (unsigned)p->active_peak_rms);
        return queue_utterance(p, true);
    }
    if (voiced) {
        if (p->silence_frames > VAD_SILENCE_LEAK_FRAMES) {
            p->silence_frames -= VAD_SILENCE_LEAK_FRAMES;
        } else {
            p->silence_frames = 0;
        }
    } else {
        if (p->manual_capture_active) {
            update_noise_floor(p, p->last_rms);
            if (p->manual_capture_deadline_ms != 0) {
                return false;
            }
        }
        update_noise_floor(p, p->last_rms);
        if (p->manual_capture_active && p->manual_capture_deadline_ms == 0 &&
            p->active_peak_rms < dynamic_start_threshold(p)) {
            if (ticks_reached(now_ms, p->manual_capture_armed_ms + MANUAL_CAPTURE_NO_SPEECH_TIMEOUT_MS)) {
                if (p->turn_segment_count > 0) {
                    ESP_LOGI(TAG,
                             "manual quiet final bytes=%u segments=%u rms=%u threshold=%u noise=%u peak=%u",
                             (unsigned)p->capture_len_bytes,
                             (unsigned)p->turn_segment_count,
                             (unsigned)p->last_rms,
                             (unsigned)dynamic_start_threshold(p),
                             (unsigned)p->noise_rms,
                             (unsigned)p->active_peak_rms);
                    p->manual_capture = false;
                    return queue_utterance(p, true);
                }
                ESP_LOGW(TAG, "No speech detected bytes=%u rms=%u threshold=%u noise=%u peak=%u",
                         (unsigned)p->capture_len_bytes,
                         (unsigned)p->last_rms,
                         (unsigned)dynamic_start_threshold(p),
                         (unsigned)p->noise_rms,
                         (unsigned)p->active_peak_rms);
                p->manual_capture = false;
                p->manual_capture_active = false;
                p->manual_capture_deadline_ms = 0;
                p->manual_capture_quiet_frames = 0;
                p->turn_segment_count = 0;
                (void)drain_queued_segments(p, "no speech");
                reset_capture(p);
                emit(p, ASTROLABE_AUDIO_PIPELINE_EVENT_ERROR, "No speech detected");
            }
            return false;
        }
        if (++p->silence_frames >= p->cfg.silence_frames) {
            ESP_LOGI(TAG, "VAD end bytes=%u rms=%u threshold=%u noise=%u silence=%u peak=%u",
                     (unsigned)p->capture_len_bytes,
                     (unsigned)p->last_rms,
                     (unsigned)threshold,
                     (unsigned)p->noise_rms,
                     (unsigned)p->silence_frames,
                     (unsigned)p->active_peak_rms);
            return queue_utterance(p, true);
        }
    }
    return false;
}

static void listen_task(void *arg)
{
    astrolabe_audio_pipeline_t *p = (astrolabe_audio_pipeline_t *)arg;
    int16_t *frame = heap_caps_malloc(p->cfg.frame_samples * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (frame == NULL) {
        frame = heap_caps_malloc(p->cfg.frame_samples * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (frame == NULL) {
        frame = malloc(p->cfg.frame_samples * sizeof(int16_t));
    }
    if (frame == NULL) {
        emit(p, ASTROLABE_AUDIO_PIPELINE_EVENT_ERROR, "listen frame alloc");
        p->listen_task = NULL;
        vTaskDelete(NULL);
    }
    emit(p, ASTROLABE_AUDIO_PIPELINE_EVENT_LISTENING, NULL);
    while (p->running && p->listen_should_run) {
        size_t got = 0;
        esp_err_t err = p->cfg.io.read(frame, p->cfg.frame_samples, &got, 100, p->cfg.io.user);
        if (err != ESP_OK || got == 0) {
            if (err != ESP_OK) {
                p->read_err_count++;
                p->last_read_err = err;
            } else {
                p->read_zero_count++;
            }
            const TickType_t backoff_ticks = pdMS_TO_TICKS(20);
            vTaskDelay(backoff_ticks > 0 ? backoff_ticks : 1);
            continue;
        }
        p->read_ok_count++;
        p->last_read_err = ESP_OK;
        (void)push_frame(p, frame, got);
    }
    free(frame);
    p->listen_task = NULL;
    vTaskDelete(NULL);
}

static void voice_task(void *arg)
{
    astrolabe_audio_pipeline_t *p = (astrolabe_audio_pipeline_t *)arg;
    ESP_LOGI(TAG, "voice task ready");
    while (p->running) {
        utterance_t utt = {};
        if (xQueueReceive(p->utterance_queue, &utt, pdMS_TO_TICKS(250)) != pdTRUE) {
            continue;
        }
        ESP_LOGI(TAG, "voice dequeue segment #%u final=%s bytes=%u path=%s",
                 (unsigned)utt.sequence,
                 utt.final_segment ? "yes" : "no",
                 (unsigned)utt.byte_count,
                 utt.path);
        if (utt.final_segment && !p->cfg.duplex) {
            ESP_LOGI(TAG, "voice stopping listen before final segment #%u", (unsigned)utt.sequence);
            stop_listen_task_if_running(p, 1000);
            ESP_LOGI(TAG, "voice listen stop complete for final segment #%u", (unsigned)utt.sequence);
        } else if (utt.final_segment) {
            ESP_LOGI(TAG, "voice keeping listen active for duplex final segment #%u", (unsigned)utt.sequence);
        }
        if (utt.final_segment) {
            emit(p, ASTROLABE_AUDIO_PIPELINE_EVENT_THINKING, NULL);
        }
        /* Context is needed by the single end-of-turn request, not by each
         * fire-and-forget PCM transport frame. */
        if (utt.final_segment) {
            prepare_context(p);
        }
        ESP_LOGI(TAG, "posting capture segment #%u final=%s bytes=%u path=%s",
                 (unsigned)utt.sequence, utt.final_segment ? "yes" : "no",
                 (unsigned)utt.byte_count, utt.path);
        voice_result_t *result = pipeline_calloc_prefer_psram(1, sizeof(*result));
        if (result == NULL) {
            // Do not remove SPIFFS spool files from the voice task. In duplex
            // builds this task may use a PSRAM stack to preserve internal heap
            // for WebSocket/TLS, while flash metadata updates require an
            // internal stack during cache-disabled sections. The capture task
            // truncates/reuses fixed ring slots on an internal stack.
            if (utt.pcm_data != NULL) {
                free(utt.pcm_data);
            }
            if (utt.slot < p->capture_ring_slots) {
                p->capture_slot_busy[utt.slot] = false;
            }
            emit(p, ASTROLABE_AUDIO_PIPELINE_EVENT_ERROR, "voice-result alloc");
            start_capture_cooldown(p);
            if (should_idle_listen_between_turns(p) && start_listen_task_if_needed(p) == ESP_OK) {
                emit(p, ASTROLABE_AUDIO_PIPELINE_EVENT_LISTENING, NULL);
            } else if (should_idle_listen_between_turns(p)) {
                emit(p, ASTROLABE_AUDIO_PIPELINE_EVENT_ERROR, "listen restart");
            }
            continue;
        }
        esp_err_t err = ESP_OK;
        if (rolling_websocket_enabled(p)) {
            err = stream_pcm_file(p, &utt, result);
            if (err != ESP_OK) {
                if (!utt.final_segment) {
                    ESP_LOGW(TAG, "voice-stream dropped rolling segment #%u after send failure",
                             (unsigned)utt.sequence);
                    err = ESP_OK;
                } else {
                    ESP_LOGW(TAG, "voice-stream failed for final segment #%u", (unsigned)utt.sequence);
                }
            }
        } else if (!utt.final_segment) {
            ESP_LOGW(TAG, "capture dropped unexpected non-final segment #%u in flash-post mode",
                     (unsigned)utt.sequence);
        } else if (utt.pcm_data != NULL && utt.byte_count > 0) {
            err = post_pcm_buffer(p, utt.pcm_data, utt.byte_count, result);
        } else {
            err = post_pcm_file(p, utt.path, utt.byte_count, result);
        }
        // Keep flash spool files in place; fixed ring slots are truncated on
        // reuse by the internal-stack capture task.
        if (utt.pcm_data != NULL) {
            free(utt.pcm_data);
        }
        if (utt.slot < p->capture_ring_slots) {
            p->capture_slot_busy[utt.slot] = false;
        }
        if (err != ESP_OK) {
            if (utt.final_segment && result->reply[0] == '\0' && p->rolling_result != NULL &&
                (p->rolling_result->reply[0] != '\0' || p->rolling_result->transcript[0] != '\0')) {
                rolling_stream_copy_snapshot(p->rolling_result, result, false);
            }
            if (utt.final_segment && result->reply[0] != '\0' && result->mp3_len == 0) {
                esp_err_t recover_err = fetch_tts_for_reply(p, result);
                if (recover_err == ESP_OK) {
                    ESP_LOGW(TAG, "voice-stream recovered final turn audio via HTTP TTS");
                    err = ESP_OK;
                } else {
                    ESP_LOGW(TAG, "voice-stream final-turn TTS recovery failed: %s",
                             esp_err_to_name(recover_err));
                }
            }
            if (utt.final_segment) {
                // Ordinary final-turn failures (for example "no speech detected"
                // from the backend) can recover by closing the current rolling
                // websocket session and re-opening it on the next turn. Now
                // that manual-trigger turns prewarm on demand, keeping a
                // failed session alive only burns internal RAM between turns.
                // Reserve the heavier full-pipeline reset flag for true local
                // memory exhaustion, where reusing the current task graph is
                // unlikely to succeed.
                p->unhealthy = (err == ESP_ERR_NO_MEM);
                rolling_stream_session_close(p);
                (capture_uses_ram(p) ? drain_queued_segments : drain_queued_segments_keep_files)(p, "final error");
            }
            voice_result_free(result);
            free(result);
            turn_pcm_reset(p);
            emit(p, ASTROLABE_AUDIO_PIPELINE_EVENT_ERROR, "voice-pipeline");
            start_capture_cooldown(p);
            if (rolling_websocket_enabled(p)) {
                const esp_err_t prewarm_err = rolling_stream_session_open(p);
                if (prewarm_err != ESP_OK) {
                    ESP_LOGW(TAG, "voice-stream error recovery prewarm failed: %s",
                             esp_err_to_name(prewarm_err));
                }
            }
            if (should_idle_listen_between_turns(p) && start_listen_task_if_needed(p) == ESP_OK) {
                emit(p, ASTROLABE_AUDIO_PIPELINE_EVENT_LISTENING, NULL);
            } else if (should_idle_listen_between_turns(p)) {
                emit(p, ASTROLABE_AUDIO_PIPELINE_EVENT_ERROR, "listen restart");
            }
            continue;
        }
        if (!utt.final_segment) {
            if (p->cfg.on_result != NULL &&
                (result->faculty_slug[0] != '\0' || result->faculty_name[0] != '\0')) {
                p->cfg.on_result(result->transcript, "", result->faculty_slug, result->faculty_name, p->cfg.event_user);
            }
            if (result->transcript[0] != '\0') {
                ESP_LOGI(TAG, "early transcript segment #%u: %s", (unsigned)utt.sequence, result->transcript);
            }
            voice_result_free(result);
            free(result);
            continue;
        }
        if (result->transcript[0] != '\0') {
            emit(p, ASTROLABE_AUDIO_PIPELINE_EVENT_TRANSCRIPT, result->transcript);
        }
        if (result->reply[0] != '\0') {
            emit(p, ASTROLABE_AUDIO_PIPELINE_EVENT_REPLY, result->reply);
        }
        if (p->cfg.on_result != NULL) {
            p->cfg.on_result(result->transcript, result->reply, result->faculty_slug, result->faculty_name, p->cfg.event_user);
        }
        if (p->cfg.on_session != NULL && (result->session_id[0] != '\0' || result->expression[0] != '\0')) {
            p->cfg.on_session(result->session_id, result->expression, p->cfg.event_user);
        }
        // Once a final streamed turn has produced its full semantic result, the
        // websocket session is no longer needed for local playback. Keeping it
        // alive through TTS allows ping/close traffic to race with the speaker
        // path and has caused otherwise-good turns to die before TURN_DONE.
        rolling_stream_session_close(p);
        if (result->mp3 != NULL && result->mp3_len > 0) {
            emit(p, ASTROLABE_AUDIO_PIPELINE_EVENT_SPEAKING,
                 result->faculty_name[0] != '\0' ? result->faculty_name : p->cfg.faculty_name);
            if (p->cfg.play_mp3 != NULL) {
                (void)p->cfg.play_mp3(result->mp3, result->mp3_len, p->cfg.event_user);
            } else {
                (void)play_mp3(p, result->mp3, result->mp3_len);
            }
        }
        voice_result_free(result);
        free(result);
        turn_pcm_reset(p);
        (capture_uses_ram(p) ? drain_queued_segments : drain_queued_segments_keep_files)(p, "final success");
        /* The response socket is closed before playback to keep WebSocket
         * callbacks away from I2S. Re-open it now, before the next person can
         * begin speaking, so their pre-roll is never spent on TLS startup. */
        if (rolling_websocket_enabled(p)) {
            const esp_err_t prewarm_err = rolling_stream_session_open(p);
            if (prewarm_err != ESP_OK) {
                ESP_LOGW(TAG, "voice-stream next-turn prewarm failed: %s",
                         esp_err_to_name(prewarm_err));
            }
        }
        emit(p, ASTROLABE_AUDIO_PIPELINE_EVENT_TURN_DONE, NULL);
        start_capture_cooldown(p);
        if (should_idle_listen_between_turns(p) && start_listen_task_if_needed(p) == ESP_OK) {
            emit(p, ASTROLABE_AUDIO_PIPELINE_EVENT_LISTENING, NULL);
        } else if (should_idle_listen_between_turns(p)) {
            emit(p, ASTROLABE_AUDIO_PIPELINE_EVENT_ERROR, "listen restart");
        }
        /* Keep the worker resident between rolling turns. Its static stack is
         * reserved for the pipeline lifetime, so deleting the task saves no
         * memory and leaves hands-free VAD captures queued without a consumer.
         * The blocking queue receive keeps the idle worker inexpensive. */
    }
    p->voice_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t astrolabe_audio_pipeline_create(const astrolabe_audio_pipeline_config_t *config,
                                          astrolabe_audio_pipeline_t **out_pipeline)
{
    if (config == NULL || out_pipeline == NULL || config->io.read == NULL || config->io.write == NULL ||
        config->endpoint_url == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    astrolabe_audio_pipeline_t *p = pipeline_calloc_prefer_psram(1, sizeof(*p));
    if (p == NULL) {
        return ESP_ERR_NO_MEM;
    }
    p->cfg = *config;
    if (p->cfg.sample_rate_hz == 0) {
        p->cfg.sample_rate_hz = DEFAULT_SAMPLE_RATE_HZ;
    }
    if (p->cfg.frame_samples == 0) {
        p->cfg.frame_samples = DEFAULT_FRAME_SAMPLES;
    }
    if (p->cfg.rms_start == 0) {
        p->cfg.rms_start = DEFAULT_RMS_START;
    }
    if (p->cfg.rms_end == 0) {
        p->cfg.rms_end = DEFAULT_RMS_END;
    }
    if (p->cfg.start_frames == 0) {
        p->cfg.start_frames = DEFAULT_START_FRAMES;
    }
    if (p->cfg.silence_frames == 0) {
        p->cfg.silence_frames = DEFAULT_SILENCE_FRAMES;
    }
    const bool unbounded_rolling_capture =
        p->cfg.max_seconds == 0 && rolling_websocket_enabled(p);
    if (p->cfg.max_seconds == 0 && !unbounded_rolling_capture) {
        p->cfg.max_seconds = DEFAULT_MAX_SECONDS;
    }
    if (p->cfg.min_ms == 0) {
        p->cfg.min_ms = DEFAULT_MIN_MS;
    }
    if (p->cfg.capture_cooldown_ms == 0) {
        p->cfg.capture_cooldown_ms = DEFAULT_CAPTURE_COOLDOWN_MS;
    }
    const uint32_t listen_stack_bytes = p->cfg.listen_stack ? p->cfg.listen_stack : DEFAULT_LISTEN_STACK;
    p->listen_task_stack_words = (listen_stack_bytes + sizeof(StackType_t) - 1u) / sizeof(StackType_t);
    /* The listener writes directly to SPIFFS; its task stack must remain accessible
     * while flash cache is disabled, so never fall back to PSRAM here. */
    const size_t listen_storage_bytes = p->listen_task_stack_words * sizeof(StackType_t);
    if (p->cfg.listen_stack_storage != NULL && p->cfg.listen_tcb_storage != NULL &&
        p->cfg.listen_stack_storage_bytes >= listen_storage_bytes) {
        p->listen_task_stack_storage = p->cfg.listen_stack_storage;
        p->listen_task_tcb_storage = p->cfg.listen_tcb_storage;
        p->owns_listen_task_storage = false;
    } else {
        p->listen_task_stack_storage =
            heap_caps_malloc(listen_storage_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        p->listen_task_tcb_storage =
            heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        p->owns_listen_task_storage = true;
    }
    const uint32_t voice_stack_bytes = p->cfg.voice_stack ? p->cfg.voice_stack : DEFAULT_VOICE_STACK;
    p->voice_task_stack_words = (voice_stack_bytes + sizeof(StackType_t) - 1u) / sizeof(StackType_t);
    p->voice_task_stack_storage = pipeline_alloc_stack(p->voice_task_stack_words, p->cfg.duplex);
    p->voice_task_tcb_storage =
        heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (p->listen_task_stack_storage == NULL || p->listen_task_tcb_storage == NULL ||
        p->voice_task_stack_storage == NULL || p->voice_task_tcb_storage == NULL) {
        ESP_LOGE(TAG,
                 "task storage alloc failed listen_stack=%p listen_tcb=%p voice_stack=%p voice_tcb=%p internal=%u largest=%u psram=%u",
                 (void *)p->listen_task_stack_storage,
                 (void *)p->listen_task_tcb_storage,
                 (void *)p->voice_task_stack_storage,
                 (void *)p->voice_task_tcb_storage,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (p->owns_listen_task_storage) {
            free(p->listen_task_stack_storage);
            free(p->listen_task_tcb_storage);
        }
        free(p->voice_task_stack_storage);
        free(p->voice_task_tcb_storage);
        free(p);
        return ESP_ERR_NO_MEM;
    }
    snprintf(p->capture_base_path, sizeof(p->capture_base_path), "%s",
             p->cfg.capture_file_path != NULL ? p->cfg.capture_file_path : DEFAULT_CAPTURE_FILE_PATH);
    p->capture_ring_slots = p->cfg.capture_ring_slots;
    if (p->capture_ring_slots == 0) {
        p->capture_ring_slots = DEFAULT_RING_SLOTS;
    }
    if (p->capture_ring_slots > MAX_RING_SLOTS) {
        p->capture_ring_slots = MAX_RING_SLOTS;
    }
    /* A rolling Theritor turn is an actual stream: segment rotation bounds
     * memory while VAD silence, rather than elapsed time, ends the utterance.
     * Batch transports retain their configured duration bound. */
    p->capture_cap_bytes = unbounded_rolling_capture
                               ? SIZE_MAX
                               : (size_t)p->cfg.max_seconds * cfg_stt_sample_rate_hz(p) * sizeof(int16_t);
    p->vad_preroll_cap = ((size_t)p->cfg.sample_rate_hz * VAD_PREROLL_MS / 1000u) * sizeof(int16_t);
    if (p->vad_preroll_cap > 0) {
        p->vad_preroll = heap_caps_malloc(p->vad_preroll_cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (p->vad_preroll == NULL) {
            p->vad_preroll = malloc(p->vad_preroll_cap);
        }
        if (p->vad_preroll == NULL) {
            ESP_LOGW(TAG, "VAD pre-roll unavailable bytes=%u", (unsigned)p->vad_preroll_cap);
            p->vad_preroll_cap = 0;
        }
    }
    if (rolling_websocket_enabled(p)) {
        const uint32_t segment_ms = p->cfg.capture_segment_ms != 0 ? p->cfg.capture_segment_ms : DEFAULT_SEGMENT_MS;
        p->segment_cap_bytes = (((size_t)segment_ms * cfg_stt_sample_rate_hz(p)) / 1000) * sizeof(int16_t);
    } else {
        p->segment_cap_bytes = p->capture_cap_bytes;
    }
    if (p->segment_cap_bytes == 0 ||
        (!unbounded_rolling_capture && p->segment_cap_bytes > p->capture_cap_bytes)) {
        p->segment_cap_bytes = p->capture_cap_bytes;
    }
    if (!capture_uses_ram(p) && !p->cfg.capture_skip_spiffs_mount) {
        esp_vfs_spiffs_conf_t spiffs = {
            .base_path = p->cfg.capture_mount_path != NULL ? p->cfg.capture_mount_path : DEFAULT_CAPTURE_MOUNT_PATH,
            .partition_label = p->cfg.capture_partition_label,
            .max_files = 4,
            .format_if_mount_failed = true,
        };
        esp_err_t mount_err = esp_vfs_spiffs_register(&spiffs);
        if (mount_err == ESP_OK) {
            p->spiffs_mounted = true;
        } else if (mount_err != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG,
                     "capture spool mount failed partition=%s err=%s internal=%u largest=%u psram=%u",
                     p->cfg.capture_partition_label != NULL ? p->cfg.capture_partition_label : "-",
                     esp_err_to_name(mount_err),
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                     (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
            astrolabe_audio_pipeline_destroy(p);
            return mount_err;
        }
    }
    if (!capture_uses_ram(p)) {
        for (uint32_t slot = 0; slot < p->capture_ring_slots; ++slot) {
            char path[128];
            make_capture_slot_path(p, slot, path, sizeof(path));
            remove(path);
        }
        remove(p->capture_base_path);
        p->capture_slot = p->capture_ring_slots - 1;
        size_t spiffs_total = 0;
        size_t spiffs_used = 0;
        if (esp_spiffs_info(p->cfg.capture_partition_label, &spiffs_total, &spiffs_used) == ESP_OK) {
            ESP_LOGI(TAG, "capture spool spiffs total=%u used=%u free=%u slots=%u segment=%uB mirror_max=%uB",
                     (unsigned)spiffs_total, (unsigned)spiffs_used, (unsigned)(spiffs_total - spiffs_used),
                     (unsigned)p->capture_ring_slots, (unsigned)p->segment_cap_bytes,
                     (unsigned)turn_pcm_mirror_max_bytes(p));
        }
    } else {
        p->capture_slot = 0;
        ESP_LOGI(TAG, "capture spool ram segment=%uB slots=%u max=%uB mirror_max=%uB",
                 (unsigned)p->segment_cap_bytes,
                 (unsigned)p->capture_ring_slots,
                 (unsigned)p->capture_cap_bytes,
                 (unsigned)turn_pcm_mirror_max_bytes(p));
    }
    p->utterance_queue = xQueueCreate(p->capture_ring_slots, sizeof(utterance_t));
    if (p->utterance_queue == NULL) {
        ESP_LOGE(TAG,
                 "utterance queue alloc failed slots=%u item=%u internal=%u largest=%u psram=%u",
                 (unsigned)p->capture_ring_slots,
                 (unsigned)sizeof(utterance_t),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        astrolabe_audio_pipeline_destroy(p);
        return ESP_ERR_NO_MEM;
    }
    p->waveform_mux = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    p->capture_fd = -1;
    reset_capture(p);
    *out_pipeline = p;
    return ESP_OK;
}

void astrolabe_audio_pipeline_set_synthetic_validation(astrolabe_audio_pipeline_t *p,
                                                       bool enabled)
{
    if (p != NULL) {
        p->cfg.synthetic_validation = enabled;
    }
}

esp_err_t astrolabe_audio_pipeline_start(astrolabe_audio_pipeline_t *p)
{
    if (p == NULL || p->running) {
        return ESP_ERR_INVALID_STATE;
    }
    p->running = true;
    p->unhealthy = false;
    // Rolling duplex capture must be able to start even when the backend is
    // temporarily unavailable; each queued segment opens/reopens transport on
    // demand from the voice task.
    // Duplex mode may place the voice worker stack in PSRAM to leave enough
    // internal heap for WebSocket/TLS. Non-duplex builds still prefer internal
    // stack storage for flash-cache-disabled paths.
    esp_err_t voice_err = start_voice_task_if_needed(p);
    if (voice_err != ESP_OK) {
        stop_listen_task_if_running(p, 1000);
        rolling_stream_session_close(p);
        p->running = false;
        return voice_err;
    }
    if (rolling_websocket_enabled(p)) {
        const esp_err_t stream_err = rolling_stream_session_open(p);
        if (stream_err != ESP_OK) {
            ESP_LOGW(TAG, "voice-stream preconnect deferred: %s", esp_err_to_name(stream_err));
        }
    }
    p->listen_should_run = true;
    esp_err_t listen_err = start_listen_task_if_needed(p);
    if (listen_err != ESP_OK) {
        p->running = false;
        wait_for_task_exit(&p->voice_task, 1000);
        rolling_stream_session_close(p);
        return listen_err;
    }
    return ESP_OK;
}

void astrolabe_audio_pipeline_stop(astrolabe_audio_pipeline_t *p)
{
    if (p == NULL) {
        return;
    }
    p->running = false;
    p->listen_should_run = false;
}

void astrolabe_audio_pipeline_destroy(astrolabe_audio_pipeline_t *p)
{
    if (p == NULL) {
        return;
    }
    astrolabe_audio_pipeline_stop(p);
    wait_for_task_exit(&p->listen_task, 1000);
    wait_for_task_exit(&p->voice_task, 1000);
    if (p->utterance_queue != NULL) {
        utterance_t utt = {};
        while (xQueueReceive(p->utterance_queue, &utt, 0) == pdTRUE) {
            if (utt.pcm_data != NULL) {
                free(utt.pcm_data);
            } else {
                remove(utt.path);
            }
        }
        vQueueDelete(p->utterance_queue);
    }
    rolling_stream_session_close(p);
    rolling_stream_state_free(p);
    reset_capture(p);
    if (!capture_uses_ram(p)) {
        for (uint32_t slot = 0; slot < p->capture_ring_slots; ++slot) {
            char path[128];
            make_capture_slot_path(p, slot, path, sizeof(path));
            remove(path);
        }
        remove(p->capture_path);
    }
    // Keep the flash spool mounted for the firmware lifetime. Re-registering
    // SPIFFS after Wi-Fi/LVGL/TLS have fragmented internal heap can fail even
    // though the mounted spool itself is healthy.
    free(p->capture_ram);
    free(p->turn_pcm);
    free(p->vad_preroll);
    if (p->owns_listen_task_storage) {
        free(p->listen_task_stack_storage);
        free(p->listen_task_tcb_storage);
    }
    free(p->voice_task_stack_storage);
    free(p->voice_task_tcb_storage);
    free(p);
}

esp_err_t astrolabe_audio_pipeline_trigger_capture(astrolabe_audio_pipeline_t *p)
{
    return astrolabe_audio_pipeline_trigger_capture_for_ms(p, MANUAL_CAPTURE_DEFAULT_HOLD_MS);
}

esp_err_t astrolabe_audio_pipeline_trigger_capture_for_ms(astrolabe_audio_pipeline_t *p, uint32_t hold_ms)
{
    if (p == NULL || !p->running) {
        return ESP_ERR_INVALID_STATE;
    }
    const bool open_ended_duplex_capture = hold_ms == 0 && p->cfg.duplex && rolling_websocket_enabled(p);
    if (!open_ended_duplex_capture && hold_ms < MANUAL_CAPTURE_MIN_MS) {
        hold_ms = MANUAL_CAPTURE_MIN_MS;
    } else if (!open_ended_duplex_capture && hold_ms > 15000u) {
        hold_ms = 15000u;
    }
    if (rolling_websocket_enabled(p)) {
        esp_err_t listen_err = start_listen_task_if_needed(p);
        if (listen_err != ESP_OK) {
            ESP_LOGW(TAG, "listen start before manual capture failed: %s", esp_err_to_name(listen_err));
            return listen_err;
        }
        esp_err_t voice_err = start_voice_task_if_needed(p);
        if (voice_err != ESP_OK) {
            stop_listen_task_if_running(p, 1000);
            rolling_stream_session_close(p);
            ESP_LOGW(TAG, "voice task restart after prewarm failed: %s", esp_err_to_name(voice_err));
            return voice_err;
        }
    }
    if (p->speech_active || p->capture_fd >= 0) {
        ESP_LOGI(TAG, "manual capture overrides active segment bytes=%u", (unsigned)p->capture_len_bytes);
        if (p->capture_fd >= 0) {
            close(p->capture_fd);
            p->capture_fd = -1;
        }
        if (!capture_uses_ram(p) && p->capture_path[0] != '\0') {
            remove(p->capture_path);
        }
        reset_capture(p);
    }
    p->manual_capture_hold_ms = hold_ms;
    p->manual_capture = true;
    p->manual_capture_armed_ms = ticks_ms();
    p->manual_capture_quiet_frames = 0;
    p->noise_rms = 0;
    p->vad_frames_seen = 0;
    p->speech_frames = 0;
    p->silence_frames = 0;
    p->active_peak_rms = 0;
    p->last_rms = 0;
    p->capture_blocked_until_ms = 0;
    p->capture_needs_quiet = false;
    p->rearm_quiet_frames = 0;
    return ESP_OK;
}

bool astrolabe_audio_pipeline_speech_active(const astrolabe_audio_pipeline_t *p)
{
    return p != NULL && p->speech_active;
}

bool astrolabe_audio_pipeline_manual_capture_pending(const astrolabe_audio_pipeline_t *p)
{
    return p != NULL && p->manual_capture;
}

bool astrolabe_audio_pipeline_manual_capture_active(const astrolabe_audio_pipeline_t *p)
{
    return p != NULL && p->manual_capture_active;
}

uint32_t astrolabe_audio_pipeline_last_rms(const astrolabe_audio_pipeline_t *p)
{
    return p != NULL ? p->last_rms : 0;
}

uint32_t astrolabe_audio_pipeline_noise_rms(const astrolabe_audio_pipeline_t *p)
{
    return p != NULL ? p->noise_rms : 0;
}

uint32_t astrolabe_audio_pipeline_start_threshold(const astrolabe_audio_pipeline_t *p)
{
    return p != NULL ? dynamic_start_threshold(p) : 0;
}

size_t astrolabe_audio_pipeline_capture_bytes(const astrolabe_audio_pipeline_t *p)
{
    return p != NULL ? p->capture_len_bytes : 0;
}

UBaseType_t astrolabe_audio_pipeline_queued_segments(const astrolabe_audio_pipeline_t *p)
{
    return p != NULL && p->utterance_queue != NULL ? uxQueueMessagesWaiting(p->utterance_queue) : 0;
}

uint32_t astrolabe_audio_pipeline_turn_segments(const astrolabe_audio_pipeline_t *p)
{
    return p != NULL ? p->turn_segment_count : 0;
}

uint32_t astrolabe_audio_pipeline_read_ok_count(const astrolabe_audio_pipeline_t *p)
{
    return p != NULL ? p->read_ok_count : 0;
}

uint32_t astrolabe_audio_pipeline_read_zero_count(const astrolabe_audio_pipeline_t *p)
{
    return p != NULL ? p->read_zero_count : 0;
}

uint32_t astrolabe_audio_pipeline_read_err_count(const astrolabe_audio_pipeline_t *p)
{
    return p != NULL ? p->read_err_count : 0;
}

esp_err_t astrolabe_audio_pipeline_last_read_err(const astrolabe_audio_pipeline_t *p)
{
    return p != NULL ? p->last_read_err : ESP_ERR_INVALID_STATE;
}

TaskHandle_t astrolabe_audio_pipeline_listen_task_handle(const astrolabe_audio_pipeline_t *p)
{
    return p != NULL ? p->listen_task : NULL;
}

TaskHandle_t astrolabe_audio_pipeline_voice_task_handle(const astrolabe_audio_pipeline_t *p)
{
    return p != NULL ? p->voice_task : NULL;
}

uint32_t astrolabe_audio_pipeline_listen_stack_bytes(const astrolabe_audio_pipeline_t *p)
{
    if (p == NULL) {
        return DEFAULT_LISTEN_STACK;
    }
    return p->cfg.listen_stack != 0 ? p->cfg.listen_stack : DEFAULT_LISTEN_STACK;
}

uint32_t astrolabe_audio_pipeline_voice_stack_bytes(const astrolabe_audio_pipeline_t *p)
{
    if (p == NULL) {
        return DEFAULT_VOICE_STACK;
    }
    return p->cfg.voice_stack != 0 ? p->cfg.voice_stack : DEFAULT_VOICE_STACK;
}

bool astrolabe_audio_pipeline_unhealthy(const astrolabe_audio_pipeline_t *p)
{
    return p != NULL && p->unhealthy;
}

void astrolabe_audio_pipeline_waveform_copy(const astrolabe_audio_pipeline_t *p, uint8_t *out, size_t len)
{
    if (out == NULL || len == 0) {
        return;
    }
    memset(out, 0, len);
    if (p == NULL) {
        return;
    }
    const size_t copy_len = len < ASTROLABE_AUDIO_PIPELINE_WAVEFORM_LEN ? len : ASTROLABE_AUDIO_PIPELINE_WAVEFORM_LEN;
    portENTER_CRITICAL((portMUX_TYPE *)&p->waveform_mux);
    const uint16_t head = p->waveform_head;
    for (size_t i = 0; i < copy_len; ++i) {
        out[i] = p->waveform[(head + i) % ASTROLABE_AUDIO_PIPELINE_WAVEFORM_LEN];
    }
    portEXIT_CRITICAL((portMUX_TYPE *)&p->waveform_mux);
}

void astrolabe_audio_pipeline_waveform_stream_copy(const astrolabe_audio_pipeline_t *p, uint8_t *out, size_t len)
{
    if (out == NULL || len == 0) {
        return;
    }
    memset(out, 0, len);
    if (p == NULL) {
        return;
    }
    const size_t copy_len = len < ASTROLABE_AUDIO_PIPELINE_WAVEFORM_LEN ? len : ASTROLABE_AUDIO_PIPELINE_WAVEFORM_LEN;
    portENTER_CRITICAL((portMUX_TYPE *)&p->waveform_mux);
    const uint16_t head = p->waveform_head;
    for (size_t i = 0; i < copy_len; ++i) {
        out[i] = p->waveform_stream[(head + i) % ASTROLABE_AUDIO_PIPELINE_WAVEFORM_LEN];
    }
    portEXIT_CRITICAL((portMUX_TYPE *)&p->waveform_mux);
}
