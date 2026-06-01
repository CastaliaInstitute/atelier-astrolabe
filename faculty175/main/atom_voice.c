#include "atom_voice.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

#include "astrolabe_faculty175_face.h"
#include "atom_device_auth.h"
#include "atom_listen.h"
#include "faculty175_board.h"
#include "atom_log.h"

#if __has_include("secrets.local.h")
#include "secrets.local.h"
#else
#include "secrets.example.h"
#endif

static const char *TAG = "atom_voice";
#define HTTP_TIMEOUT_MS 660000
#define VOICE_RESP_MAX_BYTES (768 * 1024)
#define TTS_MP3_MAX_BYTES (384 * 1024)
#define VOICE_STREAM_CT "application/vnd.astrolabe.voice-stream"
#define VOICE_SPOOL_BASE "/voice"
#define VOICE_SPOOL_PARTITION "voice_spool"
#define VOICE_SPOOL_PATH VOICE_SPOOL_BASE "/voice-turn.pcm"
#define VOICE_TTS_PATH VOICE_SPOOL_BASE "/voice-reply.mp3"
#define VOICE_HEAP_MIN_INTERNAL_FREE (112 * 1024)
#define VOICE_HEAP_MIN_INTERNAL_LARGEST (36 * 1024)
#define VOICE_STREAM_CHUNK_BYTES 2048
#define VOICE_MP3_STREAM_BUFFER_BYTES (24 * 1024)

typedef struct {
    bool open;
    size_t pcm_bytes;
    char *meta_json;
    size_t meta_len;
    FILE *spool;
    char spool_path[64];
} atom_voice_stream_t;

static atom_voice_stream_t s_stream = {};

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

