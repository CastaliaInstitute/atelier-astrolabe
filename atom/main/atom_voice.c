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

#include "astrolabe_wand_face.h"
#include "atom_board.h"

#if __has_include("secrets.local.h")
#include "secrets.local.h"
#else
#include "secrets.example.h"
#endif

static const char *TAG = "atom_voice";
#define HTTP_TIMEOUT_MS 25000
#define TTS_MP3_MAX_BYTES (384 * 1024)

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
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", content_type);
    if (accept != NULL) {
        esp_http_client_set_header(client, "Accept", accept);
    }
    if (strlen(MYNAH_SUPABASE_ANON_KEY) > 0) {
        esp_http_client_set_header(client, "apikey", MYNAH_SUPABASE_ANON_KEY);
        char auth[192];
        snprintf(auth, sizeof(auth), "Bearer %s", MYNAH_SUPABASE_ANON_KEY);
        esp_http_client_set_header(client, "Authorization", auth);
    }

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
    if (status < 200 || status >= 300) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    size_t cap = 64 * 1024;
    uint8_t *response = heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (response == NULL) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_NO_MEM;
    }
    size_t total = 0;
    while (true) {
        if (total == cap) {
            size_t next = cap * 2;
            if (next > TTS_MP3_MAX_BYTES) {
                free(response);
                esp_http_client_close(client);
                esp_http_client_cleanup(client);
                return ESP_ERR_NO_MEM;
            }
            uint8_t *grown = heap_caps_realloc(response, next, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (grown == NULL) {
                free(response);
                esp_http_client_close(client);
                esp_http_client_cleanup(client);
                return ESP_ERR_NO_MEM;
            }
            response = grown;
            cap = next;
        }
        const int read = esp_http_client_read(client, (char *)response + total, (int)(cap - total));
        if (read < 0) {
            free(response);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return ESP_FAIL;
        }
        if (read == 0) {
            break;
        }
        total += (size_t)read;
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
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

    const size_t b64_cap = ((pcm_len + 2) / 3) * 4 + 1;
    char *audio_b64 = heap_caps_malloc(b64_cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (audio_b64 == NULL) {
        return ESP_ERR_NO_MEM;
    }
    size_t b64_len = 0;
    if (mbedtls_base64_encode((unsigned char *)audio_b64, b64_cap, &b64_len, pcm, pcm_len) != 0) {
        free(audio_b64);
        return ESP_FAIL;
    }
    audio_b64[b64_len] = '\0';

    char sys[768];
    snprintf(sys, sizeof(sys), "%s Active faculty slug: %s. Conversation history: %s",
             ASTROLABE_WAND_SYSTEM_INSTRUCTION,
             faculty_slug != NULL ? faculty_slug : ASTROLABE_WAND_DEFAULT_FACULTY_SLUG,
             history != NULL && history[0] != '\0' ? history : "(none yet)");

    char *esc_sys = json_escape_alloc(sys);
    char *esc_slug = json_escape_alloc(faculty_slug != NULL ? faculty_slug : ASTROLABE_WAND_DEFAULT_FACULTY_SLUG);
    char *esc_name = json_escape_alloc(faculty_name != NULL ? faculty_name : ASTROLABE_WAND_DEFAULT_FACULTY_NAME);
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
        free(audio_b64);
        free(esc_sys);
        free(esc_slug);
        free(esc_name);
        return ESP_ERR_NO_MEM;
    }

    const int body_len = snprintf(body, body_cap,
                                  "{\"languageCode\":\"en-US\",\"sampleRateHertz\":16000,"
                                  "\"face\":\"%s\",\"facultySlug\":\"%s\",\"facultyName\":\"%s\","
                                  "\"systemInstruction\":\"%s\",\"audioBase64\":\"%s\","
                                  "\"responseFormat\":\"mp3\"}",
                                  ASTROLABE_WAND_FACE_NAME, esc_slug, esc_name, esc_sys, audio_b64);
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
    int status = 0;
    uint8_t *response = NULL;
    size_t response_len = 0;
    esp_err_t ret = post_collect_body(url, "application/json", (const uint8_t *)body, (size_t)body_len,
                                      "audio/mpeg,application/json", &response, &response_len, &status);
    free(body);
    ESP_LOGI(TAG, "voice-pipeline status=%d bytes=%u err=%s", status, (unsigned)response_len, esp_err_to_name(ret));
    if (ret != ESP_OK || response == NULL || response_len < 64) {
        free(response);
        return ESP_FAIL;
    }

    if (response_len > 0 && response[0] == '{') {
        char *json = realloc(response, response_len + 1);
        if (json == NULL) {
            free(response);
            return ESP_ERR_NO_MEM;
        }
        response = (uint8_t *)json;
        response[response_len] = '\0';
        json_find_string((const char *)response, "transcript", result->transcript, sizeof(result->transcript));
        json_find_string((const char *)response, "reply", result->reply, sizeof(result->reply));
        json_find_string((const char *)response, "route", result->route, sizeof(result->route));
        json_find_string((const char *)response, "facultySlug", result->faculty_slug, sizeof(result->faculty_slug));
        json_find_string((const char *)response, "facultyName", result->faculty_name, sizeof(result->faculty_name));
        free(response);
        return ESP_OK;
    }

    result->mp3 = response;
    result->mp3_len = response_len;
    return ESP_OK;
}

esp_err_t atom_voice_play_mp3(const uint8_t *mp3, size_t mp3_len)
{
    if (mp3 == NULL || mp3_len < 64) {
        return ESP_ERR_INVALID_ARG;
    }

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
    atom_audio_set_speaker_mute(false);

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
            ESP_ERROR_CHECK_WITHOUT_ABORT(atom_audio_set_sample_rate((uint32_t)info.hz));
            configured = true;
        }
        const size_t count = (size_t)samples * (info.channels == 1 ? 1 : 2);
        ESP_ERROR_CHECK_WITHOUT_ABORT(atom_audio_write_pcm(pcm, count, 1000));
        vTaskDelay(1);
    }

    free(pcm);
    ESP_ERROR_CHECK_WITHOUT_ABORT(atom_audio_set_sample_rate(ATOM_AUDIO_RATE));
    return ESP_OK;
}
