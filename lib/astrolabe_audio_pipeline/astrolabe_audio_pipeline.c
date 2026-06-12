#include "astrolabe_audio_pipeline.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
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
#define MAX_RING_SLOTS 8u
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
#define STREAM_PCM_CHUNK_BYTES 2048
#define STREAM_CONNECT_TIMEOUT_MS 5000
#define STREAM_SEND_TIMEOUT_MS 5000
#define STREAM_FRAME_PACE_MS 0
#define STREAM_RESPONSE_TIMEOUT_MS 120000
#define STREAM_EARLY_RESPONSE_TIMEOUT_MS 8000
#define STREAM_WEBSOCKET_TASK_STACK 5120u
#define MANUAL_CAPTURE_MIN_MS 1500u
#define MANUAL_CAPTURE_DEFAULT_HOLD_MS 2500u
#define VAD_WARMUP_FRAMES 25u
#define VAD_NOISE_ATTACK_SHIFT 6
#define VAD_NOISE_RELEASE_SHIFT 4
#define VAD_MIN_DELTA_RMS 500u
#define VAD_REARM_QUIET_FRAMES 75u
#define VAD_REARM_FORCE_MS 6000u
#define VAD_SILENCE_LEAK_FRAMES 4u

typedef struct {
    char path[128];
    size_t byte_count;
    uint32_t sequence;
    uint32_t slot;
    bool final_segment;
} utterance_t;

typedef struct voice_result voice_result_t;
typedef struct stream_response_ctx stream_response_ctx_t;

struct astrolabe_audio_pipeline {
    astrolabe_audio_pipeline_config_t cfg;
    QueueHandle_t utterance_queue;
    TaskHandle_t listen_task;
    TaskHandle_t voice_task;
    FILE *capture_file;
    char capture_path[128];
    char capture_base_path[128];
    size_t capture_cap_bytes;
    size_t segment_cap_bytes;
    size_t capture_len_bytes;
    uint32_t capture_slot;
    uint32_t capture_sequence;
    uint32_t capture_ring_slots;
    uint32_t capture_started_ms;
    uint32_t manual_capture_hold_ms;
    uint32_t manual_capture_deadline_ms;
    bool capture_slot_busy[MAX_RING_SLOTS];
    bool speech_active;
    volatile bool manual_capture;
    bool manual_capture_active;
    uint32_t silence_frames;
    uint32_t speech_frames;
    uint32_t last_rms;
    uint32_t noise_rms;
    uint32_t active_peak_rms;
    uint32_t vad_frames_seen;
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
    uint8_t *mp3;
    size_t mp3_len;
};

