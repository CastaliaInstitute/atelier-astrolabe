#include "atom_voice.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

#include "astrolabe_faculty_face.h"
#include "atom_listen.h"
#include "faculty18_board.h"
#include "atom_log.h"
#include "atom_util.h"

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

typedef struct {
    bool open;
    size_t pcm_bytes;
    size_t pcm_cap;
    uint8_t *pcm;
    char *meta_json;
    size_t meta_len;
} atom_voice_stream_t;

static atom_voice_stream_t s_stream = {};
static int s_last_http_status;

int atom_voice_last_http_status(void)
{
    return s_last_http_status;
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
    free(s_stream.pcm);
    free(s_stream.meta_json);
    memset(&s_stream, 0, sizeof(s_stream));
}

static esp_err_t stream_buffer_grow(size_t extra_bytes)
{
    const size_t need = s_stream.pcm_bytes + extra_bytes;
    if (need <= s_stream.pcm_cap) {
        return ESP_OK;
    }
    size_t next = s_stream.pcm_cap == 0 ? 8192 : s_stream.pcm_cap;
    while (next < need) {
        next *= 2;
    }
    const size_t max_bytes = (size_t)ATOM_LISTEN_MAX_SECONDS * 16000 * sizeof(int16_t);
    if (next > max_bytes) {
        next = max_bytes;
    }
    if (need > next) {
        return ESP_ERR_NO_MEM;
    }
    uint8_t *grown = heap_caps_realloc(s_stream.pcm, next, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (grown == NULL) {
        grown = realloc(s_stream.pcm, next);
    }
    if (grown == NULL) {
        return ESP_ERR_NO_MEM;
    }
    s_stream.pcm = grown;
    s_stream.pcm_cap = next;
    return ESP_OK;
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
    const char *hist = (history != NULL && history[0] != '\0') ? history : "";

    char *esc_slug = json_escape_alloc(active_slug);
    char *esc_name = json_escape_alloc(active_name);
    char *esc_hist = json_escape_alloc(hist);
    if (esc_slug == NULL || esc_name == NULL || esc_hist == NULL) {
        free(esc_slug);
        free(esc_name);
        free(esc_hist);
        return NULL;
    }

    const size_t cap = strlen(esc_slug) + strlen(esc_name) + strlen(esc_hist) + 320;
    char *json = heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (json == NULL) {
        json = malloc(cap);
    }
    if (json == NULL) {
        free(esc_slug);
        free(esc_name);
        free(esc_hist);
        return NULL;
    }

    const int json_len = snprintf(json, cap,
                                  "{\"languageCode\":\"en-US\",\"sampleRateHertz\":16000,"
                                  "\"face\":\"%s\",\"facultySlug\":\"%s\",\"facultyName\":\"%s\","
                                  "\"responseFormat\":\"mp3\",\"conversationHistory\":\"%s\"}",
                                  ASTROLABE_FACULTY_FACE_NAME, esc_slug, esc_name, esc_hist);
    free(esc_slug);
    free(esc_name);
    free(esc_hist);
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

static esp_err_t parse_voice_response_body(uint8_t *response,
                                           size_t response_len,
                                           atom_voice_result_t *result,
                                           uint32_t t0)
{
    if (response == NULL || response_len < 8 || result == NULL) {
        return ESP_FAIL;
    }

    if (response_len > 0 && response[0] != '{') {
        if (result->route[0] == '\0') {
            atom_strlcpy(result->route, "ask-faculty", sizeof(result->route));
        }
        ATOM_LOG_STAGE(TAG, "tts", "raw MP3 response %uB route=%s", (unsigned)response_len, result->route);
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
    if (result->tts_voice[0] != '\0') {
        ATOM_LOG_STAGE(TAG, "tts", "voice=%s", result->tts_voice);
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

static void capture_voice_response_headers(esp_http_client_handle_t client, atom_voice_result_t *result)
{
    if (client == NULL || result == NULL) {
        return;
    }
    char *header = NULL;
    if (esp_http_client_get_header(client, "X-Voice-Route", &header) == ESP_OK && header != NULL && header[0] != '\0') {
        atom_strlcpy(result->route, header, sizeof(result->route));
    }
    header = NULL;
    if (esp_http_client_get_header(client, "X-Voice-Name", &header) == ESP_OK && header != NULL && header[0] != '\0') {
        atom_strlcpy(result->tts_voice, header, sizeof(result->tts_voice));
    }
    header = NULL;
    if (esp_http_client_get_header(client, "X-Faculty-Slug", &header) == ESP_OK && header != NULL && header[0] != '\0' &&
        result->faculty_slug[0] == '\0') {
        atom_strlcpy(result->faculty_slug, header, sizeof(result->faculty_slug));
    }
    header = NULL;
    if (esp_http_client_get_header(client, "X-Faculty-Name", &header) == ESP_OK && header != NULL && header[0] != '\0' &&
        result->faculty_name[0] == '\0') {
        atom_strlcpy(result->faculty_name, header, sizeof(result->faculty_name));
    }
    header = NULL;
    if (esp_http_client_get_header(client, "X-Voice-Transcript", &header) == ESP_OK && header != NULL &&
        header[0] != '\0' && result->transcript[0] == '\0') {
        atom_strlcpy(result->transcript, header, sizeof(result->transcript));
    }
    header = NULL;
    if (esp_http_client_get_header(client, "X-Voice-Reply", &header) == ESP_OK && header != NULL && header[0] != '\0' &&
        result->reply[0] == '\0') {
        atom_strlcpy(result->reply, header, sizeof(result->reply));
    }
}

static esp_err_t post_collect_body(const char *url,
                                   const char *content_type,
                                   const uint8_t *body,
                                   size_t body_len,
                                   const char *accept,
                                   uint8_t **out_body,
                                   size_t *out_len,
                                   int *out_status,
                                   atom_voice_result_t *header_meta)
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
    s_last_http_status = status;
    if (out_status != NULL) {
        *out_status = status;
    }

    uint8_t *response = NULL;
    size_t total = 0;
    ret = read_http_body(client, status, &response, &total);
    if (ret == ESP_OK && header_meta != NULL) {
        capture_voice_response_headers(client, header_meta);
    }
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

    const char *hist = (history != NULL && history[0] != '\0') ? history : "";

    char *esc_hist = json_escape_alloc(hist);
    char *esc_slug = json_escape_alloc(active_slug);
    char *esc_name = json_escape_alloc(active_name);
    if (esc_hist == NULL || esc_slug == NULL || esc_name == NULL) {
        free(audio_b64);
        free(esc_hist);
        free(esc_slug);
        free(esc_name);
        return ESP_ERR_NO_MEM;
    }

    const size_t body_cap = b64_len + strlen(esc_hist) + strlen(esc_slug) + strlen(esc_name) + 384;
    char *body = heap_caps_malloc(body_cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (body == NULL) {
        body = malloc(body_cap);
    }
    if (body == NULL) {
        free(audio_b64);
        free(esc_hist);
        free(esc_slug);
        free(esc_name);
        return ESP_ERR_NO_MEM;
    }

    const int body_len = snprintf(body, body_cap,
                                  "{\"languageCode\":\"en-US\",\"sampleRateHertz\":16000,"
                                  "\"face\":\"%s\",\"facultySlug\":\"%s\",\"facultyName\":\"%s\","
                                  "\"responseFormat\":\"mp3\",\"conversationHistory\":\"%s\",\"audioBase64\":\"%s\"}",
                                  ASTROLABE_FACULTY_FACE_NAME, esc_slug, esc_name, esc_hist, audio_b64);
    free(audio_b64);
    free(esc_hist);
    free(esc_slug);
    free(esc_name);
    if (body_len <= 0 || (size_t)body_len >= body_cap) {
        free(body);
        return ESP_ERR_NO_MEM;
    }

    char url[224];
    snprintf(url, sizeof(url), "%s/functions/v1/voice-pipeline", MYNAH_SUPABASE_URL);
    const uint32_t t0 = atom_log_ms();
    ATOM_LOG_STAGE(TAG, "pipeline", "POST %s pcm=%uB face=%s faculty=%s (%s) ask-faculty+chirp3",
                   url, (unsigned)pcm_len, ASTROLABE_FACULTY_FACE_NAME, active_name, active_slug);

    int status = 0;
    uint8_t *response = NULL;
    size_t response_len = 0;
    esp_err_t ret = post_collect_body(url, "application/json", (const uint8_t *)body, (size_t)body_len,
                                      "application/json,audio/mpeg", &response, &response_len, &status, result);
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
    if (s_stream.open) {
        atom_voice_stream_cancel();
    }

    size_t meta_len = 0;
    char *meta_json = build_faculty_request_json(faculty_slug, faculty_name, history, &meta_len);
    if (meta_json == NULL || meta_len == 0 || meta_len > 16384) {
        free(meta_json);
        return ESP_ERR_NO_MEM;
    }

    s_stream.meta_json = meta_json;
    s_stream.meta_len = meta_len;
    s_stream.open = true;
    s_stream.pcm_bytes = 0;
    ATOM_LOG_STAGE(TAG, "stream", "VAD capture started");
    return ESP_OK;
}

esp_err_t atom_voice_stream_write(const int16_t *pcm, size_t sample_count)
{
    if (!s_stream.open || pcm == NULL || sample_count == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    const size_t bytes = sample_count * sizeof(int16_t);
    if (stream_buffer_grow(bytes) != ESP_OK) {
        atom_voice_stream_cancel();
        return ESP_ERR_NO_MEM;
    }
    memcpy(s_stream.pcm + s_stream.pcm_bytes, pcm, bytes);
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

    const size_t body_len = 4 + s_stream.meta_len + s_stream.pcm_bytes;
    uint8_t *body = heap_caps_malloc(body_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (body == NULL) {
        body = malloc(body_len);
    }
    if (body == NULL) {
        atom_voice_stream_cancel();
        return ESP_ERR_NO_MEM;
    }

    body[0] = (uint8_t)(s_stream.meta_len & 0xff);
    body[1] = (uint8_t)((s_stream.meta_len >> 8) & 0xff);
    body[2] = (uint8_t)((s_stream.meta_len >> 16) & 0xff);
    body[3] = (uint8_t)((s_stream.meta_len >> 24) & 0xff);
    memcpy(body + 4, s_stream.meta_json, s_stream.meta_len);
    memcpy(body + 4 + s_stream.meta_len, s_stream.pcm, s_stream.pcm_bytes);

    char url[224];
    snprintf(url, sizeof(url), "%s/functions/v1/voice-pipeline", MYNAH_SUPABASE_URL);
    const uint32_t t0 = atom_log_ms();
    ATOM_LOG_STAGE(TAG, "stream", "POST %s pcm=%uB faculty=%s (%s) ask-faculty+chirp3", url, (unsigned)s_stream.pcm_bytes,
                   active_name, active_slug);

    int status = 0;
    uint8_t *response = NULL;
    size_t response_len = 0;
    esp_err_t ret = post_collect_body(url, VOICE_STREAM_CT, body, body_len, "application/json,audio/mpeg", &response,
                                      &response_len, &status, result);
    free(body);
    atom_voice_stream_cancel();

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
    faculty18_audio_set_speaker_mute(false);

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
            ESP_ERROR_CHECK_WITHOUT_ABORT(faculty18_audio_set_sample_rate((uint32_t)info.hz));
            configured = true;
            ATOM_LOG_STAGE(TAG, "tts", "decoder %u Hz ch=%d", (unsigned)info.hz, info.channels);
        }
        if (info.channels == 1) {
            int16_t *stereo = pcm + MINIMP3_MAX_SAMPLES_PER_FRAME;
            for (int i = samples - 1; i >= 0; --i) {
                stereo[i * 2] = pcm[i];
                stereo[i * 2 + 1] = pcm[i];
            }
            ESP_ERROR_CHECK_WITHOUT_ABORT(faculty18_audio_write_pcm(stereo, (size_t)samples * 2, 1000));
        } else {
            ESP_ERROR_CHECK_WITHOUT_ABORT(faculty18_audio_write_pcm(pcm, (size_t)samples * 2, 1000));
        }
        vTaskDelay(1);
    }

    free(pcm);
    ESP_ERROR_CHECK_WITHOUT_ABORT(faculty18_audio_set_sample_rate(FACULTY18_AUDIO_RATE));
    ATOM_LOG_STAGE(TAG, "tts", "play done in %ums", (unsigned)(atom_log_ms() - t0));
    return ESP_OK;
}
