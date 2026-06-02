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
#include "freertos/queue.h"
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
#define DEFAULT_LISTEN_STACK 6144u
#define DEFAULT_VOICE_STACK 12288u
#define DEFAULT_CAPTURE_MOUNT_PATH "/spiffs"
#define DEFAULT_CAPTURE_FILE_PATH "/spiffs/astrolabe_utterance.pcm"
#define HTTP_TIMEOUT_MS 660000
#define VOICE_RESP_MAX_BYTES (768 * 1024)
#define JSON_SUFFIX_MAX_BYTES 384
#define POST_PCM_CHUNK_BYTES 1536
#define STREAM_PCM_CHUNK_BYTES 1536
#define STREAM_CONNECT_TIMEOUT_MS 5000
#define STREAM_SEND_TIMEOUT_MS 5000
#define STREAM_FRAME_PACE_MS 45
#define VAD_WARMUP_FRAMES 25u
#define VAD_NOISE_ATTACK_SHIFT 6
#define VAD_NOISE_RELEASE_SHIFT 4
#define VAD_MIN_DELTA_RMS 50000u
#define VAD_REARM_QUIET_FRAMES 75u
#define VAD_REARM_FORCE_MS 6000u

typedef struct {
    char path[128];
    size_t byte_count;
    uint32_t sequence;
    uint32_t slot;
    bool final_segment;
} utterance_t;

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
    bool capture_slot_busy[MAX_RING_SLOTS];
    bool speech_active;
    uint32_t silence_frames;
    uint32_t speech_frames;
    uint32_t last_rms;
    uint32_t noise_rms;
    uint32_t active_peak_rms;
    uint32_t vad_frames_seen;
    uint32_t rearm_quiet_frames;
    uint32_t capture_blocked_until_ms;
    bool capture_needs_quiet;
    bool running;
    bool spiffs_mounted;
};

typedef struct {
    char transcript[320];
    char reply[768];
    char faculty_slug[64];
    char faculty_name[96];
    uint8_t *mp3;
    size_t mp3_len;
} voice_result_t;

