#include "faculty175_voice.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "esp_attr.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_spiffs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"
#include "minimp3.h"

#include "astrolabe_faculty175_face.h"
#include "faculty175_device_auth.h"
#include "faculty175_face_profile.h"
#include "faculty175_listen.h"
#include "faculty175_board.h"
#include "faculty175_log.h"
#include "faculty175_screen_http.h"

#if __has_include("secrets.local.h")
#include "secrets.local.h"
#else
#include "secrets.example.h"
#endif

static const char *TAG = "faculty175_voice";
/* A failed cloud turn must return control to the UI in human-scale time. */
#define HTTP_TIMEOUT_MS 60000
#define VOICE_RESP_MAX_BYTES (768 * 1024)
/* Replies spool to flash, so this limit bounds playback time rather than RAM.
 * 128 KiB accommodates normal face readings while still rejecting runaway
 * responses before they can monopolize the duplex UI. */
#define TTS_MP3_MAX_BYTES (128 * 1024)
#define VOICE_STREAM_CT "application/vnd.astrolabe.voice-stream"
#define VOICE_SPOOL_BASE "/voice"
#define VOICE_SPOOL_PARTITION "voice_spool"
#define VOICE_SPOOL_PATH VOICE_SPOOL_BASE "/voice-turn.pcm"
#define VOICE_TTS_PATH VOICE_SPOOL_BASE "/voice-reply.mp3"
#define VOICE_HEAP_MIN_INTERNAL_FREE (1024)
#define VOICE_HEAP_MIN_INTERNAL_FREE_STT_FINISH (1024)
#define VOICE_HEAP_MIN_INTERNAL_LARGEST (512)
#define VOICE_HEAP_MIN_PSRAM_FREE (512 * 1024)
#define VOICE_STREAM_CHUNK_BYTES 1024
#define VOICE_PCM_CAPTURE_MAX_BYTES (15 * 16000 * sizeof(int16_t))
#define VOICE_FLASH_WRITE_BUFFER_BYTES 4096
#define VOICE_MP3_STREAM_BUFFER_BYTES (24 * 1024)
#define VOICE_FLASH_READ_BOUNCE_BYTES 4096
#define VOICE_PLAY_TASK_TIMEOUT_MS 30000

#ifndef MYNAH_VOICE_HTTP_URL
#define MYNAH_VOICE_HTTP_URL ""
#endif

EXT_RAM_BSS_ATTR static uint8_t s_voice_mp3_stream_buf[VOICE_MP3_STREAM_BUFFER_BYTES];
EXT_RAM_BSS_ATTR static uint8_t s_voice_http_chunk[VOICE_STREAM_CHUNK_BYTES];
EXT_RAM_BSS_ATTR static int16_t s_voice_mp3_pcm[MINIMP3_MAX_SAMPLES_PER_FRAME * 2];
EXT_RAM_BSS_ATTR static mp3dec_t s_voice_mp3_dec;

typedef struct {
    bool open;
    size_t pcm_bytes;
    uint8_t *pcm_buf;
    size_t pcm_cap;
    uint8_t *spool_buf;
    char *meta_json;
    size_t meta_len;
    FILE *spool;
    char spool_path[64];
    char faculty_slug[64];
    char faculty_name[96];
} faculty175_voice_stream_t;

static faculty175_voice_stream_t s_stream = {};

typedef struct {
    bool open;
    bool configured;
    uint32_t playback_rate_hz;
    uint32_t start_ms;
    size_t len;
    size_t off;
    size_t bytes_in;
    size_t decoded_frames;
    esp_err_t err;
} faculty175_tts_speaker_stream_state_t;

EXT_RAM_BSS_ATTR static faculty175_tts_speaker_stream_state_t s_tts_speaker_stream = {};
static volatile bool s_tts_playback_busy;

static esp_err_t voice_set_decoder_playback_rate(uint32_t decoder_rate_hz,
                                                  uint32_t *playback_rate_hz)
{
    if (playback_rate_hz == NULL || decoder_rate_hz < 8000u || decoder_rate_hz > 48000u) {
        return ESP_ERR_INVALID_ARG;
    }
    if (*playback_rate_hz == decoder_rate_hz) {
        return ESP_OK;
    }

    const esp_err_t err = faculty175_audio_set_sample_rate(decoder_rate_hz);
    if (err != ESP_OK) {
        FACULTY175_LOG_STAGE_W(TAG,
                               "tts",
                               "playback rate %u Hz configure failed: %s",
                               (unsigned)decoder_rate_hz,
                               esp_err_to_name(err));
        return err;
    }
    *playback_rate_hz = decoder_rate_hz;
    FACULTY175_LOG_STAGE(TAG,
                         "tts",
                         "playback rate configured=%u Hz",
                         (unsigned)decoder_rate_hz);
    return ESP_OK;
}

static esp_err_t voice_restore_capture_rate(uint32_t playback_rate_hz)
{
    (void)faculty175_audio_reset_speaker(1000);
    if (playback_rate_hz != 0u && playback_rate_hz != FACULTY175_AUDIO_RATE) {
        const esp_err_t rate_err = faculty175_audio_set_sample_rate(FACULTY175_AUDIO_RATE);
        if (rate_err != ESP_OK) {
            FACULTY175_LOG_STAGE_W(TAG,
                                   "tts",
                                   "capture rate %u Hz restore failed: %s",
                                   (unsigned)FACULTY175_AUDIO_RATE,
                                   esp_err_to_name(rate_err));
            return rate_err;
        }
    }
    return faculty175_audio_reset_capture(1000);
}