struct stream_response_ctx {
    SemaphoreHandle_t done;
    voice_result_t *result;
    char *message;
    size_t message_cap;
    size_t message_len;
    esp_err_t err;
    bool response_done;
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
static esp_err_t websocket_send_text_all(esp_websocket_client_handle_t client, const char *text);
static void stream_response_event(void *handler_arg, esp_event_base_t base, int32_t event_id, void *event_data);

static uint32_t cfg_stt_sample_rate_hz(const astrolabe_audio_pipeline_t *p)
{
    if (p != NULL && p->cfg.stt_sample_rate_hz > 0) {
        return p->cfg.stt_sample_rate_hz;
    }
    return p != NULL && p->cfg.sample_rate_hz > 0 ? p->cfg.sample_rate_hz : DEFAULT_SAMPLE_RATE_HZ;
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
           p->cfg.stream_url != NULL && p->cfg.stream_url[0] != '\0';
}

static void prepare_context(astrolabe_audio_pipeline_t *p)
{
    if (p != NULL && p->cfg.prepare_context != NULL) {
        p->cfg.prepare_context(p->cfg.event_user);
    }
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

static esp_err_t start_listen_task_if_needed(astrolabe_audio_pipeline_t *p)
{
    if (p == NULL || !p->running) {
        return ESP_ERR_INVALID_STATE;
    }
    if (p->listen_task != NULL) {
        return ESP_OK;
    }
    p->listen_should_run = true;
    BaseType_t ok = xTaskCreate(listen_task, "ast_audio_listen",
                                p->cfg.listen_stack ? p->cfg.listen_stack : DEFAULT_LISTEN_STACK, p,
                                p->cfg.listen_priority ? p->cfg.listen_priority : 5, &p->listen_task);
    if (ok != pdPASS) {
        p->listen_should_run = false;
        p->listen_task = NULL;
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
    snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
    const char *start = strstr(body, pattern);
    if (start == NULL) {
        out[0] = '\0';
        return NULL;
    }
    start += strlen(pattern);
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
    if (face == NULL || slug == NULL || name == NULL || system == NULL || history == NULL ||
        interaction == NULL || commonplace == NULL || response_format == NULL) {
        free(face);
        free(slug);
        free(name);
        free(system);
        free(history);
        free(interaction);
        free(commonplace);
        free(response_format);
        return ESP_ERR_NO_MEM;
    }

    const size_t meta_cap = strlen(face) + strlen(slug) + strlen(name) + strlen(history) + strlen(system) +
                            strlen(interaction) + strlen(commonplace) + strlen(response_format) + 512;
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
        return ESP_ERR_NO_MEM;
    }
    int meta_len = snprintf(meta, meta_cap,
             "{\"languageCode\":\"en-US\",\"sampleRateHertz\":%u,"
             "\"face\":\"%s\",\"facultySlug\":\"%s\",\"facultyName\":\"%s\","
             "\"conversationHistory\":\"%s\",\"systemInstruction\":\"%s\","
             "\"interactionMode\":\"%s\",\"commonplaceMode\":\"%s\","
             "\"responseFormat\":\"%s\",\"skipLlm\":%s,\"logToCommonplace\":%s}",
             (unsigned)cfg_stt_sample_rate_hz(p), face, slug, name, history, system,
             interaction, commonplace, response_format,
             cfg_skip_llm(p) ? "true" : "false",
             cfg_log_to_commonplace(p) ? "true" : "false");
    free(face);
    free(slug);
    free(name);
    free(system);
    free(history);
    free(interaction);
    free(commonplace);
    free(response_format);
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
    if (strstr(json, "\"type\":\"conversation.item.input_audio_transcription.completed\"") != NULL) {
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
                if (!append_bytes(&ctx->result->mp3, &ctx->result->mp3_len, mp3, mp3_len)) {
                    ctx->err = ESP_ERR_NO_MEM;
                    if (ctx->done != NULL) {
                        xSemaphoreGive(ctx->done);
                    }
                }
                free(mp3);
            }
            free(audio_b64);
        }
    } else if (strstr(json, "\"type\":\"response.done\"") != NULL) {
        json_find_string(json, "facultySlug", ctx->result->faculty_slug, sizeof(ctx->result->faculty_slug));
        json_find_string(json, "facultyName", ctx->result->faculty_name, sizeof(ctx->result->faculty_name));
        ctx->response_done = true;
        if (ctx->done != NULL) {
            xSemaphoreGive(ctx->done);
        }
    } else if (strstr(json, "\"type\":\"error\"") != NULL) {
        char code[96] = "";
        char message[192] = "";
        (void)json_find_string(json, "code", code, sizeof(code));
        (void)json_find_string(json, "message", message, sizeof(message));
        ESP_LOGW(TAG, "voice-stream error code=%s message=%s",
                 code[0] != '\0' ? code : "?",
                 message[0] != '\0' ? message : "?");
        ctx->err = ESP_FAIL;
        if (ctx->done != NULL) {
            xSemaphoreGive(ctx->done);
        }
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
        return;
    }
    if (event_id == WEBSOCKET_EVENT_CLOSED) {
        ESP_LOGW(TAG, "voice-stream websocket closed");
        return;
    }
    if (event_id == WEBSOCKET_EVENT_ERROR) {
        ESP_LOGW(TAG, "voice-stream websocket error");
        ctx->err = ESP_FAIL;
        if (ctx->done != NULL) {
            xSemaphoreGive(ctx->done);
        }
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
            ctx->message = malloc(payload_len + 1);
        }
        ctx->message_cap = ctx->message != NULL ? payload_len + 1 : 0;
        ctx->message_len = 0;
    }
    if (ctx->message == NULL || payload_off + (size_t)data->data_len > ctx->message_cap - 1) {
        ctx->err = ESP_ERR_NO_MEM;
        if (ctx->done != NULL) {
            xSemaphoreGive(ctx->done);
        }
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

static void rolling_stream_session_close(astrolabe_audio_pipeline_t *p)
{
    if (p == NULL) {
        return;
    }
    if (p->rolling_client != NULL) {
        esp_websocket_client_close(p->rolling_client, pdMS_TO_TICKS(1000));
        esp_websocket_client_destroy(p->rolling_client);
        p->rolling_client = NULL;
    }
    if (p->rolling_response != NULL) {
        if (p->rolling_response->done != NULL) {
            vSemaphoreDelete(p->rolling_response->done);
            p->rolling_response->done = NULL;
        }
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
    if (p->rolling_client != NULL && p->rolling_response != NULL && p->rolling_result != NULL) {
        return ESP_OK;
    }

    rolling_stream_session_close(p);

    char *face = json_escape_alloc(p->cfg.face);
    char *slug = json_escape_alloc(p->cfg.faculty_slug);
    char *name = json_escape_alloc(p->cfg.faculty_name);
    char *system = json_escape_alloc(p->cfg.system_instruction);
    char *history = json_escape_alloc(p->cfg.history);
    char *interaction = json_escape_alloc(cfg_interaction_mode(p));
    char *commonplace = json_escape_alloc(cfg_commonplace_mode(p));
    char *response_format = json_escape_alloc(cfg_response_format(p));
    if (face == NULL || slug == NULL || name == NULL || system == NULL || history == NULL ||
        interaction == NULL || commonplace == NULL || response_format == NULL) {
        free(face);
        free(slug);
        free(name);
        free(system);
        free(history);
        free(interaction);
        free(commonplace);
        free(response_format);
        return ESP_ERR_NO_MEM;
    }

    char auth_header[560];
    auth_header[0] = '\0';
    if (p->cfg.api_key != NULL && p->cfg.api_key[0] != '\0') {
        snprintf(auth_header, sizeof(auth_header), "apikey: %s\r\nAuthorization: Bearer %s\r\n",
                 p->cfg.api_key, p->cfg.api_key);
    }
    esp_websocket_client_config_t ws_cfg = {
        .uri = p->cfg.stream_url,
        .headers = auth_header[0] != '\0' ? auth_header : NULL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .disable_auto_reconnect = true,
        .task_name = "voice_ws",
        .task_stack = STREAM_WEBSOCKET_TASK_STACK,
        .network_timeout_ms = STREAM_SEND_TIMEOUT_MS,
    };
    p->rolling_client = esp_websocket_client_init(&ws_cfg);
    if (p->rolling_client == NULL) {
        free(face);
        free(slug);
        free(name);
        free(system);
        free(history);
        free(interaction);
        free(commonplace);
        free(response_format);
        return ESP_FAIL;
    }

    p->rolling_result = calloc(1, sizeof(*p->rolling_result));
    p->rolling_response = calloc(1, sizeof(*p->rolling_response));
    if (p->rolling_result == NULL || p->rolling_response == NULL) {
        free(face);
        free(slug);
        free(name);
        free(system);
        free(history);
        free(interaction);
        free(commonplace);
        free(response_format);
        rolling_stream_session_close(p);
        return ESP_ERR_NO_MEM;
    }
    p->rolling_response->result = p->rolling_result;
    p->rolling_response->done = xSemaphoreCreateBinary();
    if (p->rolling_response->done == NULL) {
        free(face);
        free(slug);
        free(name);
        free(system);
        free(history);
        free(interaction);
        free(commonplace);
        free(response_format);
        rolling_stream_session_close(p);
        return ESP_ERR_NO_MEM;
    }
    (void)esp_websocket_register_events(p->rolling_client, WEBSOCKET_EVENT_ANY, stream_response_event, p->rolling_response);

    const uint32_t t0 = ticks_ms();
    esp_err_t err = esp_websocket_client_start(p->rolling_client);
    while (err == ESP_OK && !esp_websocket_client_is_connected(p->rolling_client) &&
           ticks_ms() - t0 < STREAM_CONNECT_TIMEOUT_MS) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (err == ESP_OK && !esp_websocket_client_is_connected(p->rolling_client)) {
        err = ESP_ERR_TIMEOUT;
    }

    const size_t session_cap = strlen(face) + strlen(slug) + strlen(name) + strlen(system) + strlen(history) +
                               strlen(interaction) + strlen(commonplace) + strlen(response_format) + 640;
    char *session = heap_caps_malloc(session_cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (session == NULL) {
        session = malloc(session_cap);
    }
    if (err == ESP_OK && session == NULL) {
        err = ESP_ERR_NO_MEM;
    }
    if (err == ESP_OK) {
        int session_len = snprintf(session, session_cap,
                                   "{\"type\":\"session.update\",\"session\":{\"sampleRateHertz\":%u,"
                                   "\"sample_width_bits\":16,\"channels\":1,\"encoding\":\"pcm16\","
                                   "\"face\":\"%s\",\"facultySlug\":\"%s\",\"facultyName\":\"%s\","
                                   "\"conversationHistory\":\"%s\",\"systemInstruction\":\"%s\","
                                   "\"interactionMode\":\"%s\",\"commonplaceMode\":\"%s\","
                                   "\"responseFormat\":\"%s\",\"skipLlm\":%s,\"logToCommonplace\":%s}}",
                                   (unsigned)cfg_stt_sample_rate_hz(p), face, slug, name, history, system,
                                   interaction, commonplace, response_format,
                                   cfg_skip_llm(p) ? "true" : "false",
                                   cfg_log_to_commonplace(p) ? "true" : "false");
        if (session_len <= 0 || (size_t)session_len >= session_cap) {
            err = ESP_ERR_NO_MEM;
        } else {
            err = websocket_send_text_all(p->rolling_client, session);
        }
    }

    free(session);
    free(face);
    free(slug);
    free(name);
    free(system);
    free(history);
    free(interaction);
    free(commonplace);
    free(response_format);

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
    if (err != ESP_OK) {
        return err;
    }

    FILE *file = NULL;
    uint8_t *packet = NULL;
    file = fopen(utt->path, "rb");
    if (file == NULL) {
        rolling_stream_session_close(p);
        return ESP_FAIL;
    }
    packet = heap_caps_malloc(STREAM_PCM_CHUNK_BYTES + 9, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (packet == NULL) {
        packet = malloc(STREAM_PCM_CHUNK_BYTES + 9);
    }
    if (packet == NULL) {
        fclose(file);
        rolling_stream_session_close(p);
        return ESP_ERR_NO_MEM;
    }
    size_t sent_bytes = 0;
    while (err == ESP_OK && sent_bytes < utt->byte_count) {
        const size_t want = (utt->byte_count - sent_bytes) > STREAM_PCM_CHUNK_BYTES
                                ? STREAM_PCM_CHUNK_BYTES
                                : (utt->byte_count - sent_bytes);
        const size_t got = fread(packet + 9, 1, want, file);
        if (got == 0) {
            err = ESP_FAIL;
            break;
        }
        const uint32_t capture_ms =
            (uint32_t)(((sent_bytes / sizeof(int16_t)) * 1000u) / cfg_stt_sample_rate_hz(p));
        packet[0] = 0xa1u;
        put_u32_le(packet + 1, utt->sequence);
        put_u32_le(packet + 5, capture_ms);
        err = websocket_send_binary_all(p->rolling_client, packet, got + 9);
        sent_bytes += got;
        if (err == ESP_OK && STREAM_FRAME_PACE_MS > 0) {
            vTaskDelay(pdMS_TO_TICKS(STREAM_FRAME_PACE_MS));
        }
    }
    free(packet);
    if (file != NULL) {
        fclose(file);
    }
    if (err == ESP_OK && p->rolling_response != NULL) {
        while (xSemaphoreTake(p->rolling_response->done, 0) == pdTRUE) {
        }
        p->rolling_response->err = ESP_OK;
        p->rolling_response->response_done = false;
    }
    if (err == ESP_OK) {
        char commit[160];
        snprintf(commit, sizeof(commit),
                 "{\"type\":\"input_audio_buffer.commit\",\"turnId\":\"segment-%u\",\"final\":%s,\"bytes\":%u}",
                 (unsigned)utt->sequence, utt->final_segment ? "true" : "false", (unsigned)utt->byte_count);
        err = websocket_send_text_all(p->rolling_client, commit);
    }
    if (err == ESP_OK && p->rolling_response != NULL) {
        const uint32_t timeout_ms = utt->final_segment ? STREAM_RESPONSE_TIMEOUT_MS : STREAM_EARLY_RESPONSE_TIMEOUT_MS;
        if (utt->final_segment) {
            if (xSemaphoreTake(p->rolling_response->done, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
                err = ESP_ERR_TIMEOUT;
            } else if (p->rolling_response->err != ESP_OK) {
                err = p->rolling_response->err;
            } else if (!p->rolling_response->response_done) {
                err = ESP_FAIL;
            } else if (p->rolling_result != NULL && p->rolling_result->transcript[0] == '\0' &&
                       p->rolling_result->reply[0] == '\0' && p->rolling_result->mp3_len == 0) {
                err = ESP_FAIL;
            }
        } else {
            if (xSemaphoreTake(p->rolling_response->done, pdMS_TO_TICKS(timeout_ms)) == pdTRUE &&
                p->rolling_response->err != ESP_OK) {
                err = p->rolling_response->err;
            }
        }
    }

    if (err == ESP_OK && result != NULL && p->rolling_result != NULL) {
        rolling_stream_copy_snapshot(p->rolling_result, result, utt->final_segment);
        if (utt->final_segment) {
            p->rolling_result->mp3 = NULL;
            p->rolling_result->mp3_len = 0;
        }
    }

    if (err != ESP_OK || utt->final_segment) {
        rolling_stream_session_close(p);
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
        const size_t out_samples = (size_t)samples * (size_t)(info.channels > 0 ? info.channels : 1);
        (void)p->cfg.io.write(pcm, out_samples, 1000, p->cfg.io.user);
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
    if (p->capture_file != NULL) {
        fclose(p->capture_file);
        p->capture_file = NULL;
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
    remove(dropped.path);
    if (dropped.slot < p->capture_ring_slots) {
        p->capture_slot_busy[dropped.slot] = false;
    }
    ESP_LOGW(TAG, "capture ring dropped queued segment #%u slot=%u bytes=%u",
             (unsigned)dropped.sequence, (unsigned)dropped.slot, (unsigned)dropped.byte_count);
    return true;
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
        const uint32_t adaptive = p->noise_rms + (p->noise_rms >> 4) + VAD_MIN_DELTA_RMS;
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
        const uint32_t adaptive = p->noise_rms + (p->noise_rms >> 1) + VAD_MIN_DELTA_RMS;
        if (adaptive > threshold) {
            threshold = adaptive;
        }
    }
    return threshold;
}

static bool begin_capture_file(astrolabe_audio_pipeline_t *p)
{
    if (p->capture_file != NULL) {
        fclose(p->capture_file);
        p->capture_file = NULL;
    }
    if (!advance_capture_slot(p)) {
        emit(p, ASTROLABE_AUDIO_PIPELINE_EVENT_ERROR, "capture ring full");
        return false;
    }
    remove(p->capture_path);
    p->capture_file = fopen(p->capture_path, "wb");
    if (p->capture_file != NULL) {
        setvbuf(p->capture_file, NULL, _IONBF, 0);
    }
    p->capture_len_bytes = 0;
    p->capture_started_ms = ticks_ms();
    return p->capture_file != NULL;
}

static bool queue_utterance(astrolabe_audio_pipeline_t *p, bool final_segment)
{
    if (p->capture_file != NULL) {
        fflush(p->capture_file);
        fclose(p->capture_file);
        p->capture_file = NULL;
    }
    const size_t min_bytes = (((size_t)p->cfg.min_ms * cfg_stt_sample_rate_hz(p)) / 1000) * sizeof(int16_t);
    if (p->capture_len_bytes < min_bytes) {
        remove(p->capture_path);
        reset_capture(p);
        return false;
    }
    utterance_t utt = {
        .byte_count = p->capture_len_bytes,
        .sequence = p->capture_sequence++,
        .slot = p->capture_slot,
        .final_segment = final_segment,
    };
    snprintf(utt.path, sizeof(utt.path), "%s", p->capture_path);
    if (xQueueSend(p->utterance_queue, &utt, 0) != pdTRUE) {
        remove(p->capture_path);
        p->capture_slot_busy[p->capture_slot] = false;
        reset_capture(p);
        return false;
    }
    p->capture_slot_busy[p->capture_slot] = true;
    if (final_segment) {
        p->manual_capture_active = false;
        p->manual_capture_deadline_ms = 0;
    }
    emit(p, ASTROLABE_AUDIO_PIPELINE_EVENT_CAPTURE_QUEUED, NULL);
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
    prepare_context(p);
    p->last_rms = frame_rms(frame, frame_samples);
    p->vad_frames_seen++;
    waveform_push(p, pipeline_wave_level(p, p->last_rms), p->speech_active);
    if (p->manual_capture && !p->speech_active) {
        p->manual_capture = false;
        p->capture_blocked_until_ms = 0;
        p->capture_needs_quiet = false;
        p->rearm_quiet_frames = 0;
        p->speech_frames = 0;
        if (!begin_capture_file(p)) {
            emit(p, ASTROLABE_AUDIO_PIPELINE_EVENT_ERROR, "capture file");
            reset_capture(p);
            return false;
        }
        p->speech_active = true;
        p->manual_capture_active = true;
        p->active_peak_rms = p->last_rms;
        p->silence_frames = 0;
        const uint32_t manual_hold_ms =
            p->manual_capture_hold_ms >= MANUAL_CAPTURE_MIN_MS ? p->manual_capture_hold_ms : MANUAL_CAPTURE_MIN_MS;
        p->manual_capture_deadline_ms = ticks_ms() + manual_hold_ms;
        ESP_LOGI(TAG, "manual capture start rms=%u noise=%u",
                 (unsigned)p->last_rms,
                 (unsigned)p->noise_rms);
        emit(p, ASTROLABE_AUDIO_PIPELINE_EVENT_CAPTURE_START, "button");
    } else if (p->manual_capture) {
        p->manual_capture = false;
    }
    if (!p->speech_active && p->vad_frames_seen <= VAD_WARMUP_FRAMES) {
        update_noise_floor(p, p->last_rms);
        return false;
    }
    const uint32_t now_ms = ticks_ms();
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
            p->speech_active = true;
            p->manual_capture_active = false;
            p->active_peak_rms = p->last_rms;
            p->silence_frames = 0;
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
    if (p->capture_len_bytes + frame_bytes > p->capture_cap_bytes) {
        return queue_utterance(p, true);
    }
    if (rolling_websocket_enabled(p) && p->capture_len_bytes + frame_bytes > p->segment_cap_bytes) {
        if (!rotate_capture_segment(p)) {
            return false;
        }
    }
    const size_t wrote = p->capture_file != NULL ? fwrite(frame, 1, frame_bytes, p->capture_file) : 0;
    if (wrote != frame_bytes) {
        ESP_LOGE(TAG, "capture write failed path=%s len=%u frame=%u wrote=%u errno=%d",
                 p->capture_path, (unsigned)p->capture_len_bytes, (unsigned)frame_bytes, (unsigned)wrote, errno);
        emit(p, ASTROLABE_AUDIO_PIPELINE_EVENT_ERROR, "capture write");
        remove(p->capture_path);
        reset_capture(p);
        return false;
    }
    p->capture_len_bytes += frame_bytes;
    const uint32_t manual_hold_ms =
        p->manual_capture_hold_ms >= MANUAL_CAPTURE_MIN_MS ? p->manual_capture_hold_ms : MANUAL_CAPTURE_MIN_MS;
    if (p->manual_capture_active && ticks_reached(now_ms, p->manual_capture_deadline_ms)) {
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
            return false;
        }
        update_noise_floor(p, p->last_rms);
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
    int16_t *frame = heap_caps_malloc(p->cfg.frame_samples * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
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
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        (void)push_frame(p, frame, got);
    }
    free(frame);
    p->listen_task = NULL;
    vTaskDelete(NULL);
}

static void voice_task(void *arg)
{
    astrolabe_audio_pipeline_t *p = (astrolabe_audio_pipeline_t *)arg;
    while (p->running) {
        utterance_t utt = {};
        if (xQueueReceive(p->utterance_queue, &utt, pdMS_TO_TICKS(250)) != pdTRUE) {
            continue;
        }
        if (utt.final_segment) {
            stop_listen_task_if_running(p, 1000);
            emit(p, ASTROLABE_AUDIO_PIPELINE_EVENT_THINKING, NULL);
        }
        prepare_context(p);
        ESP_LOGI(TAG, "posting capture segment #%u final=%s bytes=%u path=%s",
                 (unsigned)utt.sequence, utt.final_segment ? "yes" : "no",
                 (unsigned)utt.byte_count, utt.path);
        voice_result_t result = {};
        esp_err_t err = ESP_OK;
        if (rolling_websocket_enabled(p)) {
            err = stream_pcm_file(p, &utt, &result);
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
        } else {
            err = post_pcm_file(p, utt.path, utt.byte_count, &result);
        }
        remove(utt.path);
        if (utt.slot < p->capture_ring_slots) {
            p->capture_slot_busy[utt.slot] = false;
        }
        if (err != ESP_OK) {
            emit(p, ASTROLABE_AUDIO_PIPELINE_EVENT_ERROR, "voice-pipeline");
            start_capture_cooldown(p);
            if (start_listen_task_if_needed(p) == ESP_OK) {
                emit(p, ASTROLABE_AUDIO_PIPELINE_EVENT_LISTENING, NULL);
            } else {
                emit(p, ASTROLABE_AUDIO_PIPELINE_EVENT_ERROR, "listen restart");
            }
            continue;
        }
        if (!utt.final_segment) {
            if (p->cfg.on_result != NULL &&
                (result.faculty_slug[0] != '\0' || result.faculty_name[0] != '\0')) {
                p->cfg.on_result(result.transcript, "", result.faculty_slug, result.faculty_name, p->cfg.event_user);
            }
            if (result.transcript[0] != '\0') {
                ESP_LOGI(TAG, "early transcript segment #%u: %s", (unsigned)utt.sequence, result.transcript);
            }
            voice_result_free(&result);
            continue;
        }
        if (result.transcript[0] != '\0') {
            emit(p, ASTROLABE_AUDIO_PIPELINE_EVENT_TRANSCRIPT, result.transcript);
        }
        if (result.reply[0] != '\0') {
            emit(p, ASTROLABE_AUDIO_PIPELINE_EVENT_REPLY, result.reply);
        }
        if (p->cfg.on_result != NULL) {
            p->cfg.on_result(result.transcript, result.reply, result.faculty_slug, result.faculty_name, p->cfg.event_user);
        }
        if (result.mp3 != NULL && result.mp3_len > 0) {
            emit(p, ASTROLABE_AUDIO_PIPELINE_EVENT_SPEAKING,
                 result.faculty_name[0] != '\0' ? result.faculty_name : p->cfg.faculty_name);
            (void)play_mp3(p, result.mp3, result.mp3_len);
        }
        voice_result_free(&result);
        emit(p, ASTROLABE_AUDIO_PIPELINE_EVENT_TURN_DONE, NULL);
        start_capture_cooldown(p);
        if (start_listen_task_if_needed(p) == ESP_OK) {
            emit(p, ASTROLABE_AUDIO_PIPELINE_EVENT_LISTENING, NULL);
        } else {
            emit(p, ASTROLABE_AUDIO_PIPELINE_EVENT_ERROR, "listen restart");
        }
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
    astrolabe_audio_pipeline_t *p = calloc(1, sizeof(*p));
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
    if (p->cfg.max_seconds == 0) {
        p->cfg.max_seconds = DEFAULT_MAX_SECONDS;
    }
    if (p->cfg.min_ms == 0) {
        p->cfg.min_ms = DEFAULT_MIN_MS;
    }
    if (p->cfg.capture_cooldown_ms == 0) {
        p->cfg.capture_cooldown_ms = DEFAULT_CAPTURE_COOLDOWN_MS;
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
    p->capture_cap_bytes = (size_t)p->cfg.max_seconds * cfg_stt_sample_rate_hz(p) * sizeof(int16_t);
    if (rolling_websocket_enabled(p)) {
        const uint32_t segment_ms = p->cfg.capture_segment_ms != 0 ? p->cfg.capture_segment_ms : DEFAULT_SEGMENT_MS;
        p->segment_cap_bytes = (((size_t)segment_ms * cfg_stt_sample_rate_hz(p)) / 1000) * sizeof(int16_t);
    } else {
        p->segment_cap_bytes = p->capture_cap_bytes;
    }
    if (p->segment_cap_bytes == 0 || p->segment_cap_bytes > p->capture_cap_bytes) {
        p->segment_cap_bytes = p->capture_cap_bytes;
    }
    if (!p->cfg.capture_skip_spiffs_mount) {
        esp_vfs_spiffs_conf_t spiffs = {
            .base_path = p->cfg.capture_mount_path != NULL ? p->cfg.capture_mount_path : DEFAULT_CAPTURE_MOUNT_PATH,
            .partition_label = p->cfg.capture_partition_label,
            .max_files = (int)(p->capture_ring_slots + 2),
            .format_if_mount_failed = true,
        };
        esp_err_t mount_err = esp_vfs_spiffs_register(&spiffs);
        if (mount_err == ESP_OK) {
            p->spiffs_mounted = true;
        } else if (mount_err != ESP_ERR_INVALID_STATE) {
            astrolabe_audio_pipeline_destroy(p);
            return mount_err;
        }
    }
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
        ESP_LOGI(TAG, "capture spool spiffs total=%u used=%u free=%u slots=%u segment=%uB",
                 (unsigned)spiffs_total, (unsigned)spiffs_used, (unsigned)(spiffs_total - spiffs_used),
                 (unsigned)p->capture_ring_slots, (unsigned)p->segment_cap_bytes);
    }
    p->utterance_queue = xQueueCreate(p->capture_ring_slots, sizeof(utterance_t));
    if (p->utterance_queue == NULL) {
        astrolabe_audio_pipeline_destroy(p);
        return ESP_ERR_NO_MEM;
    }
    p->waveform_mux = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    reset_capture(p);
    *out_pipeline = p;
    return ESP_OK;
}

esp_err_t astrolabe_audio_pipeline_start(astrolabe_audio_pipeline_t *p)
{
    if (p == NULL || p->running) {
        return ESP_ERR_INVALID_STATE;
    }
    p->running = true;
    p->listen_should_run = true;
    esp_err_t listen_err = start_listen_task_if_needed(p);
    if (listen_err != ESP_OK) {
        p->running = false;
        return listen_err;
    }
    BaseType_t ok;
    // Keep the voice worker stack in internal RAM. This task runs through
    // websocket/TLS and flash-cache-disabled paths where a PSRAM-backed stack
    // can trip cache-safety assertions on ESP32-S3.
    ok = xTaskCreate(voice_task, "ast_audio_voice",
                     p->cfg.voice_stack ? p->cfg.voice_stack : DEFAULT_VOICE_STACK, p,
                     p->cfg.voice_priority ? p->cfg.voice_priority : 4, &p->voice_task);
    if (ok != pdPASS) {
        stop_listen_task_if_running(p, 1000);
        p->running = false;
        return ESP_ERR_NO_MEM;
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
            remove(utt.path);
        }
        vQueueDelete(p->utterance_queue);
    }
    rolling_stream_session_close(p);
    reset_capture(p);
    for (uint32_t slot = 0; slot < p->capture_ring_slots; ++slot) {
        char path[128];
        make_capture_slot_path(p, slot, path, sizeof(path));
        remove(path);
    }
    remove(p->capture_path);
    if (p->spiffs_mounted) {
        esp_vfs_spiffs_unregister(p->cfg.capture_partition_label);
    }
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
    if (hold_ms < MANUAL_CAPTURE_MIN_MS) {
        hold_ms = MANUAL_CAPTURE_MIN_MS;
    } else if (hold_ms > 15000u) {
        hold_ms = 15000u;
    }
    p->manual_capture_hold_ms = hold_ms;
    p->manual_capture = true;
    p->capture_blocked_until_ms = 0;
    p->capture_needs_quiet = false;
    p->rearm_quiet_frames = 0;
    p->speech_frames = 0;
    return ESP_OK;
}

bool astrolabe_audio_pipeline_speech_active(const astrolabe_audio_pipeline_t *p)
{
    return p != NULL && p->speech_active;
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