static uint32_t ticks_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static bool ticks_reached(uint32_t now_ms, uint32_t deadline_ms)
{
    return (int32_t)(now_ms - deadline_ms) >= 0;
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
    if (face == NULL || slug == NULL || name == NULL || system == NULL || history == NULL) {
        free(face);
        free(slug);
        free(name);
        free(system);
        free(history);
        return ESP_ERR_NO_MEM;
    }

    const size_t prefix_cap = strlen(face) + strlen(slug) + strlen(name) + strlen(history) + strlen(system) + 384;
    char *prefix = heap_caps_malloc(prefix_cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    char *suffix = heap_caps_malloc(JSON_SUFFIX_MAX_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (prefix == NULL) {
        prefix = malloc(prefix_cap);
    }
    if (suffix == NULL) {
        suffix = malloc(JSON_SUFFIX_MAX_BYTES);
    }
    if (prefix == NULL || suffix == NULL) {
        free(face);
        free(slug);
        free(name);
        free(system);
        free(history);
        free(prefix);
        free(suffix);
        return ESP_ERR_NO_MEM;
    }
    int prefix_len = snprintf(prefix, prefix_cap,
             "{\"languageCode\":\"en-US\",\"sampleRateHertz\":%u,"
             "\"face\":\"%s\",\"facultySlug\":\"%s\",\"facultyName\":\"%s\","
             "\"history\":\"%s\",\"systemInstruction\":\"%s\",\"audioBase64\":\"",
             (unsigned)p->cfg.sample_rate_hz, face, slug, name, history, system);
    int suffix_len = snprintf(suffix, JSON_SUFFIX_MAX_BYTES, "\"}");
    free(face);
    free(slug);
    free(name);
    free(system);
    free(history);
    if (prefix_len <= 0 || (size_t)prefix_len >= prefix_cap || suffix_len <= 0 ||
        (size_t)suffix_len >= JSON_SUFFIX_MAX_BYTES) {
        free(prefix);
        free(suffix);
        return ESP_ERR_NO_MEM;
    }

    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        free(prefix);
        free(suffix);
        return ESP_FAIL;
    }

    uint8_t *response = NULL;
    size_t response_len = 0;
    int status = 0;
    const uint32_t t0 = ticks_ms();
    const size_t b64_len = ((pcm_len + 2) / 3) * 4;
    const size_t body_len = (size_t)prefix_len + b64_len + (size_t)suffix_len;
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
        free(prefix);
        free(suffix);
        return ESP_FAIL;
    }
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Accept", "application/json,audio/mpeg");
    if (p->cfg.api_key != NULL && p->cfg.api_key[0] != '\0') {
        esp_http_client_set_header(client, "apikey", p->cfg.api_key);
        char auth[512];
        snprintf(auth, sizeof(auth), "Bearer %s", p->cfg.api_key);
        esp_http_client_set_header(client, "Authorization", auth);
    }

    esp_err_t err = esp_http_client_open(client, body_len);
    if (err == ESP_OK) {
        err = http_write_all(client, prefix, (size_t)prefix_len);
    }
    uint8_t raw[POST_PCM_CHUNK_BYTES];
    char b64[((POST_PCM_CHUNK_BYTES + 2) / 3) * 4 + 4];
    size_t remaining = pcm_len;
    while (err == ESP_OK && remaining > 0) {
        const size_t want = remaining > sizeof(raw) ? sizeof(raw) : remaining;
        const size_t got = fread(raw, 1, want, file);
        if (got == 0) {
            err = ESP_FAIL;
            break;
        }
        size_t encoded = 0;
        if (mbedtls_base64_encode((unsigned char *)b64, sizeof(b64), &encoded, raw, got) != 0) {
            err = ESP_FAIL;
            break;
        }
        err = http_write_all(client, b64, encoded);
        remaining -= got;
    }
    if (err == ESP_OK) {
        err = http_write_all(client, suffix, (size_t)suffix_len);
    }
    fclose(file);
    free(prefix);
    free(suffix);
    if (err == ESP_OK) {
        (void)esp_http_client_fetch_headers(client);
        status = esp_http_client_get_status_code(client);
        if (status < 200 || status >= 300) {
            err = ESP_FAIL;
        } else {
            err = read_response_body(client, &response, &response_len);
        }
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    ESP_LOGI(TAG, "voice-pipeline HTTP %d body=%uB in %ums", status, (unsigned)response_len,
             (unsigned)(ticks_ms() - t0));
    if (err != ESP_OK) {
        free(response);
        return err;
    }
    if (response_len >= 3 && response[0] == 0x49 && response[1] == 0x44 && response[2] == 0x33) {
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

static esp_err_t stream_pcm_file(astrolabe_audio_pipeline_t *p, const utterance_t *utt)
{
    if (p->cfg.stream_url == NULL || p->cfg.stream_url[0] == '\0') {
        return ESP_ERR_NOT_SUPPORTED;
    }

    char *face = json_escape_alloc(p->cfg.face);
    char *slug = json_escape_alloc(p->cfg.faculty_slug);
    char *name = json_escape_alloc(p->cfg.faculty_name);
    char *system = json_escape_alloc(p->cfg.system_instruction);
    char *history = json_escape_alloc(p->cfg.history);
    if (face == NULL || slug == NULL || name == NULL || system == NULL || history == NULL) {
        free(face);
        free(slug);
        free(name);
        free(system);
        free(history);
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
        .network_timeout_ms = STREAM_SEND_TIMEOUT_MS,
    };
    esp_websocket_client_handle_t client = esp_websocket_client_init(&ws_cfg);
    if (client == NULL) {
        free(face);
        free(slug);
        free(name);
        free(system);
        free(history);
        return ESP_FAIL;
    }

    const uint32_t t0 = ticks_ms();
    esp_err_t err = esp_websocket_client_start(client);
    while (err == ESP_OK && !esp_websocket_client_is_connected(client) &&
           ticks_ms() - t0 < STREAM_CONNECT_TIMEOUT_MS) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (err == ESP_OK && !esp_websocket_client_is_connected(client)) {
        err = ESP_ERR_TIMEOUT;
    }

    const size_t session_cap = strlen(face) + strlen(slug) + strlen(name) + strlen(system) + strlen(history) + 512;
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
                                   "\"conversationHistory\":\"%s\",\"systemInstruction\":\"%s\"}}",
                                   (unsigned)p->cfg.sample_rate_hz, face, slug, name, history, system);
        if (session_len <= 0 || (size_t)session_len >= session_cap) {
            err = ESP_ERR_NO_MEM;
        } else {
            err = websocket_send_text_all(client, session);
        }
    }
    free(session);
    free(face);
    free(slug);
    free(name);
    free(system);
    free(history);

    FILE *file = NULL;
    uint8_t *packet = NULL;
    if (err == ESP_OK) {
        file = fopen(utt->path, "rb");
        if (file == NULL) {
            err = ESP_FAIL;
        }
    }
    if (err == ESP_OK) {
        packet = heap_caps_malloc(STREAM_PCM_CHUNK_BYTES + 9, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (packet == NULL) {
            packet = malloc(STREAM_PCM_CHUNK_BYTES + 9);
        }
        if (packet == NULL) {
            err = ESP_ERR_NO_MEM;
        }
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
            (uint32_t)(((sent_bytes / sizeof(int16_t)) * 1000u) / p->cfg.sample_rate_hz);
        packet[0] = 0xa1u;
        put_u32_le(packet + 1, utt->sequence);
        put_u32_le(packet + 5, capture_ms);
        err = websocket_send_binary_all(client, packet, got + 9);
        sent_bytes += got;
        if (err == ESP_OK && STREAM_FRAME_PACE_MS > 0) {
            vTaskDelay(pdMS_TO_TICKS(STREAM_FRAME_PACE_MS));
        }
    }
    free(packet);
    if (file != NULL) {
        fclose(file);
    }
    if (err == ESP_OK) {
        char commit[160];
        snprintf(commit, sizeof(commit),
                 "{\"type\":\"input_audio_buffer.commit\",\"turnId\":\"segment-%u\",\"final\":%s,\"bytes\":%u}",
                 (unsigned)utt->sequence, utt->final_segment ? "true" : "false", (unsigned)utt->byte_count);
        err = websocket_send_text_all(client, commit);
    }

    esp_websocket_client_close(client, pdMS_TO_TICKS(1000));
    esp_websocket_client_destroy(client);
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
    mp3dec_t dec;
    mp3dec_frame_info_t info;
    int16_t *pcm = heap_caps_malloc(MINIMP3_MAX_SAMPLES_PER_FRAME * sizeof(int16_t) * 2,
                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (pcm == NULL) {
        pcm = malloc(MINIMP3_MAX_SAMPLES_PER_FRAME * sizeof(int16_t) * 2);
    }
    if (pcm == NULL) {
        return ESP_ERR_NO_MEM;
    }
    mp3dec_init(&dec);
    if (p->cfg.io.mute != NULL) {
        p->cfg.io.mute(false, p->cfg.io.user);
    }
    size_t offset = 0;
    int current_hz = 0;
    while (offset < mp3_len) {
        memset(&info, 0, sizeof(info));
        int samples = mp3dec_decode_frame(&dec, mp3 + offset, (int)(mp3_len - offset), pcm, &info);
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
    return ESP_OK;
}

static uint32_t frame_rms(const int16_t *frame, size_t count)
{
    uint64_t acc = 0;
    for (size_t i = 0; i < count; ++i) {
        int32_t s = frame[i];
        acc += (uint64_t)(s * s);
    }
    return count == 0 ? 0 : (uint32_t)(acc / count);
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
        const uint32_t adaptive = p->noise_rms + (p->noise_rms >> 6) + (VAD_MIN_DELTA_RMS / 4);
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
    return p->capture_file != NULL;
}

static bool queue_utterance(astrolabe_audio_pipeline_t *p, bool final_segment)
{
    if (p->capture_file != NULL) {
        fflush(p->capture_file);
        fclose(p->capture_file);
        p->capture_file = NULL;
    }
    const size_t min_bytes = (((size_t)p->cfg.min_ms * p->cfg.sample_rate_hz) / 1000) * sizeof(int16_t);
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
    emit(p, ASTROLABE_AUDIO_PIPELINE_EVENT_CAPTURE_QUEUED, NULL);
    reset_capture(p);
    return true;
}

static bool rotate_capture_segment(astrolabe_audio_pipeline_t *p)
{
    if (!queue_utterance(p, false)) {
        return false;
    }
    if (!begin_capture_file(p)) {
        reset_capture(p);
        return false;
    }
    p->speech_active = true;
    p->active_peak_rms = p->last_rms;
    p->silence_frames = 0;
    emit(p, ASTROLABE_AUDIO_PIPELINE_EVENT_CAPTURE_START, "segment");
    return true;
}

static bool push_frame(astrolabe_audio_pipeline_t *p, const int16_t *frame, size_t frame_samples)
{
    p->last_rms = frame_rms(frame, frame_samples);
    p->vad_frames_seen++;
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
            p->active_peak_rms = p->last_rms;
            p->silence_frames = 0;
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
    if (p->capture_len_bytes + frame_bytes > p->segment_cap_bytes) {
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
    if (voiced) {
        p->silence_frames = 0;
    } else {
        update_noise_floor(p, p->last_rms);
        if (++p->silence_frames >= p->cfg.silence_frames) {
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
        frame = malloc(p->cfg.frame_samples * sizeof(int16_t));
    }
    if (frame == NULL) {
        emit(p, ASTROLABE_AUDIO_PIPELINE_EVENT_ERROR, "listen frame alloc");
        p->listen_task = NULL;
        vTaskDelete(NULL);
    }
    emit(p, ASTROLABE_AUDIO_PIPELINE_EVENT_LISTENING, NULL);
    while (p->running) {
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
        emit(p, ASTROLABE_AUDIO_PIPELINE_EVENT_THINKING, NULL);
        ESP_LOGI(TAG, "posting capture segment #%u final=%s bytes=%u path=%s",
                 (unsigned)utt.sequence, utt.final_segment ? "yes" : "no",
                 (unsigned)utt.byte_count, utt.path);
        voice_result_t result = {};
        esp_err_t err = stream_pcm_file(p, &utt);
        if (err != ESP_OK) {
            if (p->cfg.stream_url != NULL && p->cfg.stream_url[0] != '\0' && !utt.final_segment) {
                ESP_LOGW(TAG, "voice-stream dropped rolling segment #%u after send failure",
                         (unsigned)utt.sequence);
                err = ESP_OK;
            } else {
                ESP_LOGW(TAG, "voice-stream unavailable for segment #%u, falling back to voice-pipeline",
                         (unsigned)utt.sequence);
                err = post_pcm_file(p, utt.path, utt.byte_count, &result);
            }
        }
        remove(utt.path);
        if (utt.slot < p->capture_ring_slots) {
            p->capture_slot_busy[utt.slot] = false;
        }
        if (err != ESP_OK) {
            emit(p, ASTROLABE_AUDIO_PIPELINE_EVENT_ERROR, "voice-pipeline");
            start_capture_cooldown(p);
            emit(p, ASTROLABE_AUDIO_PIPELINE_EVENT_LISTENING, NULL);
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
        emit(p, ASTROLABE_AUDIO_PIPELINE_EVENT_LISTENING, NULL);
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
    p->capture_cap_bytes = (size_t)p->cfg.max_seconds * p->cfg.sample_rate_hz * sizeof(int16_t);
    const uint32_t segment_ms = p->cfg.capture_segment_ms != 0 ? p->cfg.capture_segment_ms : DEFAULT_SEGMENT_MS;
    p->segment_cap_bytes = (((size_t)segment_ms * p->cfg.sample_rate_hz) / 1000) * sizeof(int16_t);
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
    BaseType_t ok = xTaskCreate(listen_task, "ast_audio_listen",
                                p->cfg.listen_stack ? p->cfg.listen_stack : DEFAULT_LISTEN_STACK, p,
                                p->cfg.listen_priority ? p->cfg.listen_priority : 5, &p->listen_task);
    if (ok != pdPASS) {
        p->running = false;
        return ESP_ERR_NO_MEM;
    }
    ok = xTaskCreate(voice_task, "ast_audio_voice",
                     p->cfg.voice_stack ? p->cfg.voice_stack : DEFAULT_VOICE_STACK, p,
                     p->cfg.voice_priority ? p->cfg.voice_priority : 4, &p->voice_task);
    if (ok != pdPASS) {
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
}

void astrolabe_audio_pipeline_destroy(astrolabe_audio_pipeline_t *p)
{
    if (p == NULL) {
        return;
    }
    astrolabe_audio_pipeline_stop(p);
    if (p->utterance_queue != NULL) {
        utterance_t utt = {};
        while (xQueueReceive(p->utterance_queue, &utt, 0) == pdTRUE) {
            remove(utt.path);
        }
        vQueueDelete(p->utterance_queue);
    }
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