static uint32_t voice_internal_free(void)
{
    return (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

static uint32_t voice_internal_largest(void)
{
    return (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

static uint32_t voice_psram_free(void)
{
    return (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

static void voice_log_heap(const char *stage)
{
    const uint32_t free_i = voice_internal_free();
    const uint32_t largest_i = voice_internal_largest();
    if (free_i < 2048 || largest_i < 1024) {
        return;
    }
    FACULTY175_LOG_STAGE(TAG, "heap", "%s internal=%u largest=%u psram=%u",
                         stage != NULL ? stage : "voice",
                         (unsigned)free_i,
                         (unsigned)largest_i,
                         (unsigned)voice_psram_free());
}

static void voice_pipeline_url_from_base(char *out, size_t cap, const char *base)
{
    if (out == NULL || cap == 0) {
        return;
    }
    out[0] = '\0';
    if (base == NULL || base[0] == '\0') {
        return;
    }
    if (strstr(base, "/functions/v1/voice-pipeline") != NULL) {
        strlcpy(out, base, cap);
    } else {
        snprintf(out, cap, "%s/functions/v1/voice-pipeline", base);
    }
}

static bool voice_pipeline_http_fallback_url(char *out, size_t cap, const char *primary_url)
{
    if (out == NULL || cap == 0 || primary_url == NULL || primary_url[0] == '\0') {
        return false;
    }
    out[0] = '\0';
    if (MYNAH_VOICE_HTTP_URL[0] != '\0') {
        voice_pipeline_url_from_base(out, cap, MYNAH_VOICE_HTTP_URL);
    }
    return out[0] != '\0' && strcmp(out, primary_url) != 0;
}

static void voice_pipeline_preferred_url(char *out, size_t cap)
{
    const char *base = MYNAH_VOICE_HTTP_URL[0] != '\0' ? MYNAH_VOICE_HTTP_URL : MYNAH_SUPABASE_URL;
    voice_pipeline_url_from_base(out, cap, base);
}

static bool voice_should_try_http_fallback(esp_err_t err, int status)
{
    return err != ESP_OK && status == 0;
}

bool faculty175_voice_config_ready(char *reason, size_t reason_cap)
{
    if (reason != NULL && reason_cap > 0) {
        reason[0] = '\0';
    }

    const bool has_http_url = MYNAH_VOICE_HTTP_URL[0] != '\0';
    const bool has_supabase_url = MYNAH_SUPABASE_URL[0] != '\0';
    const bool has_key = MYNAH_SUPABASE_ANON_KEY[0] != '\0';

    if (!has_http_url && !has_supabase_url) {
        if (reason != NULL && reason_cap > 0) {
            strlcpy(reason, "missing MYNAH_VOICE_HTTP_URL or MYNAH_SUPABASE_URL", reason_cap);
        }
        return false;
    }
    if (!has_http_url && !has_key) {
        if (reason != NULL && reason_cap > 0) {
            strlcpy(reason, "missing MYNAH_SUPABASE_ANON_KEY", reason_cap);
        }
        return false;
    }

    if (reason != NULL && reason_cap > 0) {
        snprintf(reason,
                 reason_cap,
                 "url=%s auth=%s",
                 has_http_url ? "voice-http" : "supabase",
                 has_key ? "supabase-key" : "none");
    }
    return true;
}

static esp_err_t voice_require_config(const char *stage)
{
    char reason[96];
    if (!faculty175_voice_config_ready(reason, sizeof(reason))) {
        FACULTY175_LOG_STAGE_W(TAG, stage != NULL ? stage : "voice", "voice config not ready: %s", reason);
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

bool faculty175_voice_heap_ready(const char *stage)
{
    const uint32_t free_i = voice_internal_free();
    const uint32_t largest_i = voice_internal_largest();
    const uint32_t free_psram = voice_psram_free();
    const bool stt_finish = stage != NULL &&
                            (strcmp(stage, "voice-finish") == 0 || strcmp(stage, "voice-post") == 0);
    const uint32_t need_free_i = stt_finish ? VOICE_HEAP_MIN_INTERNAL_FREE_STT_FINISH
                                            : VOICE_HEAP_MIN_INTERNAL_FREE;
    if (free_i < need_free_i || largest_i < VOICE_HEAP_MIN_INTERNAL_LARGEST ||
        free_psram < VOICE_HEAP_MIN_PSRAM_FREE) {
        esp_rom_printf("voice: low heap stage=%s internal=%u largest=%u psram=%u need=%u/%u/%u\n",
                       stage != NULL ? stage : "voice",
                       (unsigned)free_i,
                       (unsigned)largest_i,
                       (unsigned)free_psram,
                       (unsigned)need_free_i,
                       VOICE_HEAP_MIN_INTERNAL_LARGEST,
                       VOICE_HEAP_MIN_PSRAM_FREE);
        return false;
    }
    return true;
}

static esp_err_t voice_spiffs_mount(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = VOICE_SPOOL_BASE,
        .partition_label = VOICE_SPOOL_PARTITION,
        .max_files = 16,
        .format_if_mount_failed = true,
    };
    const esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
        size_t total = 0;
        size_t used = 0;
        const size_t need = (size_t)FACULTY175_LISTEN_MAX_SECONDS * 16000 * sizeof(int16_t) + TTS_MP3_MAX_BYTES + 16384;
        if (esp_spiffs_info(VOICE_SPOOL_PARTITION, &total, &used) == ESP_OK && total >= used) {
            const size_t free_bytes = total - used;
            if (free_bytes < need) {
                FACULTY175_LOG_STAGE_W(TAG, "stream", "SPIFFS low space free=%u need=%u",
                                 (unsigned)free_bytes, (unsigned)need);
                return ESP_ERR_NO_MEM;
            }
            return ESP_OK;
        }
        return ESP_OK;
    }
    FACULTY175_LOG_STAGE_W(TAG, "stream", "SPIFFS unavailable for voice spool: %s", esp_err_to_name(err));
    return err;
}

static char *json_escape_alloc(const char *src)
{
    if (src == NULL) {
        return strdup("");
    }
    size_t cap = strlen(src) * 6 + 8;
    char *out = heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (out == NULL) {
        out = malloc(cap);
    }
    if (out == NULL) {
        return NULL;
    }
    size_t w = 0;
    for (const unsigned char *p = (const unsigned char *)src; *p != '\0'; ++p) {
        switch (*p) {
        case '\"':
        case '\\':
            out[w++] = '\\';
            out[w++] = (char)*p;
            break;
        case '\b':
            out[w++] = '\\';
            out[w++] = 'b';
            break;
        case '\f':
            out[w++] = '\\';
            out[w++] = 'f';
            break;
        case '\n':
            out[w++] = '\\';
            out[w++] = 'n';
            break;
        case '\r':
            out[w++] = '\\';
            out[w++] = 'r';
            break;
        case '\t':
            out[w++] = '\\';
            out[w++] = 't';
            break;
        default:
            if (*p < 0x20) {
                snprintf(out + w, cap - w, "\\u%04x", (unsigned)*p);
                w += 6;
            } else {
                out[w++] = (char)*p;
            }
            break;
        }
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
    size_t i = 0;
    while (start[i] != '\0' && start[i] != '\"' && i + 1 < cap) {
        if (start[i] == '\\' && start[i + 1] != '\0') {
            out[i] = start[i + 1];
            i++;
            start++;
        } else {
            out[i] = start[i];
        }
        i++;
    }
    out[i] = '\0';
    return out;
}

static bool extract_audio_base64(const char *json, uint8_t **out_bin, size_t *out_len)
{
    if (json == NULL || out_bin == NULL || out_len == NULL) {
        return false;
    }
    const char *key = "\"audioBase64\":\"";
    const char *start = strstr(json, key);
    if (start == NULL) {
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

    size_t cap = (b64_len / 4) * 3 + 64;
    uint8_t *buf = heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buf == NULL) {
        buf = malloc(cap);
    }
    if (buf == NULL) {
        return false;
    }

    size_t olen = 0;
    int rc = mbedtls_base64_decode(buf, cap, &olen, (const unsigned char *)start, b64_len);
    if (rc == MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL) {
        free(buf);
        cap = (b64_len / 4) * 3 + 256;
        buf = heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (buf == NULL) {
            buf = malloc(cap);
        }
        if (buf == NULL) {
            return false;
        }
        rc = mbedtls_base64_decode(buf, cap, &olen, (const unsigned char *)start, b64_len);
    }
    if (rc != 0 || olen == 0) {
        free(buf);
        return false;
    }
    *out_bin = buf;
    *out_len = olen;
    return true;
}

void faculty175_voice_result_free(faculty175_voice_result_t *result)
{
    if (result == NULL) {
        return;
    }
    if (result->mp3 != NULL) {
        free(result->mp3);
        result->mp3 = NULL;
    }
    result->mp3_len = 0;
    if (result->mp3_path[0] != '\0') {
        result->mp3_path[0] = '\0';
    }
}

static void url_decode_in_place(char *s)
{
    if (s == NULL) {
        return;
    }
    char *r = s;
    char *w = s;
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

static void capture_voice_response_headers(esp_http_client_handle_t client, faculty175_voice_result_t *result)
{
    if (client == NULL || result == NULL) {
        return;
    }
    const char *header = NULL;
    char *raw_header = NULL;
    if (esp_http_client_get_header(client, "X-Voice-Route", &raw_header) == ESP_OK && raw_header != NULL &&
        raw_header[0] != '\0') {
        header = raw_header;
    } else if (esp_http_client_get_header(client, "x-voice-route", &raw_header) == ESP_OK && raw_header != NULL &&
               raw_header[0] != '\0') {
        header = raw_header;
    }
    if (header != NULL) {
        strlcpy(result->route, header, sizeof(result->route));
        url_decode_in_place(result->route);
    }
    header = NULL;
    raw_header = NULL;
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

static esp_err_t voice_http_event(esp_http_client_event_t *event)
{
    if (event == NULL || event->event_id != HTTP_EVENT_ON_HEADER || event->user_data == NULL ||
        event->header_key == NULL || event->header_value == NULL) {
        return ESP_OK;
    }

    faculty175_voice_result_t *result = (faculty175_voice_result_t *)event->user_data;
    char *dst = NULL;
    size_t cap = 0;
    if (strcasecmp(event->header_key, "X-Voice-Route") == 0) {
        dst = result->route;
        cap = sizeof(result->route);
    } else if (strcasecmp(event->header_key, "X-Faculty-Slug") == 0) {
        dst = result->faculty_slug;
        cap = sizeof(result->faculty_slug);
    } else if (strcasecmp(event->header_key, "X-Faculty-Name") == 0) {
        dst = result->faculty_name;
        cap = sizeof(result->faculty_name);
    } else if (strcasecmp(event->header_key, "X-Voice-Transcript") == 0) {
        dst = result->transcript;
        cap = sizeof(result->transcript);
    } else if (strcasecmp(event->header_key, "X-Voice-Reply") == 0) {
        dst = result->reply;
        cap = sizeof(result->reply);
    }
    if (dst != NULL && dst[0] == '\0') {
        strlcpy(dst, event->header_value, cap);
        url_decode_in_place(dst);
    }
    return ESP_OK;
}

static esp_err_t http_set_supabase_headers(esp_http_client_handle_t client)
{
    if (strlen(MYNAH_SUPABASE_ANON_KEY) > 0) {
        esp_http_client_set_header(client, "apikey", MYNAH_SUPABASE_ANON_KEY);
        char auth[512];
        snprintf(auth, sizeof(auth), "Bearer %s", MYNAH_SUPABASE_ANON_KEY);
        esp_http_client_set_header(client, "Authorization", auth);
    }
    return ESP_OK;
}

static void stream_buffer_reset(void)
{
    if (s_stream.spool != NULL) {
        fclose(s_stream.spool);
    }
    free(s_stream.pcm_buf);
    free(s_stream.spool_buf);
    free(s_stream.meta_json);
    memset(&s_stream, 0, sizeof(s_stream));
}

static char *build_faculty_request_json(const char *face,
                                        const char *faculty_slug,
                                        const char *faculty_name,
                                        const char *system_instruction,
                                        const char *history,
                                        size_t *out_len)
{
    const char *active_face = (face != NULL && face[0] != '\0') ? face : ASTROLABE_FACULTY_FACE_NAME;
    const char *active_slug =
        (faculty_slug != NULL && faculty_slug[0] != '\0') ? faculty_slug : ASTROLABE_FACULTY_DEFAULT_FACULTY_SLUG;
    const char *active_name =
        (faculty_name != NULL && faculty_name[0] != '\0') ? faculty_name : ASTROLABE_FACULTY_DEFAULT_FACULTY_NAME;
    const char *base_system = (system_instruction != NULL && system_instruction[0] != '\0')
                                  ? system_instruction
                                  : ASTROLABE_FACULTY_SYSTEM_INSTRUCTION;
    const char *device_profile = faculty175_face_profile_slug(faculty175_face_profile_current());

    char *esc_sys = json_escape_alloc(base_system);
    char *esc_face = json_escape_alloc(active_face);
    char *esc_slug = json_escape_alloc(active_slug);
    char *esc_name = json_escape_alloc(active_name);
    char *esc_history = json_escape_alloc(history != NULL && history[0] != '\0' ? history : "(none yet)");
    if (esc_sys == NULL || esc_face == NULL || esc_slug == NULL || esc_name == NULL || esc_history == NULL) {
        free(esc_sys);
        free(esc_face);
        free(esc_slug);
        free(esc_name);
        free(esc_history);
        return NULL;
    }

    const size_t cap = strlen(esc_sys) + strlen(esc_face) + strlen(esc_slug) + strlen(esc_name) +
                       strlen(esc_history) + strlen(device_profile) + 352;
    char *json = heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (json == NULL) {
        json = malloc(cap);
    }
    if (json == NULL) {
        free(esc_sys);
        free(esc_face);
        free(esc_slug);
        free(esc_name);
        free(esc_history);
        return NULL;
    }

    const int json_len = snprintf(json, cap,
                                  "{\"languageCode\":\"en-US\",\"sampleRateHertz\":16000,"
                                  "\"face\":\"%s\",\"facultySlug\":\"%s\",\"facultyName\":\"%s\","
                                  "\"deviceProfile\":\"%s\","
                                  "\"systemInstruction\":\"%s\",\"conversationHistory\":\"%s\","
                                  "\"responseFormat\":\"mp3\"}",
                                  esc_face, esc_slug, esc_name, device_profile, esc_sys, esc_history);
    free(esc_sys);
    free(esc_face);
    free(esc_slug);
    free(esc_name);
    free(esc_history);
    if (json_len <= 0 || (size_t)json_len >= cap) {
        free(json);
        return NULL;
    }
    if (out_len != NULL) {
        *out_len = (size_t)json_len;
    }
    return json;
}

static esp_err_t read_http_body(esp_http_client_handle_t client, int status, uint8_t **out_body, size_t *out_len)
{
    if (out_body != NULL) {
        *out_body = NULL;
    }
    if (out_len != NULL) {
        *out_len = 0;
    }
    if (status < 200 || status >= 300) {
        char err_body[160] = {};
        const int err_read = esp_http_client_read(client, err_body, (int)sizeof(err_body) - 1);
        if (err_read > 0) {
            err_body[err_read] = '\0';
            char clipped[96];
            faculty175_log_clip(clipped, sizeof(clipped), err_body, 80);
            FACULTY175_LOG_STAGE_W(TAG, "pipeline", "HTTP %d err=%s", status, clipped);
        } else {
            FACULTY175_LOG_STAGE_W(TAG, "pipeline", "HTTP %d (no body)", status);
        }
        return ESP_FAIL;
    }

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
        const int read = esp_http_client_read(client, (char *)response + total, (int)(cap - total));
        if (read < 0) {
            free(response);
            return ESP_FAIL;
        }
        if (read == 0) {
            break;
        }
        total += (size_t)read;
    }
    if (out_body != NULL) {
        *out_body = response;
    } else {
        free(response);
    }
    if (out_len != NULL) {
        *out_len = total;
    }
    return ESP_OK;
}

static esp_err_t read_http_body_to_file(esp_http_client_handle_t client,
                                        int status,
                                        const char *path,
                                        const faculty175_voice_tts_stream_t *stream,
                                        size_t *out_len)
{
    if (out_len != NULL) {
        *out_len = 0;
    }
    const bool spool = path != NULL && path[0] != '\0';
    const bool callback = stream != NULL && stream->on_mp3_chunk != NULL;
    if (!spool && !callback) {
        return ESP_ERR_INVALID_ARG;
    }
    if (status < 200 || status >= 300) {
        char err_body[160] = {};
        const int err_read = esp_http_client_read(client, err_body, (int)sizeof(err_body) - 1);
        if (err_read > 0) {
            err_body[err_read] = '\0';
            char clipped[96];
            faculty175_log_clip(clipped, sizeof(clipped), err_body, 80);
            FACULTY175_LOG_STAGE_W(TAG, "pipeline", "HTTP %d err=%s", status, clipped);
        } else {
            FACULTY175_LOG_STAGE_W(TAG, "pipeline", "HTTP %d (no body)", status);
        }
        return ESP_FAIL;
    }

    FILE *f = NULL;
    if (spool) {
        f = fopen(path, "wb");
        if (f == NULL) {
            return ESP_FAIL;
        }
    }
    size_t total = 0;
    while (true) {
        const int read = esp_http_client_read(client, (char *)s_voice_http_chunk, (int)sizeof(s_voice_http_chunk));
        if (read < 0) {
            if (f != NULL) {
                fclose(f);
            }
            return ESP_FAIL;
        }
        if (read == 0) {
            break;
        }
        if (f != NULL) {
            if (fwrite(s_voice_http_chunk, 1, (size_t)read, f) != (size_t)read) {
                fclose(f);
                return ESP_FAIL;
            }
        }
        if (callback) {
            const esp_err_t cb_err = stream->on_mp3_chunk(s_voice_http_chunk, (size_t)read, stream->user);
            if (cb_err != ESP_OK) {
                if (f != NULL) {
                    fclose(f);
                }
                return cb_err;
            }
        }
        total += (size_t)read;
        if (total > TTS_MP3_MAX_BYTES) {
            if (f != NULL) {
                fclose(f);
            }
            FACULTY175_LOG_STAGE_W(TAG, "tts", "MP3 response too large: %uB max=%u",
                             (unsigned)total, (unsigned)TTS_MP3_MAX_BYTES);
            return ESP_ERR_INVALID_SIZE;
        }
        vTaskDelay(1);
    }
    if (f != NULL) {
        fclose(f);
    }
    if (total < 64) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (out_len != NULL) {
        *out_len = total;
    }
    return ESP_OK;
}

static esp_err_t parse_voice_response_body(uint8_t *response,
                                           size_t response_len,
                                           faculty175_voice_result_t *result,
                                           uint32_t t0)
{
    if (response == NULL || response_len < 8 || result == NULL) {
        return ESP_FAIL;
    }

    if (response_len > 0 && response[0] != '{') {
        FACULTY175_LOG_STAGE(TAG, "tts", "raw MP3 response %uB (no JSON wrapper)", (unsigned)response_len);
        result->mp3 = response;
        result->mp3_len = response_len;
        return ESP_OK;
    }

    char *json = realloc(response, response_len + 1);
    if (json == NULL) {
        free(response);
        return ESP_ERR_NO_MEM;
    }
    json[response_len] = '\0';

    json_find_string(json, "transcript", result->transcript, sizeof(result->transcript));
    json_find_string(json, "reply", result->reply, sizeof(result->reply));
    json_find_string(json, "route", result->route, sizeof(result->route));
    json_find_string(json, "facultySlug", result->faculty_slug, sizeof(result->faculty_slug));
    json_find_string(json, "facultyName", result->faculty_name, sizeof(result->faculty_name));

    uint8_t *mp3 = NULL;
    size_t mp3_len = 0;
    if (extract_audio_base64(json, &mp3, &mp3_len)) {
        result->mp3 = mp3;
        result->mp3_len = mp3_len;
    }
    free(json);

    char clip[120];
    if (result->transcript[0] != '\0') {
        faculty175_log_clip(clip, sizeof(clip), result->transcript, 96);
        FACULTY175_LOG_STAGE(TAG, "stt", "transcript=\"%s\"", clip);
    } else {
        FACULTY175_LOG_STAGE_W(TAG, "stt", "empty transcript");
    }
    if (result->route[0] != '\0') {
        FACULTY175_LOG_STAGE(TAG, "llm", "route=%s", result->route);
    }
    if (result->faculty_slug[0] != '\0' || result->faculty_name[0] != '\0') {
        FACULTY175_LOG_STAGE(TAG, "llm", "faculty=%s (%s)", result->faculty_name[0] ? result->faculty_name : "?",
                       result->faculty_slug[0] ? result->faculty_slug : "?");
    }
    if (result->reply[0] != '\0') {
        faculty175_log_clip(clip, sizeof(clip), result->reply, 96);
        FACULTY175_LOG_STAGE(TAG, "llm", "reply=\"%s\"", clip);
    } else {
        FACULTY175_LOG_STAGE_W(TAG, "llm", "empty reply text");
    }
    if (result->mp3 != NULL && result->mp3_len > 0) {
        FACULTY175_LOG_STAGE(TAG, "tts", "mp3=%uB embedded in JSON", (unsigned)result->mp3_len);
    } else {
        FACULTY175_LOG_STAGE_W(TAG, "tts", "no audioBase64 in response");
    }

    if (result->transcript[0] == '\0' && result->reply[0] == '\0' && result->mp3 == NULL) {
        FACULTY175_LOG_STAGE_E(TAG, "pipeline", "response missing transcript, reply, and audio");
        return ESP_FAIL;
    }
    FACULTY175_LOG_STAGE(TAG, "pipeline", "turn complete in %ums", (unsigned)(faculty175_log_ms() - t0));
    return ESP_OK;
}

static esp_err_t post_collect_body(const char *url,
                                   const char *content_type,
                                   const uint8_t *body,
                                   size_t body_len,
                                   const char *accept,
                                   uint8_t **out_body,
                                   size_t *out_len,
                                   int *out_status)
{
    if (out_body != NULL) {
        *out_body = NULL;
    }
    if (out_len != NULL) {
        *out_len = 0;
    }
    if (out_status != NULL) {
        *out_status = 0;
    }

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 4096,
        .buffer_size_tx = 2048,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", content_type);
    if (accept != NULL) {
        esp_http_client_set_header(client, "Accept", accept);
    }
    http_set_supabase_headers(client);
    (void)faculty175_device_auth_headers(client);

    esp_err_t ret = esp_http_client_open(client, body_len);
    if (ret != ESP_OK) {
        esp_http_client_cleanup(client);
        return ret;
    }
    size_t written_total = 0;
    while (written_total < body_len) {
        const int written = esp_http_client_write(client, (const char *)body + written_total,
                                                  (int)(body_len - written_total));
        if (written <= 0) {
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return ESP_FAIL;
        }
        written_total += (size_t)written;
    }

    (void)esp_http_client_fetch_headers(client);
    const int status = esp_http_client_get_status_code(client);
    if (out_status != NULL) {
        *out_status = status;
    }

    uint8_t *response = NULL;
    size_t total = 0;
    ret = read_http_body(client, status, &response, &total);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    if (ret != ESP_OK) {
        free(response);
        return ret;
    }
    if (out_body != NULL) {
        *out_body = response;
    } else {
        free(response);
    }
    if (out_len != NULL) {
        *out_len = total;
    }
    return ESP_OK;
}

static esp_err_t write_all(esp_http_client_handle_t client, const void *data, size_t len);

static esp_err_t post_collect_file(const char *url,
                                   const char *content_type,
                                   const uint8_t *body,
                                   size_t body_len,
                                   const char *accept,
                                   const char *path,
                                   const faculty175_voice_tts_stream_t *stream,
                                   faculty175_voice_result_t *result,
                                   size_t *out_len,
                                   int *out_status)
{
    if (out_len != NULL) {
        *out_len = 0;
    }
    if (out_status != NULL) {
        *out_status = 0;
    }
    if (url == NULL || content_type == NULL || body == NULL || body_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    faculty175_screen_http_stop();
    voice_log_heap("voice-http-start");

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 1024,
        .buffer_size_tx = 1024,
        .event_handler = voice_http_event,
        .user_data = result,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        (void)faculty175_screen_http_start(NULL);
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", content_type);
    if (accept != NULL) {
        esp_http_client_set_header(client, "Accept", accept);
    }
    http_set_supabase_headers(client);
    (void)faculty175_device_auth_headers(client);

    esp_err_t ret = esp_http_client_open(client, body_len);
    if (ret != ESP_OK) {
        esp_http_client_cleanup(client);
        (void)faculty175_screen_http_start(NULL);
        return ret;
    }
    ret = write_all(client, body, body_len);
    if (ret != ESP_OK) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        (void)faculty175_screen_http_start(NULL);
        return ret;
    }

    (void)esp_http_client_fetch_headers(client);
    const int status = esp_http_client_get_status_code(client);
    voice_log_heap("voice-http-read");
    if (out_status != NULL) {
        *out_status = status;
    }
    if (status >= 200 && status < 300 && result != NULL) {
        capture_voice_response_headers(client, result);
    }

    size_t total = 0;
    ret = read_http_body_to_file(client, status, path, stream, &total);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    voice_log_heap("voice-http-done");
    (void)faculty175_screen_http_start(NULL);
    if (ret == ESP_OK && out_len != NULL) {
        *out_len = total;
    }
    return ret;
}

static esp_err_t write_all(esp_http_client_handle_t client, const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    size_t written_total = 0;
    while (written_total < len) {
        const size_t remain = len - written_total;
        const int want = remain > 4096 ? 4096 : (int)remain;
        const int written = esp_http_client_write(client, (const char *)p + written_total, want);
        if (written <= 0) {
            return ESP_FAIL;
        }
        written_total += (size_t)written;
    }
    return ESP_OK;
}

static esp_err_t post_collect_voice_file(const char *url,
                                         const char *meta_json,
                                         size_t meta_len,
                                         const char *pcm_path,
                                         size_t pcm_len,
                                         const faculty175_voice_tts_stream_t *stream,
                                         int *out_status,
                                         faculty175_voice_result_t *result)
{
    if (out_status != NULL) {
        *out_status = 0;
    }
    if (url == NULL || meta_json == NULL || meta_len == 0 || pcm_path == NULL || pcm_len == 0 || result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!faculty175_voice_heap_ready("voice-post")) {
        return ESP_ERR_NO_MEM;
    }

    FILE *pcm = fopen(pcm_path, "rb");
    if (pcm == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    faculty175_screen_http_stop();
    voice_log_heap("voice-stt-http-start");

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 1024,
        .buffer_size_tx = 1024,
        .event_handler = voice_http_event,
        .user_data = result,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        fclose(pcm);
        (void)faculty175_screen_http_start(NULL);
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", VOICE_STREAM_CT);
    esp_http_client_set_header(client, "Accept", "audio/mpeg");
    http_set_supabase_headers(client);
    (void)faculty175_device_auth_headers(client);

    const size_t body_len = 4 + meta_len + pcm_len;
    esp_err_t ret = esp_http_client_open(client, body_len);
    if (ret != ESP_OK) {
        fclose(pcm);
        esp_http_client_cleanup(client);
        (void)faculty175_screen_http_start(NULL);
        return ret;
    }

    uint8_t prefix[4] = {
        (uint8_t)(meta_len & 0xff),
        (uint8_t)((meta_len >> 8) & 0xff),
        (uint8_t)((meta_len >> 16) & 0xff),
        (uint8_t)((meta_len >> 24) & 0xff),
    };
    ret = write_all(client, prefix, sizeof(prefix));
    if (ret == ESP_OK) {
        ret = write_all(client, meta_json, meta_len);
    }

    while (ret == ESP_OK) {
        const size_t n = fread(s_voice_http_chunk, 1, sizeof(s_voice_http_chunk), pcm);
        if (n > 0) {
            ret = write_all(client, s_voice_http_chunk, n);
        }
        if (n < sizeof(s_voice_http_chunk)) {
            if (ferror(pcm)) {
                ret = ESP_FAIL;
            }
            break;
        }
        vTaskDelay(1);
    }
    fclose(pcm);

    if (ret != ESP_OK) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        (void)faculty175_screen_http_start(NULL);
        return ret;
    }

    (void)esp_http_client_fetch_headers(client);
    const int status = esp_http_client_get_status_code(client);
    voice_log_heap("voice-stt-http-read");
    if (out_status != NULL) {
        *out_status = status;
    }
    if (status >= 200 && status < 300) {
        capture_voice_response_headers(client, result);
    }
    size_t total = 0;
    const char *spool_path = stream != NULL && stream->spool_path != NULL && stream->spool_path[0] != '\0'
                                 ? stream->spool_path
                                 : VOICE_TTS_PATH;
    ret = read_http_body_to_file(client, status, spool_path, stream, &total);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    voice_log_heap("voice-stt-http-done");
    (void)faculty175_screen_http_start(NULL);
    if (ret == ESP_OK) {
        strlcpy(result->mp3_path, spool_path, sizeof(result->mp3_path));
        result->mp3_len = total;
        FACULTY175_LOG_STAGE(TAG, "tts", "mp3 spooled %uB -> %s", (unsigned)total, result->mp3_path);
    }
    return ret;
}

esp_err_t faculty175_voice_post_pcm(const uint8_t *pcm,
                              size_t pcm_len,
                              const char *faculty_slug,
                              const char *faculty_name,
                              const char *history,
                              faculty175_voice_result_t *result)
{
    if (pcm == NULL || pcm_len == 0 || result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t config_err = voice_require_config("pipeline");
    if (config_err != ESP_OK) {
        return config_err;
    }

    memset(result, 0, sizeof(*result));

    const char *active_slug =
        (faculty_slug != NULL && faculty_slug[0] != '\0') ? faculty_slug : ASTROLABE_FACULTY_DEFAULT_FACULTY_SLUG;
    const char *active_name =
        (faculty_name != NULL && faculty_name[0] != '\0') ? faculty_name : ASTROLABE_FACULTY_DEFAULT_FACULTY_NAME;

    const size_t b64_cap = ((pcm_len + 2) / 3) * 4 + 1;
    char *audio_b64 = heap_caps_malloc(b64_cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (audio_b64 == NULL) {
        audio_b64 = malloc(b64_cap);
    }
    if (audio_b64 == NULL) {
        return ESP_ERR_NO_MEM;
    }
    size_t b64_len = 0;
    if (mbedtls_base64_encode((unsigned char *)audio_b64, b64_cap, &b64_len, pcm, pcm_len) != 0) {
        free(audio_b64);
        return ESP_FAIL;
    }
    audio_b64[b64_len] = '\0';

    char sys[896];
    snprintf(sys, sizeof(sys),
             "%s If the user does not name a faculty member, continue with the active faculty (%s, %s). "
             "Never exceed 35 spoken words, even when the user asks for detail. Conversation history: %s",
             ASTROLABE_FACULTY_SYSTEM_INSTRUCTION, active_name, active_slug,
             history != NULL && history[0] != '\0' ? history : "(none yet)");

    char *esc_sys = json_escape_alloc(sys);
    char *esc_slug = json_escape_alloc(active_slug);
    char *esc_name = json_escape_alloc(active_name);
    if (esc_sys == NULL || esc_slug == NULL || esc_name == NULL) {
        free(audio_b64);
        free(esc_sys);
        free(esc_slug);
        free(esc_name);
        return ESP_ERR_NO_MEM;
    }

    const size_t body_cap = b64_len + strlen(esc_sys) + strlen(esc_slug) + strlen(esc_name) + 256;
    char *body = heap_caps_malloc(body_cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (body == NULL) {
        body = malloc(body_cap);
    }
    if (body == NULL) {
        free(audio_b64);
        free(esc_sys);
        free(esc_slug);
        free(esc_name);
        return ESP_ERR_NO_MEM;
    }

    const int body_len = snprintf(body, body_cap,
                                  "{\"languageCode\":\"en-US\",\"sampleRateHertz\":16000,"
                                  "\"face\":\"%s\",\"facultySlug\":\"%s\",\"facultyName\":\"%s\","
                                  "\"systemInstruction\":\"%s\",\"audioBase64\":\"%s\"}",
                                  ASTROLABE_FACULTY_FACE_NAME, esc_slug, esc_name, esc_sys, audio_b64);
    free(audio_b64);
    free(esc_sys);
    free(esc_slug);
    free(esc_name);
    if (body_len <= 0 || (size_t)body_len >= body_cap) {
        free(body);
        return ESP_ERR_NO_MEM;
    }

    char url[224];
    voice_pipeline_preferred_url(url, sizeof(url));
    const uint32_t t0 = faculty175_log_ms();
    FACULTY175_LOG_STAGE(TAG, "pipeline", "POST %s pcm=%uB face=%s faculty=%s (%s)",
                   url, (unsigned)pcm_len, ASTROLABE_FACULTY_FACE_NAME, active_name, active_slug);

    int status = 0;
    uint8_t *response = NULL;
    size_t response_len = 0;
    esp_err_t ret = post_collect_body(url, "application/json", (const uint8_t *)body, (size_t)body_len,
                                      "application/json,audio/mpeg", &response, &response_len, &status);
    if (voice_should_try_http_fallback(ret, status)) {
        char fallback_url[224];
        if (voice_pipeline_http_fallback_url(fallback_url, sizeof(fallback_url), url)) {
            FACULTY175_LOG_STAGE_W(TAG, "pipeline", "HTTP fallback POST %s", fallback_url);
            free(response);
            response = NULL;
            response_len = 0;
            status = 0;
            ret = post_collect_body(fallback_url, "application/json", (const uint8_t *)body, (size_t)body_len,
                                    "application/json,audio/mpeg", &response, &response_len, &status);
        }
    }
    free(body);
    const uint32_t http_ms = faculty175_log_ms() - t0;
    FACULTY175_LOG_STAGE(TAG, "pipeline", "HTTP %d body=%uB in %ums err=%s", status, (unsigned)response_len, (unsigned)http_ms,
                   esp_err_to_name(ret));
    if (ret != ESP_OK || response == NULL || response_len < 8) {
        free(response);
        return ESP_FAIL;
    }

    ret = parse_voice_response_body(response, response_len, result, t0);
    if (ret != ESP_OK) {
        faculty175_voice_result_free(result);
    }
    return ret;
}

esp_err_t faculty175_voice_post_message_streaming(const char *message,
                                                  const char *system_instruction,
                                                  const char *face,
                                                  const char *faculty_slug,
                                                  const char *faculty_name,
                                                  const char *history,
                                                  const faculty175_voice_tts_stream_t *stream,
                                                  faculty175_voice_result_t *result)
{
    if (message == NULL || message[0] == '\0' || result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t config_err = voice_require_config("voice");
    if (config_err != ESP_OK) {
        return config_err;
    }

    memset(result, 0, sizeof(*result));

    const char *active_face =
        (face != NULL && face[0] != '\0') ? face : ASTROLABE_FACULTY_FACE_NAME;
    const char *active_slug =
        (faculty_slug != NULL && faculty_slug[0] != '\0') ? faculty_slug : ASTROLABE_FACULTY_DEFAULT_FACULTY_SLUG;
    const char *active_name =
        (faculty_name != NULL && faculty_name[0] != '\0') ? faculty_name : ASTROLABE_FACULTY_DEFAULT_FACULTY_NAME;
    const char *base_system =
        (system_instruction != NULL && system_instruction[0] != '\0') ? system_instruction : ASTROLABE_FACULTY_SYSTEM_INSTRUCTION;

    char sys[1536];
    snprintf(sys, sizeof(sys),
             "%s If the user does not name a faculty member, continue with the active faculty (%s, %s). "
             "Speak one compact, useful reading for the requested watch face. Conversation history: %s",
             base_system, active_name, active_slug,
             history != NULL && history[0] != '\0' ? history : "(none yet)");

    char *esc_message = json_escape_alloc(message);
    char *esc_sys = json_escape_alloc(sys);
    char *esc_face = json_escape_alloc(active_face);
    char *esc_slug = json_escape_alloc(active_slug);
    char *esc_name = json_escape_alloc(active_name);
    if (esc_message == NULL || esc_sys == NULL || esc_face == NULL || esc_slug == NULL || esc_name == NULL) {
        free(esc_message);
        free(esc_sys);
        free(esc_face);
        free(esc_slug);
        free(esc_name);
        return ESP_ERR_NO_MEM;
    }

    const size_t body_cap = strlen(esc_message) + strlen(esc_sys) + strlen(esc_face) +
                            strlen(esc_slug) + strlen(esc_name) + 320;
    char *body = heap_caps_malloc(body_cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (body == NULL) {
        body = malloc(body_cap);
    }
    if (body == NULL) {
        free(esc_message);
        free(esc_sys);
        free(esc_face);
        free(esc_slug);
        free(esc_name);
        return ESP_ERR_NO_MEM;
    }

    const int body_len = snprintf(body, body_cap,
                                  "{\"languageCode\":\"en-US\",\"sampleRateHertz\":16000,"
                                  "\"face\":\"%s\",\"facultySlug\":\"%s\",\"facultyName\":\"%s\","
                                  "\"systemInstruction\":\"%s\",\"message\":\"%s\"}",
                                  esc_face, esc_slug, esc_name, esc_sys, esc_message);
    free(esc_message);
    free(esc_sys);
    free(esc_face);
    free(esc_slug);
    free(esc_name);
    if (body_len <= 0 || (size_t)body_len >= body_cap) {
        free(body);
        return ESP_ERR_NO_MEM;
    }

    char url[224];
    voice_pipeline_preferred_url(url, sizeof(url));
    const uint32_t t0 = faculty175_log_ms();
    FACULTY175_LOG_STAGE(TAG, "voice", "HTTP POST message %uB face=%s faculty=%s (%s)",
                         (unsigned)body_len, active_face, active_name, active_slug);
    printf("voice: HTTP POST message (%u B body)\n", (unsigned)body_len);
    fflush(stdout);

    esp_err_t ret = voice_spiffs_mount();
    if (ret != ESP_OK) {
        free(body);
        return ret;
    }

    int status = 0;
    size_t mp3_len = 0;
    const char *spool_path = stream != NULL && stream->spool_path != NULL && stream->spool_path[0] != '\0'
                                 ? stream->spool_path
                                 : NULL;
    ret = post_collect_file(url,
                            "application/json",
                            (const uint8_t *)body,
                            (size_t)body_len,
                            "audio/mpeg",
                            spool_path,
                            stream,
                            result,
                            &mp3_len,
                            &status);
    if (voice_should_try_http_fallback(ret, status)) {
        char fallback_url[224];
        if (voice_pipeline_http_fallback_url(fallback_url, sizeof(fallback_url), url)) {
            FACULTY175_LOG_STAGE_W(TAG, "voice", "HTTP fallback POST message %s", fallback_url);
            printf("voice: HTTP fallback message\n");
            fflush(stdout);
            memset(result, 0, sizeof(*result));
            status = 0;
            mp3_len = 0;
            ret = post_collect_file(fallback_url,
                                    "application/json",
                                    (const uint8_t *)body,
                                    (size_t)body_len,
                                    "audio/mpeg",
                                    spool_path,
                                    stream,
                                    result,
                                    &mp3_len,
                                    &status);
        }
    }
    free(body);

    const uint32_t http_ms = faculty175_log_ms() - t0;
    FACULTY175_LOG_STAGE(TAG, "voice", "HTTP %d message mp3=%uB in %ums err=%s",
                         status, (unsigned)mp3_len, (unsigned)http_ms, esp_err_to_name(ret));
    printf("voice: HTTP %d (message)\n", status);
    fflush(stdout);
    if (ret != ESP_OK || mp3_len < 64) {
        return ESP_FAIL;
    }

    if (spool_path != NULL) {
        strlcpy(result->mp3_path, spool_path, sizeof(result->mp3_path));
    }
    result->mp3_len = mp3_len;
    FACULTY175_LOG_STAGE(TAG, "pipeline", "message turn complete in %ums", (unsigned)(faculty175_log_ms() - t0));
    printf("voice: message ok mp3=%u B file=%s stream=%s\n",
           (unsigned)result->mp3_len,
           result->mp3_path[0] != '\0' ? result->mp3_path : "-",
           stream != NULL && stream->on_mp3_chunk != NULL ? "yes" : "no");
    fflush(stdout);
    return ESP_OK;
}

esp_err_t faculty175_voice_post_message(const char *message,
                                        const char *system_instruction,
                                        const char *face,
                                        const char *faculty_slug,
                                        const char *faculty_name,
                                        const char *history,
                                        faculty175_voice_result_t *result)
{
    const faculty175_voice_tts_stream_t stream = {
        .spool_path = VOICE_TTS_PATH,
    };
    return faculty175_voice_post_message_streaming(message,
                                                   system_instruction,
                                                   face,
                                                   faculty_slug,
                                                   faculty_name,
                                                   history,
                                                   &stream,
                                                   result);
}

void faculty175_voice_stream_cancel(void)
{
    faculty175_voice_stt_stream_close();
}

bool faculty175_voice_stt_stream_is_open(void)
{
    return s_stream.open;
}

esp_err_t faculty175_voice_stt_stream_open(const faculty175_voice_stt_stream_config_t *config)
{
    const esp_err_t config_err = voice_require_config("stream");
    if (config_err != ESP_OK) {
        return config_err;
    }
    if (s_stream.open) {
        faculty175_voice_stt_stream_close();
    }
    esp_err_t err = voice_spiffs_mount();
    if (err != ESP_OK) {
        return err;
    }

    const char *face = config != NULL ? config->face : NULL;
    const char *faculty_slug = config != NULL ? config->faculty_slug : NULL;
    const char *faculty_name = config != NULL ? config->faculty_name : NULL;
    const char *system_instruction = config != NULL ? config->system_instruction : NULL;
    const char *history = config != NULL ? config->history : NULL;
    const char *active_slug =
        (faculty_slug != NULL && faculty_slug[0] != '\0') ? faculty_slug : ASTROLABE_FACULTY_DEFAULT_FACULTY_SLUG;
    const char *active_name =
        (faculty_name != NULL && faculty_name[0] != '\0') ? faculty_name : ASTROLABE_FACULTY_DEFAULT_FACULTY_NAME;

    size_t meta_len = 0;
    char *meta_json = build_faculty_request_json(face, active_slug, active_name, system_instruction, history, &meta_len);
    if (meta_json == NULL || meta_len == 0 || meta_len > 16384) {
        free(meta_json);
        return ESP_ERR_NO_MEM;
    }

    s_stream.meta_json = meta_json;
    s_stream.meta_len = meta_len;
    strlcpy(s_stream.spool_path, VOICE_SPOOL_PATH, sizeof(s_stream.spool_path));
    s_stream.spool = fopen(s_stream.spool_path, "wb");
    if (s_stream.spool == NULL) {
        stream_buffer_reset();
        return ESP_FAIL;
    }
    s_stream.spool_buf = heap_caps_malloc(VOICE_FLASH_WRITE_BUFFER_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_stream.spool_buf == NULL) {
        stream_buffer_reset();
        return ESP_ERR_NO_MEM;
    }
    if (setvbuf(s_stream.spool, (char *)s_stream.spool_buf, _IOFBF, VOICE_FLASH_WRITE_BUFFER_BYTES) != 0) {
        stream_buffer_reset();
        return ESP_FAIL;
    }
    s_stream.pcm_buf = heap_caps_malloc(VOICE_PCM_CAPTURE_MAX_BYTES,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_stream.pcm_buf == NULL) {
        stream_buffer_reset();
        return ESP_ERR_NO_MEM;
    }
    s_stream.pcm_cap = VOICE_PCM_CAPTURE_MAX_BYTES;
    s_stream.open = true;
    s_stream.pcm_bytes = 0;
    strlcpy(s_stream.faculty_slug, active_slug, sizeof(s_stream.faculty_slug));
    strlcpy(s_stream.faculty_name, active_name, sizeof(s_stream.faculty_name));
    FACULTY175_LOG_STAGE(TAG, "stream", "STT stream open -> %s faculty=%s (%s) heap=%u/%u psram=%u", s_stream.spool_path,
                   s_stream.faculty_name, s_stream.faculty_slug,
                   (unsigned)voice_internal_free(), (unsigned)voice_internal_largest(),
                   (unsigned)voice_psram_free());
    return ESP_OK;
}

esp_err_t faculty175_voice_stt_stream_write(const int16_t *pcm, size_t sample_count)
{
    if (!s_stream.open || pcm == NULL || sample_count == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_stream.spool == NULL || s_stream.pcm_buf == NULL) {
        faculty175_voice_stt_stream_close();
        return ESP_ERR_INVALID_STATE;
    }
    const size_t bytes = sample_count * sizeof(int16_t);
    if (s_stream.pcm_bytes + bytes > s_stream.pcm_cap) {
        faculty175_voice_stt_stream_close();
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(s_stream.pcm_buf + s_stream.pcm_bytes, pcm, bytes);
    s_stream.pcm_bytes += bytes;
    return ESP_OK;
}

esp_err_t faculty175_voice_stt_stream_commit_streaming(const faculty175_voice_tts_stream_t *tts_stream,
                                                       faculty175_voice_result_t *result)
{
    if (!s_stream.open || result == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t config_err = voice_require_config("stream");
    if (config_err != ESP_OK) {
        faculty175_voice_stt_stream_close();
        return config_err;
    }

    memset(result, 0, sizeof(*result));

    const char *active_name = s_stream.faculty_name[0] != '\0' ? s_stream.faculty_name : ASTROLABE_FACULTY_DEFAULT_FACULTY_NAME;
    const char *active_slug = s_stream.faculty_slug[0] != '\0' ? s_stream.faculty_slug : ASTROLABE_FACULTY_DEFAULT_FACULTY_SLUG;

    const size_t min_bytes = ((FACULTY175_LISTEN_MIN_MS * 16000) / 1000) * sizeof(int16_t);
    if (s_stream.pcm_bytes < min_bytes || s_stream.meta_json == NULL) {
        FACULTY175_LOG_STAGE_W(TAG, "stream", "too short pcm=%uB — cancelled", (unsigned)s_stream.pcm_bytes);
        faculty175_voice_stt_stream_close();
        return ESP_ERR_INVALID_SIZE;
    }

    if (!faculty175_voice_heap_ready("voice-finish")) {
        faculty175_voice_stt_stream_close();
        return ESP_ERR_NO_MEM;
    }
    if (s_stream.spool != NULL) {
        size_t flushed = 0;
        while (flushed < s_stream.pcm_bytes) {
            const size_t remaining = s_stream.pcm_bytes - flushed;
            const size_t chunk = remaining > 4096u ? 4096u : remaining;
            if (fwrite(s_stream.pcm_buf + flushed, 1, chunk, s_stream.spool) != chunk) {
                faculty175_voice_stt_stream_close();
                return ESP_FAIL;
            }
            flushed += chunk;
            vTaskDelay(1);
        }
        if (fflush(s_stream.spool) != 0) {
            faculty175_voice_stt_stream_close();
            return ESP_FAIL;
        }
        fclose(s_stream.spool);
        s_stream.spool = NULL;
        free(s_stream.pcm_buf);
        s_stream.pcm_buf = NULL;
        free(s_stream.spool_buf);
        s_stream.spool_buf = NULL;
    }

    char url[224];
    voice_pipeline_preferred_url(url, sizeof(url));
    const uint32_t t0 = faculty175_log_ms();
    FACULTY175_LOG_STAGE(TAG, "stream", "POST %s pcm=%uB from flash faculty=%s (%s) heap=%u/%u psram=%u", url,
                   (unsigned)s_stream.pcm_bytes, active_name, active_slug, (unsigned)voice_internal_free(),
                   (unsigned)voice_internal_largest(), (unsigned)voice_psram_free());

    int status = 0;
    esp_err_t ret = post_collect_voice_file(url, s_stream.meta_json, s_stream.meta_len, s_stream.spool_path,
                                            s_stream.pcm_bytes, tts_stream, &status, result);
    if (voice_should_try_http_fallback(ret, status)) {
        char fallback_url[224];
        if (voice_pipeline_http_fallback_url(fallback_url, sizeof(fallback_url), url)) {
            FACULTY175_LOG_STAGE_W(TAG, "stream", "HTTP fallback POST %s", fallback_url);
            memset(result, 0, sizeof(*result));
            status = 0;
            ret = post_collect_voice_file(fallback_url, s_stream.meta_json, s_stream.meta_len, s_stream.spool_path,
                                          s_stream.pcm_bytes, tts_stream, &status, result);
        }
    }
    faculty175_voice_stt_stream_close();

    const uint32_t http_ms = faculty175_log_ms() - t0;
    FACULTY175_LOG_STAGE(TAG, "pipeline", "HTTP %d mp3=%uB in %ums err=%s", status, (unsigned)result->mp3_len,
                   (unsigned)http_ms, esp_err_to_name(ret));
    if (ret != ESP_OK || result->mp3_path[0] == '\0' || result->mp3_len < 64) {
        return ESP_FAIL;
    }

    char clip[120];
    if (result->transcript[0] != '\0') {
        faculty175_log_clip(clip, sizeof(clip), result->transcript, 96);
        FACULTY175_LOG_STAGE(TAG, "stt", "transcript=\"%s\"", clip);
    }
    if (result->route[0] != '\0') {
        FACULTY175_LOG_STAGE(TAG, "llm", "route=%s", result->route);
    }
    if (result->reply[0] != '\0') {
        faculty175_log_clip(clip, sizeof(clip), result->reply, 96);
        FACULTY175_LOG_STAGE(TAG, "llm", "reply=\"%s\"", clip);
    }
    FACULTY175_LOG_STAGE(TAG, "pipeline", "turn complete in %ums", (unsigned)(faculty175_log_ms() - t0));
    return ESP_OK;
}

esp_err_t faculty175_voice_stt_stream_commit(faculty175_voice_result_t *result)
{
    const faculty175_voice_tts_stream_t stream = {
        .spool_path = VOICE_TTS_PATH,
    };
    return faculty175_voice_stt_stream_commit_streaming(&stream, result);
}

void faculty175_voice_stt_stream_close(void)
{
    stream_buffer_reset();
}

esp_err_t faculty175_voice_stream_begin(const char *faculty_slug, const char *faculty_name, const char *history)
{
    const faculty175_voice_stt_stream_config_t config = {
        .faculty_slug = faculty_slug,
        .faculty_name = faculty_name,
        .history = history,
    };
    return faculty175_voice_stt_stream_open(&config);
}

esp_err_t faculty175_voice_stream_write(const int16_t *pcm, size_t sample_count)
{
    return faculty175_voice_stt_stream_write(pcm, sample_count);
}

esp_err_t faculty175_voice_stream_finish(const char *faculty_slug,
                                   const char *faculty_name,
                                   faculty175_voice_result_t *result)
{
    (void)faculty_slug;
    (void)faculty_name;
    return faculty175_voice_stt_stream_commit(result);
}

esp_err_t faculty175_voice_play_mp3(const uint8_t *mp3, size_t mp3_len)
{
    if (mp3 == NULL || mp3_len < 64) {
        FACULTY175_LOG_STAGE_W(TAG, "tts", "play skipped — mp3 too small (%uB)", (unsigned)mp3_len);
        return ESP_ERR_INVALID_ARG;
    }

    const uint32_t t0 = faculty175_log_ms();
    FACULTY175_LOG_STAGE(TAG, "tts", "play start mp3=%uB", (unsigned)mp3_len);
    s_tts_playback_busy = true;
    (void)faculty175_audio_reset_speaker(1000);

    esp_err_t result = ESP_OK;
    mp3dec_frame_info_t info;
    int16_t *pcm = s_voice_mp3_pcm;

    mp3dec_t *dec = &s_voice_mp3_dec;
    mp3dec_init(dec);
    size_t offset = 0;
    bool configured = false;
    uint32_t playback_rate_hz = 0;
    uint32_t frame_index = 0;
    while (offset < mp3_len) {
        memset(&info, 0, sizeof(info));
        const int samples = mp3dec_decode_frame(dec, mp3 + offset, (int)(mp3_len - offset), pcm, &info);
        if (info.frame_bytes <= 0) {
            break;
        }
        offset += (size_t)info.frame_bytes;
        if (samples <= 0 || info.hz <= 0) {
            continue;
        }
        if (playback_rate_hz != (uint32_t)info.hz) {
            result = voice_set_decoder_playback_rate((uint32_t)info.hz, &playback_rate_hz);
            if (result != ESP_OK) {
                break;
            }
        }
        if (!configured) {
            configured = true;
            FACULTY175_LOG_STAGE(TAG, "tts", "decoder %u Hz ch=%d playback_rate=%u",
                                  (unsigned)info.hz,
                                  info.channels,
                                  (unsigned)playback_rate_hz);
        }
        ++frame_index;
        const uint32_t write_start_ms = faculty175_log_ms();
        if (info.channels == 1) {
            int16_t *stereo = pcm;
            for (int i = samples - 1; i >= 0; --i) {
                stereo[i * 2] = pcm[i];
                stereo[i * 2 + 1] = pcm[i];
            }
            FACULTY175_LOG_STAGE(TAG,
                                 "tts",
                                 "play frame=%u off=%u/%u samples=%d ch=1",
                                 (unsigned)frame_index,
                                 (unsigned)offset,
                                 (unsigned)mp3_len,
                                 samples);
            const esp_err_t write_err = faculty175_audio_write_pcm(stereo, (size_t)samples * 2, 1000);
            if (write_err != ESP_OK) {
                FACULTY175_LOG_STAGE_W(TAG,
                                       "tts",
                                       "play frame=%u write failed: %s",
                                       (unsigned)frame_index,
                                       esp_err_to_name(write_err));
                result = write_err;
                break;
            }
        } else {
            FACULTY175_LOG_STAGE(TAG,
                                 "tts",
                                 "play frame=%u off=%u/%u samples=%d ch=%d",
                                 (unsigned)frame_index,
                                 (unsigned)offset,
                                 (unsigned)mp3_len,
                                 samples,
                                 info.channels);
            const esp_err_t write_err = faculty175_audio_write_pcm(pcm, (size_t)samples * 2, 1000);
            if (write_err != ESP_OK) {
                FACULTY175_LOG_STAGE_W(TAG,
                                       "tts",
                                       "play frame=%u write failed: %s",
                                       (unsigned)frame_index,
                                       esp_err_to_name(write_err));
                result = write_err;
                break;
            }
        }
        FACULTY175_LOG_STAGE(TAG,
                             "tts",
                             "play frame=%u write_ms=%u",
                             (unsigned)frame_index,
                             (unsigned)(faculty175_log_ms() - write_start_ms));
        vTaskDelay(1);
    }

    if (result == ESP_OK && !configured) {
        FACULTY175_LOG_STAGE_W(TAG, "tts", "play failed: no MP3 frames decoded");
        result = ESP_ERR_INVALID_RESPONSE;
    }
    const esp_err_t restore_err = voice_restore_capture_rate(playback_rate_hz);
    if (result == ESP_OK && restore_err != ESP_OK) {
        result = restore_err;
    }
    FACULTY175_LOG_STAGE(TAG,
                         "tts",
                         "play done in %ums err=%s",
                         (unsigned)(faculty175_log_ms() - t0),
                         esp_err_to_name(result));
    s_tts_playback_busy = false;
    return result;
}

bool faculty175_voice_tts_playback_busy(void)
{
    return s_tts_playback_busy;
}

esp_err_t faculty175_voice_play_mp3_file_sync(const char *path, size_t mp3_len)
{
    if (path == NULL || path[0] == '\0' || mp3_len < 64) {
        FACULTY175_LOG_STAGE_W(TAG, "tts", "file play skipped path=%s len=%u", path ? path : "-", (unsigned)mp3_len);
        return ESP_ERR_INVALID_ARG;
    }
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    uint8_t *in = s_voice_mp3_stream_buf;
    int16_t *pcm = s_voice_mp3_pcm;
    /* SPIFFS may disable the external-memory cache while filling a read
     * buffer. Never give fread() a PSRAM destination: use a small internal
     * bounce buffer, then copy into the larger PSRAM decode window after the
     * flash operation has returned and caches are available again. */
    uint8_t *flash_read = heap_caps_malloc(VOICE_FLASH_READ_BOUNCE_BYTES,
                                           MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (flash_read == NULL) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }

    const uint32_t t0 = faculty175_log_ms();
    FACULTY175_LOG_STAGE(TAG, "tts", "play file start mp3=%uB path=%s", (unsigned)mp3_len, path);
    s_tts_playback_busy = true;
    (void)faculty175_audio_reset_speaker(1000);

    mp3dec_t *dec = &s_voice_mp3_dec;
    mp3dec_init(dec);
    mp3dec_frame_info_t info;
    size_t len = 0;
    size_t off = 0;
    bool eof = false;
    bool configured = false;
    uint32_t playback_rate_hz = 0;
    esp_err_t result = ESP_OK;
    while (true) {
        if (!eof && (len - off) < 4096) {
            if (off > 0 && off < len) {
                memmove(in, in + off, len - off);
                len -= off;
                off = 0;
            } else if (off >= len) {
                len = 0;
                off = 0;
            }
            const size_t room = VOICE_MP3_STREAM_BUFFER_BYTES - len;
            const size_t request = room < VOICE_FLASH_READ_BOUNCE_BYTES
                                       ? room
                                       : VOICE_FLASH_READ_BOUNCE_BYTES;
            const size_t n = fread(flash_read, 1, request, f);
            if (n > 0) {
                memcpy(in + len, flash_read, n);
            }
            len += n;
            if (n < request) {
                eof = true;
            }
        }
        if (off >= len) {
            break;
        }
        memset(&info, 0, sizeof(info));
        const int samples = mp3dec_decode_frame(dec, in + off, (int)(len - off), pcm, &info);
        if (info.frame_bytes <= 0) {
            if (eof) {
                break;
            }
            if (off == 0 && len == VOICE_MP3_STREAM_BUFFER_BYTES) {
                off = 1;
            }
            continue;
        }
        off += (size_t)info.frame_bytes;
        if (samples <= 0 || info.hz <= 0) {
            continue;
        }
        if (playback_rate_hz != (uint32_t)info.hz) {
            result = voice_set_decoder_playback_rate((uint32_t)info.hz, &playback_rate_hz);
            if (result != ESP_OK) {
                break;
            }
        }
        if (!configured) {
            configured = true;
            FACULTY175_LOG_STAGE(TAG, "tts", "decoder %u Hz ch=%d playback_rate=%u",
                                  (unsigned)info.hz,
                                  info.channels,
                                  (unsigned)playback_rate_hz);
        }
        if (info.channels == 1) {
            /* minimp3 writes mono samples at the beginning of this 2x buffer.
             * Expand backward in place; offsetting to the second half would
             * write 2x samples beyond the allocation and corrupt PSRAM heap
             * metadata during the mic-to-speaker transition. */
            int16_t *stereo = pcm;
            for (int i = samples - 1; i >= 0; --i) {
                stereo[i * 2] = pcm[i];
                stereo[i * 2 + 1] = pcm[i];
            }
            const esp_err_t write_err = faculty175_audio_write_pcm(stereo, (size_t)samples * 2, 1000);
            if (write_err != ESP_OK) {
                result = write_err;
                break;
            }
        } else {
            const esp_err_t write_err = faculty175_audio_write_pcm(pcm, (size_t)samples * 2, 1000);
            if (write_err != ESP_OK) {
                result = write_err;
                break;
            }
        }
        vTaskDelay(1);
    }

    fclose(f);
    free(flash_read);
    if (result == ESP_OK && !configured) {
        result = ESP_ERR_INVALID_RESPONSE;
    }
    const esp_err_t restore_err = voice_restore_capture_rate(playback_rate_hz);
    if (result == ESP_OK && restore_err != ESP_OK) {
        result = restore_err;
    }
    s_tts_playback_busy = false;
    FACULTY175_LOG_STAGE(TAG,
                         "tts",
                         "play file done in %ums err=%s",
                         (unsigned)(faculty175_log_ms() - t0),
                         esp_err_to_name(result));
    return result;
}

typedef struct {
    char path[64];
    uint8_t *mp3;
    size_t mp3_len;
    volatile TaskHandle_t waiter;
    volatile bool caller_owns_args;
    esp_err_t result;
} voice_play_file_task_args_t;

static void voice_play_mp3_file_task(void *arg)
{
    voice_play_file_task_args_t *args = (voice_play_file_task_args_t *)arg;
    if (args != NULL) {
        if (args->path[0] != '\0') {
            args->result = faculty175_voice_play_mp3_file_sync(args->path, args->mp3_len);
        } else {
            args->result = faculty175_voice_play_mp3(args->mp3, args->mp3_len);
        }
        free(args->mp3);
        args->mp3 = NULL;
        TaskHandle_t waiter = args->waiter;
        if (waiter != NULL) {
            xTaskNotifyGive(waiter);
        }
        if (!args->caller_owns_args) {
            free(args);
        }
    }
    vTaskDeleteWithCaps(NULL);
}

static esp_err_t voice_play_mp3_async(const uint8_t *mp3, size_t mp3_len)
{
    if (mp3 == NULL || mp3_len < 64 || mp3_len > TTS_MP3_MAX_BYTES) {
        return ESP_ERR_INVALID_ARG;
    }
    voice_play_file_task_args_t *args = heap_caps_calloc(1, sizeof(*args), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (args == NULL) {
        args = calloc(1, sizeof(*args));
    }
    if (args == NULL) {
        return ESP_ERR_NO_MEM;
    }
    args->waiter = xTaskGetCurrentTaskHandle();
    args->caller_owns_args = true;
    args->result = ESP_FAIL;

    args->mp3 = heap_caps_malloc(mp3_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (args->mp3 == NULL) {
        free(args);
        return ESP_ERR_NO_MEM;
    }
    memcpy(args->mp3, mp3, mp3_len);
    args->mp3_len = mp3_len;

    const BaseType_t ok = xTaskCreateWithCaps(voice_play_mp3_file_task,
                                              "voice_play",
                                              24576,
                                              args,
                                              6,
                                              NULL,
                                              MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ok != pdPASS) {
        free(args->mp3);
        free(args);
        return ESP_ERR_NO_MEM;
    }

    /* OpenAI speech is commonly about 24 kbit/s (roughly 3 bytes/ms). A fixed
     * 30 second deadline incorrectly times out longer but valid answers. */
    uint32_t timeout_ms = 15000u + (uint32_t)(mp3_len / 3u);
    if (timeout_ms < VOICE_PLAY_TASK_TIMEOUT_MS) {
        timeout_ms = VOICE_PLAY_TASK_TIMEOUT_MS;
    } else if (timeout_ms > 180000u) {
        timeout_ms = 180000u;
    }
    if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(timeout_ms)) == 0) {
        args->waiter = NULL;
        args->caller_owns_args = false;
        FACULTY175_LOG_STAGE_W(TAG,
                               "tts",
                               "play timeout after %ums mp3=%uB",
                               (unsigned)timeout_ms,
                               (unsigned)mp3_len);
        return ESP_ERR_TIMEOUT;
    }
    const esp_err_t result = args->result;
    free(args);
    return result;
}

esp_err_t faculty175_voice_play_mp3_async(const uint8_t *mp3, size_t mp3_len)
{
    return voice_play_mp3_async(mp3, mp3_len);
}

esp_err_t faculty175_voice_play_mp3_file(const char *path, size_t mp3_len)
{
    if (path == NULL || path[0] == '\0' || mp3_len < 64 || mp3_len > TTS_MP3_MAX_BYTES) {
        return ESP_ERR_INVALID_ARG;
    }
    voice_play_file_task_args_t *args = heap_caps_calloc(1, sizeof(*args), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (args == NULL) {
        args = calloc(1, sizeof(*args));
    }
    if (args == NULL) {
        return ESP_ERR_NO_MEM;
    }
    strlcpy(args->path, path, sizeof(args->path));
    args->mp3_len = mp3_len;
    args->waiter = xTaskGetCurrentTaskHandle();
    args->caller_owns_args = true;
    args->result = ESP_FAIL;

    const BaseType_t ok = xTaskCreateWithCaps(voice_play_mp3_file_task,
                                              "voice_file_play",
                                              16384,
                                              args,
                                              6,
                                              NULL,
                                              MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (ok != pdPASS) {
        free(args);
        return ESP_ERR_NO_MEM;
    }
    uint32_t timeout_ms = 15000u + (uint32_t)(mp3_len / 3u);
    if (timeout_ms < VOICE_PLAY_TASK_TIMEOUT_MS) {
        timeout_ms = VOICE_PLAY_TASK_TIMEOUT_MS;
    } else if (timeout_ms > 180000u) {
        timeout_ms = 180000u;
    }
    if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(timeout_ms)) == 0) {
        args->waiter = NULL;
        args->caller_owns_args = false;
        FACULTY175_LOG_STAGE_W(TAG,
                               "tts",
                               "file play timeout after %ums path=%s len=%u",
                               (unsigned)timeout_ms,
                               path,
                               (unsigned)mp3_len);
        return ESP_ERR_TIMEOUT;
    }
    const esp_err_t result = args->result;
    free(args);
    return result;
}

static bool tts_stream_decode_available(bool eof)
{
    uint8_t *in = s_voice_mp3_stream_buf;
    int16_t *pcm = s_voice_mp3_pcm;
    bool progressed = false;

    if (s_tts_speaker_stream.err != ESP_OK) {
        return false;
    }
    while (s_tts_speaker_stream.off < s_tts_speaker_stream.len) {
        mp3dec_frame_info_t info = {};
        const int samples = mp3dec_decode_frame(&s_voice_mp3_dec,
                                                in + s_tts_speaker_stream.off,
                                                (int)(s_tts_speaker_stream.len - s_tts_speaker_stream.off),
                                                pcm,
                                                &info);
        if (info.frame_bytes <= 0) {
            if (!eof) {
                break;
            }
            ++s_tts_speaker_stream.off;
            progressed = true;
            continue;
        }
        s_tts_speaker_stream.off += (size_t)info.frame_bytes;
        progressed = true;
        if (samples <= 0 || info.hz <= 0) {
            continue;
        }
        if (s_tts_speaker_stream.playback_rate_hz != (uint32_t)info.hz) {
            s_tts_speaker_stream.err =
                voice_set_decoder_playback_rate((uint32_t)info.hz,
                                                &s_tts_speaker_stream.playback_rate_hz);
            if (s_tts_speaker_stream.err != ESP_OK) {
                break;
            }
        }
        if (!s_tts_speaker_stream.configured) {
            s_tts_speaker_stream.configured = true;
            FACULTY175_LOG_STAGE(TAG,
                                 "tts",
                                 "stream decoder %u Hz ch=%d playback_rate=%u",
                                 (unsigned)info.hz,
                                 info.channels,
                                 (unsigned)s_tts_speaker_stream.playback_rate_hz);
        }
        if (info.channels == 1) {
            int16_t *stereo = pcm;
            for (int i = samples - 1; i >= 0; --i) {
                stereo[i * 2] = pcm[i];
                stereo[i * 2 + 1] = pcm[i];
            }
            s_tts_speaker_stream.err = faculty175_audio_write_pcm(stereo, (size_t)samples * 2, 1000);
        } else {
            s_tts_speaker_stream.err = faculty175_audio_write_pcm(pcm, (size_t)samples * 2, 1000);
        }
        if (s_tts_speaker_stream.err != ESP_OK) {
            FACULTY175_LOG_STAGE_W(TAG,
                                   "tts",
                                   "stream write failed after %u frames: %s",
                                   (unsigned)s_tts_speaker_stream.decoded_frames,
                                   esp_err_to_name(s_tts_speaker_stream.err));
            break;
        }
        ++s_tts_speaker_stream.decoded_frames;
        vTaskDelay(1);
    }

    if (s_tts_speaker_stream.off > 0) {
        if (s_tts_speaker_stream.off < s_tts_speaker_stream.len) {
            memmove(in,
                    in + s_tts_speaker_stream.off,
                    s_tts_speaker_stream.len - s_tts_speaker_stream.off);
            s_tts_speaker_stream.len -= s_tts_speaker_stream.off;
        } else {
            s_tts_speaker_stream.len = 0;
        }
        s_tts_speaker_stream.off = 0;
    }
    return progressed;
}

esp_err_t faculty175_voice_tts_speaker_stream_begin(void)
{
    memset(&s_tts_speaker_stream, 0, sizeof(s_tts_speaker_stream));
    s_tts_speaker_stream.err = ESP_OK;
    mp3dec_init(&s_voice_mp3_dec);
    s_tts_playback_busy = true;
    (void)faculty175_audio_reset_speaker(1000);
    faculty175_audio_set_speaker_mute(false);
    s_tts_speaker_stream.open = true;
    s_tts_speaker_stream.start_ms = faculty175_log_ms();
    FACULTY175_LOG_STAGE(TAG, "tts", "speaker stream begin");
    return ESP_OK;
}

esp_err_t faculty175_voice_tts_speaker_stream_write(const uint8_t *mp3_chunk, size_t chunk_len)
{
    if (!s_tts_speaker_stream.open || mp3_chunk == NULL || chunk_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_tts_speaker_stream.err != ESP_OK) {
        return s_tts_speaker_stream.err;
    }
    s_tts_speaker_stream.bytes_in += chunk_len;
    size_t copied = 0;
    while (copied < chunk_len) {
        if (s_tts_speaker_stream.len == VOICE_MP3_STREAM_BUFFER_BYTES) {
            const bool progressed = tts_stream_decode_available(false);
            if (s_tts_speaker_stream.err != ESP_OK) {
                return s_tts_speaker_stream.err;
            }
            if (!progressed && s_tts_speaker_stream.len == VOICE_MP3_STREAM_BUFFER_BYTES) {
                FACULTY175_LOG_STAGE_W(TAG, "tts", "stream decoder stalled with full buffer");
                s_tts_speaker_stream.err = ESP_ERR_INVALID_RESPONSE;
                return s_tts_speaker_stream.err;
            }
        }
        const size_t room = VOICE_MP3_STREAM_BUFFER_BYTES - s_tts_speaker_stream.len;
        const size_t remain = chunk_len - copied;
        const size_t n = remain < room ? remain : room;
        memcpy(s_voice_mp3_stream_buf + s_tts_speaker_stream.len, mp3_chunk + copied, n);
        s_tts_speaker_stream.len += n;
        copied += n;
        (void)tts_stream_decode_available(false);
        if (s_tts_speaker_stream.err != ESP_OK) {
            return s_tts_speaker_stream.err;
        }
    }
    return ESP_OK;
}

esp_err_t faculty175_voice_tts_speaker_stream_end(void)
{
    if (!s_tts_speaker_stream.open) {
        return ESP_ERR_INVALID_STATE;
    }
    while (s_tts_speaker_stream.len > 0) {
        const bool progressed = tts_stream_decode_available(true);
        if (s_tts_speaker_stream.err != ESP_OK) {
            break;
        }
        if (!progressed) {
            break;
        }
    }
    esp_err_t result = s_tts_speaker_stream.err;
    if (result == ESP_OK && s_tts_speaker_stream.decoded_frames == 0) {
        result = ESP_ERR_INVALID_RESPONSE;
    }
    s_tts_speaker_stream.open = false;
    const esp_err_t restore_err =
        voice_restore_capture_rate(s_tts_speaker_stream.playback_rate_hz);
    if (result == ESP_OK && restore_err != ESP_OK) {
        result = restore_err;
    }
    s_tts_playback_busy = false;
    FACULTY175_LOG_STAGE(TAG,
                         "tts",
                         "speaker stream end in %ums bytes=%u frames=%u err=%s",
                         (unsigned)(faculty175_log_ms() - s_tts_speaker_stream.start_ms),
                         (unsigned)s_tts_speaker_stream.bytes_in,
                         (unsigned)s_tts_speaker_stream.decoded_frames,
                         esp_err_to_name(result));
    return result;
}