bool atom_voice_heap_ready(const char *stage)
{
    const uint32_t free_i = voice_internal_free();
    const uint32_t largest_i = voice_internal_largest();
    if (free_i < VOICE_HEAP_MIN_INTERNAL_FREE || largest_i < VOICE_HEAP_MIN_INTERNAL_LARGEST) {
        ATOM_LOG_STAGE_W(TAG, "heap", "%s low heap internal=%u largest=%u psram=%u need=%u/%u",
                         stage != NULL ? stage : "voice", (unsigned)free_i, (unsigned)largest_i,
                         (unsigned)voice_psram_free(), VOICE_HEAP_MIN_INTERNAL_FREE,
                         VOICE_HEAP_MIN_INTERNAL_LARGEST);
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
        const size_t need = (size_t)ATOM_LISTEN_MAX_SECONDS * 16000 * sizeof(int16_t) + TTS_MP3_MAX_BYTES + 16384;
        if (esp_spiffs_info(VOICE_SPOOL_PARTITION, &total, &used) == ESP_OK && total >= used) {
            const size_t free_bytes = total - used;
            if (free_bytes < need) {
                ATOM_LOG_STAGE_W(TAG, "stream", "SPIFFS low space free=%u need=%u",
                                 (unsigned)free_bytes, (unsigned)need);
                return ESP_ERR_NO_MEM;
            }
            return ESP_OK;
        }
        return ESP_OK;
    }
    ATOM_LOG_STAGE_W(TAG, "stream", "SPIFFS unavailable for voice spool: %s", esp_err_to_name(err));
    return err;
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
    for (const unsigned char *p = (const unsigned char *)src; *p != '\0'; ++p) {
        if (w + 2 >= cap) {
            break;
        }
        if (*p == '\"' || *p == '\\') {
            out[w++] = '\\';
        }
        out[w++] = (char)*p;
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

void atom_voice_result_free(atom_voice_result_t *result)
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
        unlink(result->mp3_path);
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

static void capture_voice_response_headers(esp_http_client_handle_t client, atom_voice_result_t *result)
{
    if (client == NULL || result == NULL) {
        return;
    }
    char *header = NULL;
    if (esp_http_client_get_header(client, "X-Voice-Route", &header) == ESP_OK && header != NULL && header[0] != '\0') {
        strlcpy(result->route, header, sizeof(result->route));
        url_decode_in_place(result->route);
    }
    header = NULL;
    if (esp_http_client_get_header(client, "X-Faculty-Slug", &header) == ESP_OK && header != NULL && header[0] != '\0' &&
        result->faculty_slug[0] == '\0') {
        strlcpy(result->faculty_slug, header, sizeof(result->faculty_slug));
        url_decode_in_place(result->faculty_slug);
    }
    header = NULL;
    if (esp_http_client_get_header(client, "X-Faculty-Name", &header) == ESP_OK && header != NULL && header[0] != '\0' &&
        result->faculty_name[0] == '\0') {
        strlcpy(result->faculty_name, header, sizeof(result->faculty_name));
        url_decode_in_place(result->faculty_name);
    }
    header = NULL;
    if (esp_http_client_get_header(client, "X-Voice-Transcript", &header) == ESP_OK && header != NULL &&
        header[0] != '\0' && result->transcript[0] == '\0') {
        strlcpy(result->transcript, header, sizeof(result->transcript));
        url_decode_in_place(result->transcript);
    }
    header = NULL;
    if (esp_http_client_get_header(client, "X-Voice-Reply", &header) == ESP_OK && header != NULL && header[0] != '\0' &&
        result->reply[0] == '\0') {
        strlcpy(result->reply, header, sizeof(result->reply));
        url_decode_in_place(result->reply);
    }
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
    if (s_stream.spool_path[0] != '\0') {
        unlink(s_stream.spool_path);
    }
    free(s_stream.meta_json);
    memset(&s_stream, 0, sizeof(s_stream));
}

static char *build_faculty_request_json(const char *faculty_slug,
                                        const char *faculty_name,
                                        const char *history,
                                        size_t *out_len)
{
    const char *active_slug =
        (faculty_slug != NULL && faculty_slug[0] != '\0') ? faculty_slug : ASTROLABE_FACULTY_DEFAULT_FACULTY_SLUG;
    const char *active_name =
        (faculty_name != NULL && faculty_name[0] != '\0') ? faculty_name : ASTROLABE_FACULTY_DEFAULT_FACULTY_NAME;

    char sys[896];
    snprintf(sys, sizeof(sys),
             "%s If the user does not name a faculty member, continue with the active faculty (%s, %s). "
             "Conversation history: %s",
             ASTROLABE_FACULTY_SYSTEM_INSTRUCTION, active_name, active_slug,
             history != NULL && history[0] != '\0' ? history : "(none yet)");

    char *esc_sys = json_escape_alloc(sys);
    char *esc_slug = json_escape_alloc(active_slug);
    char *esc_name = json_escape_alloc(active_name);
    if (esc_sys == NULL || esc_slug == NULL || esc_name == NULL) {
        free(esc_sys);
        free(esc_slug);
        free(esc_name);
        return NULL;
    }

    const size_t cap = strlen(esc_sys) + strlen(esc_slug) + strlen(esc_name) + 256;
    char *json = heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (json == NULL) {
        json = malloc(cap);
    }
    if (json == NULL) {
        free(esc_sys);
        free(esc_slug);
        free(esc_name);
        return NULL;
    }

    const int json_len = snprintf(json, cap,
                                  "{\"languageCode\":\"en-US\",\"sampleRateHertz\":16000,"
                                  "\"face\":\"%s\",\"facultySlug\":\"%s\",\"facultyName\":\"%s\","
                                  "\"systemInstruction\":\"%s\"}",
                                  ASTROLABE_FACULTY_FACE_NAME, esc_slug, esc_name, esc_sys);
    free(esc_sys);
    free(esc_slug);
    free(esc_name);
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
            atom_log_clip(clipped, sizeof(clipped), err_body, 80);
            ATOM_LOG_STAGE_W(TAG, "pipeline", "HTTP %d err=%s", status, clipped);
        } else {
            ATOM_LOG_STAGE_W(TAG, "pipeline", "HTTP %d (no body)", status);
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
                                        size_t *out_len)
{
    if (out_len != NULL) {
        *out_len = 0;
    }
    if (path == NULL || path[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (status < 200 || status >= 300) {
        char err_body[160] = {};
        const int err_read = esp_http_client_read(client, err_body, (int)sizeof(err_body) - 1);
        if (err_read > 0) {
            err_body[err_read] = '\0';
            char clipped[96];
            atom_log_clip(clipped, sizeof(clipped), err_body, 80);
            ATOM_LOG_STAGE_W(TAG, "pipeline", "HTTP %d err=%s", status, clipped);
        } else {
            ATOM_LOG_STAGE_W(TAG, "pipeline", "HTTP %d (no body)", status);
        }
        return ESP_FAIL;
    }

    unlink(path);
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        return ESP_FAIL;
    }
    uint8_t chunk[VOICE_STREAM_CHUNK_BYTES];
    size_t total = 0;
    while (true) {
        const int read = esp_http_client_read(client, (char *)chunk, (int)sizeof(chunk));
        if (read < 0) {
            fclose(f);
            unlink(path);
            return ESP_FAIL;
        }
        if (read == 0) {
            break;
        }
        if (fwrite(chunk, 1, (size_t)read, f) != (size_t)read) {
            fclose(f);
            unlink(path);
            return ESP_FAIL;
        }
        total += (size_t)read;
        if (total > TTS_MP3_MAX_BYTES) {
            fclose(f);
            unlink(path);
            ATOM_LOG_STAGE_W(TAG, "tts", "MP3 response too large: %uB max=%u",
                             (unsigned)total, (unsigned)TTS_MP3_MAX_BYTES);
            return ESP_ERR_INVALID_SIZE;
        }
        vTaskDelay(1);
    }
    fclose(f);
    if (total < 64) {
        unlink(path);
        return ESP_ERR_INVALID_SIZE;
    }
    if (out_len != NULL) {
        *out_len = total;
    }
    return ESP_OK;
}

static esp_err_t parse_voice_response_body(uint8_t *response,
                                           size_t response_len,
                                           atom_voice_result_t *result,
                                           uint32_t t0)
{
    if (response == NULL || response_len < 8 || result == NULL) {
        return ESP_FAIL;
    }

    if (response_len > 0 && response[0] != '{') {
        ATOM_LOG_STAGE(TAG, "tts", "raw MP3 response %uB (no JSON wrapper)", (unsigned)response_len);
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
        atom_log_clip(clip, sizeof(clip), result->transcript, 96);
        ATOM_LOG_STAGE(TAG, "stt", "transcript=\"%s\"", clip);
    } else {
        ATOM_LOG_STAGE_W(TAG, "stt", "empty transcript");
    }
    if (result->route[0] != '\0') {
        ATOM_LOG_STAGE(TAG, "llm", "route=%s", result->route);
    }
    if (result->faculty_slug[0] != '\0' || result->faculty_name[0] != '\0') {
        ATOM_LOG_STAGE(TAG, "llm", "faculty=%s (%s)", result->faculty_name[0] ? result->faculty_name : "?",
                       result->faculty_slug[0] ? result->faculty_slug : "?");
    }
    if (result->reply[0] != '\0') {
        atom_log_clip(clip, sizeof(clip), result->reply, 96);
        ATOM_LOG_STAGE(TAG, "llm", "reply=\"%s\"", clip);
    } else {
        ATOM_LOG_STAGE_W(TAG, "llm", "empty reply text");
    }
    if (result->mp3 != NULL && result->mp3_len > 0) {
        ATOM_LOG_STAGE(TAG, "tts", "mp3=%uB embedded in JSON", (unsigned)result->mp3_len);
    } else {
        ATOM_LOG_STAGE_W(TAG, "tts", "no audioBase64 in response");
    }

    if (result->transcript[0] == '\0' && result->reply[0] == '\0' && result->mp3 == NULL) {
        ATOM_LOG_STAGE_E(TAG, "pipeline", "response missing transcript, reply, and audio");
        return ESP_FAIL;
    }
    ATOM_LOG_STAGE(TAG, "pipeline", "turn complete in %ums", (unsigned)(atom_log_ms() - t0));
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
    (void)atom_device_auth_headers(client);

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
                                         int *out_status,
                                         atom_voice_result_t *result)
{
    if (out_status != NULL) {
        *out_status = 0;
    }
    if (url == NULL || meta_json == NULL || meta_len == 0 || pcm_path == NULL || pcm_len == 0 || result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!atom_voice_heap_ready("voice-post")) {
        return ESP_ERR_NO_MEM;
    }

    FILE *pcm = fopen(pcm_path, "rb");
    if (pcm == NULL) {
        return ESP_ERR_NOT_FOUND;
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
        fclose(pcm);
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", VOICE_STREAM_CT);
    esp_http_client_set_header(client, "Accept", "audio/mpeg");
    http_set_supabase_headers(client);
    (void)atom_device_auth_headers(client);

    const size_t body_len = 4 + meta_len + pcm_len;
    esp_err_t ret = esp_http_client_open(client, body_len);
    if (ret != ESP_OK) {
        fclose(pcm);
        esp_http_client_cleanup(client);
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

    uint8_t chunk[VOICE_STREAM_CHUNK_BYTES];
    while (ret == ESP_OK) {
        const size_t n = fread(chunk, 1, sizeof(chunk), pcm);
        if (n > 0) {
            ret = write_all(client, chunk, n);
        }
        if (n < sizeof(chunk)) {
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
        return ret;
    }

    (void)esp_http_client_fetch_headers(client);
    const int status = esp_http_client_get_status_code(client);
    if (out_status != NULL) {
        *out_status = status;
    }
    if (status >= 200 && status < 300) {
        capture_voice_response_headers(client, result);
    }
    size_t total = 0;
    ret = read_http_body_to_file(client, status, VOICE_TTS_PATH, &total);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    if (ret == ESP_OK) {
        strlcpy(result->mp3_path, VOICE_TTS_PATH, sizeof(result->mp3_path));
        result->mp3_len = total;
        ATOM_LOG_STAGE(TAG, "tts", "mp3 spooled %uB -> %s", (unsigned)total, result->mp3_path);
    }
    return ret;
}

esp_err_t atom_voice_post_pcm(const uint8_t *pcm,
                              size_t pcm_len,
                              const char *faculty_slug,
                              const char *faculty_name,
                              const char *history,
                              atom_voice_result_t *result)
{
    if (pcm == NULL || pcm_len == 0 || result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (strlen(MYNAH_SUPABASE_URL) == 0 || strlen(MYNAH_SUPABASE_ANON_KEY) == 0) {
        ESP_LOGW(TAG, "Supabase secrets missing");
        return ESP_ERR_INVALID_STATE;
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
             "Conversation history: %s",
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
    snprintf(url, sizeof(url), "%s/functions/v1/voice-pipeline", MYNAH_SUPABASE_URL);
    const uint32_t t0 = atom_log_ms();
    ATOM_LOG_STAGE(TAG, "pipeline", "POST %s pcm=%uB face=%s faculty=%s (%s)",
                   url, (unsigned)pcm_len, ASTROLABE_FACULTY_FACE_NAME, active_name, active_slug);

    int status = 0;
    uint8_t *response = NULL;
    size_t response_len = 0;
    esp_err_t ret = post_collect_body(url, "application/json", (const uint8_t *)body, (size_t)body_len,
                                      "application/json,audio/mpeg", &response, &response_len, &status);
    free(body);
    const uint32_t http_ms = atom_log_ms() - t0;
    ATOM_LOG_STAGE(TAG, "pipeline", "HTTP %d body=%uB in %ums err=%s", status, (unsigned)response_len, (unsigned)http_ms,
                   esp_err_to_name(ret));
    if (ret != ESP_OK || response == NULL || response_len < 8) {
        free(response);
        return ESP_FAIL;
    }

    ret = parse_voice_response_body(response, response_len, result, t0);
    if (ret != ESP_OK) {
        atom_voice_result_free(result);
    }
    return ret;
}

void atom_voice_stream_cancel(void)
{
    stream_buffer_reset();
}

esp_err_t atom_voice_stream_begin(const char *faculty_slug, const char *faculty_name, const char *history)
{
    if (strlen(MYNAH_SUPABASE_URL) == 0 || strlen(MYNAH_SUPABASE_ANON_KEY) == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!atom_voice_heap_ready("voice-begin")) {
        return ESP_ERR_NO_MEM;
    }
    if (s_stream.open) {
        atom_voice_stream_cancel();
    }
    esp_err_t err = voice_spiffs_mount();
    if (err != ESP_OK) {
        return err;
    }

    size_t meta_len = 0;
    char *meta_json = build_faculty_request_json(faculty_slug, faculty_name, history, &meta_len);
    if (meta_json == NULL || meta_len == 0 || meta_len > 16384) {
        free(meta_json);
        return ESP_ERR_NO_MEM;
    }

    s_stream.meta_json = meta_json;
    s_stream.meta_len = meta_len;
    strlcpy(s_stream.spool_path, VOICE_SPOOL_PATH, sizeof(s_stream.spool_path));
    unlink(s_stream.spool_path);
    s_stream.spool = fopen(s_stream.spool_path, "wb");
    if (s_stream.spool == NULL) {
        stream_buffer_reset();
        return ESP_FAIL;
    }
    s_stream.open = true;
    s_stream.pcm_bytes = 0;
    ATOM_LOG_STAGE(TAG, "stream", "VAD capture started -> %s heap=%u/%u psram=%u", s_stream.spool_path,
                   (unsigned)voice_internal_free(), (unsigned)voice_internal_largest(),
                   (unsigned)voice_psram_free());
    return ESP_OK;
}

esp_err_t atom_voice_stream_write(const int16_t *pcm, size_t sample_count)
{
    if (!s_stream.open || pcm == NULL || sample_count == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_stream.spool == NULL) {
        atom_voice_stream_cancel();
        return ESP_ERR_INVALID_STATE;
    }
    const size_t bytes = sample_count * sizeof(int16_t);
    const size_t written = fwrite(pcm, 1, bytes, s_stream.spool);
    if (written != bytes) {
        atom_voice_stream_cancel();
        return ESP_FAIL;
    }
    s_stream.pcm_bytes += bytes;
    return ESP_OK;
}

esp_err_t atom_voice_stream_finish(const char *faculty_slug,
                                   const char *faculty_name,
                                   atom_voice_result_t *result)
{
    if (!s_stream.open || result == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (strlen(MYNAH_SUPABASE_URL) == 0 || strlen(MYNAH_SUPABASE_ANON_KEY) == 0) {
        atom_voice_stream_cancel();
        return ESP_ERR_INVALID_STATE;
    }

    memset(result, 0, sizeof(*result));

    const char *active_name =
        (faculty_name != NULL && faculty_name[0] != '\0') ? faculty_name : ASTROLABE_FACULTY_DEFAULT_FACULTY_NAME;
    const char *active_slug =
        (faculty_slug != NULL && faculty_slug[0] != '\0') ? faculty_slug : ASTROLABE_FACULTY_DEFAULT_FACULTY_SLUG;

    const size_t min_bytes = ((ATOM_LISTEN_MIN_MS * 16000) / 1000) * sizeof(int16_t);
    if (s_stream.pcm_bytes < min_bytes || s_stream.meta_json == NULL) {
        ATOM_LOG_STAGE_W(TAG, "stream", "too short pcm=%uB — cancelled", (unsigned)s_stream.pcm_bytes);
        atom_voice_stream_cancel();
        return ESP_ERR_INVALID_SIZE;
    }

    if (!atom_voice_heap_ready("voice-finish")) {
        atom_voice_stream_cancel();
        return ESP_ERR_NO_MEM;
    }
    if (s_stream.spool != NULL) {
        if (fflush(s_stream.spool) != 0) {
            atom_voice_stream_cancel();
            return ESP_FAIL;
        }
        fclose(s_stream.spool);
        s_stream.spool = NULL;
    }

    char url[224];
    snprintf(url, sizeof(url), "%s/functions/v1/voice-pipeline", MYNAH_SUPABASE_URL);
    const uint32_t t0 = atom_log_ms();
    ATOM_LOG_STAGE(TAG, "stream", "POST %s pcm=%uB from flash faculty=%s (%s) heap=%u/%u psram=%u", url,
                   (unsigned)s_stream.pcm_bytes, active_name, active_slug, (unsigned)voice_internal_free(),
                   (unsigned)voice_internal_largest(), (unsigned)voice_psram_free());

    int status = 0;
    esp_err_t ret = post_collect_voice_file(url, s_stream.meta_json, s_stream.meta_len, s_stream.spool_path,
                                            s_stream.pcm_bytes, &status, result);
    atom_voice_stream_cancel();

    const uint32_t http_ms = atom_log_ms() - t0;
    ATOM_LOG_STAGE(TAG, "pipeline", "HTTP %d mp3=%uB in %ums err=%s", status, (unsigned)result->mp3_len,
                   (unsigned)http_ms, esp_err_to_name(ret));
    if (ret != ESP_OK || result->mp3_path[0] == '\0' || result->mp3_len < 64) {
        return ESP_FAIL;
    }

    char clip[120];
    if (result->transcript[0] != '\0') {
        atom_log_clip(clip, sizeof(clip), result->transcript, 96);
        ATOM_LOG_STAGE(TAG, "stt", "transcript=\"%s\"", clip);
    }
    if (result->route[0] != '\0') {
        ATOM_LOG_STAGE(TAG, "llm", "route=%s", result->route);
    }
    if (result->reply[0] != '\0') {
        atom_log_clip(clip, sizeof(clip), result->reply, 96);
        ATOM_LOG_STAGE(TAG, "llm", "reply=\"%s\"", clip);
    }
    ATOM_LOG_STAGE(TAG, "pipeline", "turn complete in %ums", (unsigned)(atom_log_ms() - t0));
    return ESP_OK;
}

esp_err_t atom_voice_play_mp3(const uint8_t *mp3, size_t mp3_len)
{
    if (mp3 == NULL || mp3_len < 64) {
        ATOM_LOG_STAGE_W(TAG, "tts", "play skipped — mp3 too small (%uB)", (unsigned)mp3_len);
        return ESP_ERR_INVALID_ARG;
    }

    const uint32_t t0 = atom_log_ms();
    ATOM_LOG_STAGE(TAG, "tts", "play start mp3=%uB", (unsigned)mp3_len);

    mp3dec_t dec;
    mp3dec_frame_info_t info;
    int16_t *pcm = heap_caps_malloc(MINIMP3_MAX_SAMPLES_PER_FRAME * sizeof(int16_t) * 2,
                                    MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (pcm == NULL) {
        pcm = heap_caps_malloc(MINIMP3_MAX_SAMPLES_PER_FRAME * sizeof(int16_t) * 2,
                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (pcm == NULL) {
        return ESP_ERR_NO_MEM;
    }

    mp3dec_init(&dec);
    size_t offset = 0;
    bool configured = false;
    faculty175_audio_set_speaker_mute(false);

    while (offset < mp3_len) {
        memset(&info, 0, sizeof(info));
        const int samples = mp3dec_decode_frame(&dec, mp3 + offset, (int)(mp3_len - offset), pcm, &info);
        if (info.frame_bytes <= 0) {
            break;
        }
        offset += (size_t)info.frame_bytes;
        if (samples <= 0 || info.hz <= 0) {
            continue;
        }
        if (!configured) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(faculty175_audio_set_sample_rate((uint32_t)info.hz));
            configured = true;
            ATOM_LOG_STAGE(TAG, "tts", "decoder %u Hz ch=%d", (unsigned)info.hz, info.channels);
        }
        if (info.channels == 1) {
            int16_t *stereo = pcm + MINIMP3_MAX_SAMPLES_PER_FRAME;
            for (int i = samples - 1; i >= 0; --i) {
                stereo[i * 2] = pcm[i];
                stereo[i * 2 + 1] = pcm[i];
            }
            ESP_ERROR_CHECK_WITHOUT_ABORT(faculty175_audio_write_pcm(stereo, (size_t)samples * 2, 1000));
        } else {
            ESP_ERROR_CHECK_WITHOUT_ABORT(faculty175_audio_write_pcm(pcm, (size_t)samples * 2, 1000));
        }
        vTaskDelay(1);
    }

    free(pcm);
    ESP_ERROR_CHECK_WITHOUT_ABORT(faculty175_audio_set_sample_rate(FACULTY175_AUDIO_RATE));
    ATOM_LOG_STAGE(TAG, "tts", "play done in %ums", (unsigned)(atom_log_ms() - t0));
    return ESP_OK;
}

esp_err_t atom_voice_play_mp3_file(const char *path, size_t mp3_len)
{
    if (path == NULL || path[0] == '\0' || mp3_len < 64) {
        ATOM_LOG_STAGE_W(TAG, "tts", "file play skipped path=%s len=%u", path ? path : "-", (unsigned)mp3_len);
        return ESP_ERR_INVALID_ARG;
    }
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    uint8_t *in = heap_caps_malloc(VOICE_MP3_STREAM_BUFFER_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (in == NULL) {
        in = heap_caps_malloc(VOICE_MP3_STREAM_BUFFER_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    int16_t *pcm = heap_caps_malloc(MINIMP3_MAX_SAMPLES_PER_FRAME * sizeof(int16_t) * 2,
                                    MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (pcm == NULL) {
        pcm = heap_caps_malloc(MINIMP3_MAX_SAMPLES_PER_FRAME * sizeof(int16_t) * 2,
                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (in == NULL || pcm == NULL) {
        free(in);
        free(pcm);
        fclose(f);
        return ESP_ERR_NO_MEM;
    }

    const uint32_t t0 = atom_log_ms();
    ATOM_LOG_STAGE(TAG, "tts", "play file start mp3=%uB path=%s", (unsigned)mp3_len, path);

    mp3dec_t dec;
    mp3dec_init(&dec);
    mp3dec_frame_info_t info;
    size_t len = 0;
    size_t off = 0;
    bool eof = false;
    bool configured = false;
    faculty175_audio_set_speaker_mute(false);

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
            const size_t n = fread(in + len, 1, room, f);
            len += n;
            if (n < room) {
                eof = true;
            }
        }
        if (off >= len) {
            break;
        }
        memset(&info, 0, sizeof(info));
        const int samples = mp3dec_decode_frame(&dec, in + off, (int)(len - off), pcm, &info);
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
        if (!configured) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(faculty175_audio_set_sample_rate((uint32_t)info.hz));
            configured = true;
            ATOM_LOG_STAGE(TAG, "tts", "decoder %u Hz ch=%d", (unsigned)info.hz, info.channels);
        }
        if (info.channels == 1) {
            int16_t *stereo = pcm + MINIMP3_MAX_SAMPLES_PER_FRAME;
            for (int i = samples - 1; i >= 0; --i) {
                stereo[i * 2] = pcm[i];
                stereo[i * 2 + 1] = pcm[i];
            }
            ESP_ERROR_CHECK_WITHOUT_ABORT(faculty175_audio_write_pcm(stereo, (size_t)samples * 2, 1000));
        } else {
            ESP_ERROR_CHECK_WITHOUT_ABORT(faculty175_audio_write_pcm(pcm, (size_t)samples * 2, 1000));
        }
        vTaskDelay(1);
    }

    free(in);
    free(pcm);
    fclose(f);
    ESP_ERROR_CHECK_WITHOUT_ABORT(faculty175_audio_set_sample_rate(FACULTY175_AUDIO_RATE));
    ATOM_LOG_STAGE(TAG, "tts", "play file done in %ums", (unsigned)(atom_log_ms() - t0));
    return ESP_OK;
}
