#include "faculty175_spotify.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"
#include "nvs.h"

static const char *TAG = "faculty175_spotify";
static const char *NVS_NS = "spotify";
static const char *NVS_CLIENT_ID = "client_id";
static const char *NVS_REFRESH = "refresh";
enum {
    CLIENT_ID_CAP = 48,
    REFRESH_TOKEN_CAP = 768,
    ACCESS_TOKEN_CAP = 2048,
    HTTP_BODY_CAP = 12288,
    HTTP_TIMEOUT_MS = 18000,
    POLL_INTERVAL_MS = 5000,
};

typedef enum {
    SPOTIFY_TASK_STATUS = 0,
    SPOTIFY_TASK_TOGGLE,
} spotify_task_action_t;

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static faculty175_spotify_status_t s_status;
static char s_client_id[CLIENT_ID_CAP];
static char s_refresh_token[REFRESH_TOKEN_CAP];
static char s_access_token[ACCESS_TOKEN_CAP];
static int64_t s_access_expires_ms;
static bool s_loaded;
static bool s_task_busy;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void load_credentials(void)
{
    if (s_loaded) return;
    s_loaded = true;
    nvs_handle_t nvs;
    if (nvs_open(NVS_NS, NVS_READONLY, &nvs) == ESP_OK) {
        size_t client_len = sizeof(s_client_id);
        size_t refresh_len = sizeof(s_refresh_token);
        if (nvs_get_str(nvs, NVS_CLIENT_ID, s_client_id, &client_len) != ESP_OK ||
            nvs_get_str(nvs, NVS_REFRESH, s_refresh_token, &refresh_len) != ESP_OK) {
            s_client_id[0] = '\0';
            s_refresh_token[0] = '\0';
        }
        nvs_close(nvs);
    }
    portENTER_CRITICAL(&s_lock);
    s_status.configured = s_client_id[0] != '\0' && s_refresh_token[0] != '\0';
    portEXIT_CRITICAL(&s_lock);
}

esp_err_t faculty175_spotify_configure(const char *client_id, const char *refresh_token)
{
    if (client_id == NULL || refresh_token == NULL || client_id[0] == '\0' || refresh_token[0] == '\0' ||
        strlen(client_id) >= sizeof(s_client_id) || strlen(refresh_token) >= sizeof(s_refresh_token)) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;
    err = nvs_set_str(nvs, NVS_CLIENT_ID, client_id);
    if (err == ESP_OK) err = nvs_set_str(nvs, NVS_REFRESH, refresh_token);
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);
    if (err != ESP_OK) return err;

    strlcpy(s_client_id, client_id, sizeof(s_client_id));
    strlcpy(s_refresh_token, refresh_token, sizeof(s_refresh_token));
    s_access_token[0] = '\0';
    s_access_expires_ms = 0;
    s_loaded = true;
    portENTER_CRITICAL(&s_lock);
    memset(&s_status, 0, sizeof(s_status));
    s_status.configured = true;
    strlcpy(s_status.error, "Spotify paired; open face to connect", sizeof(s_status.error));
    portEXIT_CRITICAL(&s_lock);
    ESP_LOGI(TAG, "Spotify PKCE credential saved");
    return ESP_OK;
}

void faculty175_spotify_status(faculty175_spotify_status_t *out)
{
    if (out == NULL) return;
    load_credentials();
    portENTER_CRITICAL(&s_lock);
    *out = s_status;
    out->busy = s_task_busy;
    portEXIT_CRITICAL(&s_lock);
}

static void status_error(const char *message)
{
    portENTER_CRITICAL(&s_lock);
    s_status.ok = false;
    s_status.configured = s_client_id[0] != '\0' && s_refresh_token[0] != '\0';
    s_status.updated_ms = now_ms();
    strlcpy(s_status.error, message != NULL ? message : "Spotify request failed", sizeof(s_status.error));
    portEXIT_CRITICAL(&s_lock);
}

static char hex_digit(unsigned value)
{
    return value < 10 ? (char)('0' + value) : (char)('A' + value - 10);
}

static char *form_encode(const char *value)
{
    if (value == NULL) return NULL;
    const size_t len = strlen(value);
    char *out = malloc(len * 3 + 1);
    if (out == NULL) return NULL;
    size_t n = 0;
    for (size_t i = 0; i < len; ++i) {
        const unsigned char c = (unsigned char)value[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            out[n++] = (char)c;
        } else {
            out[n++] = '%';
            out[n++] = hex_digit(c >> 4);
            out[n++] = hex_digit(c & 0x0f);
        }
    }
    out[n] = '\0';
    return out;
}

static esp_err_t http_request(const char *url,
                              esp_http_client_method_t method,
                              const char *authorization,
                              const char *content_type,
                              const char *request_body,
                              char **response_body,
                              int *http_status)
{
    *response_body = NULL;
    *http_status = 0;
    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .keep_alive_enable = false,
        .buffer_size = 2048,
        .buffer_size_tx = 2048,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) return ESP_ERR_NO_MEM;
    esp_http_client_set_method(client, method);
    esp_http_client_set_header(client, "Accept", "application/json");
    esp_http_client_set_header(client, "User-Agent", "Astrolabe-Spotify/1");
    if (authorization != NULL && authorization[0] != '\0') {
        esp_http_client_set_header(client, "Authorization", authorization);
    }
    if (content_type != NULL) esp_http_client_set_header(client, "Content-Type", content_type);
    const size_t request_len = request_body != NULL ? strlen(request_body) : 0;
    esp_err_t err = esp_http_client_open(client, request_len);
    if (err == ESP_OK && request_len > 0 &&
        esp_http_client_write(client, request_body, request_len) != (int)request_len) {
        err = ESP_FAIL;
    }
    if (err == ESP_OK) {
        (void)esp_http_client_fetch_headers(client);
        *http_status = esp_http_client_get_status_code(client);
    }
    char *body = NULL;
    if (err == ESP_OK) {
        body = malloc(HTTP_BODY_CAP);
        if (body == NULL) err = ESP_ERR_NO_MEM;
    }
    size_t total = 0;
    while (err == ESP_OK && total + 1 < HTTP_BODY_CAP) {
        const int got = esp_http_client_read(client, body + total, HTTP_BODY_CAP - total - 1);
        if (got < 0) {
            err = ESP_FAIL;
            break;
        }
        if (got == 0) break;
        total += (size_t)got;
    }
    if (body != NULL) body[total] = '\0';
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    if (err != ESP_OK) {
        free(body);
        return err;
    }
    *response_body = body;
    return ESP_OK;
}

static esp_err_t refresh_access_token(void)
{
    const int64_t now = esp_timer_get_time() / 1000;
    if (s_access_token[0] != '\0' && now + 30000 < s_access_expires_ms) return ESP_OK;
    char *encoded_refresh = form_encode(s_refresh_token);
    char *encoded_client = form_encode(s_client_id);
    if (encoded_refresh == NULL || encoded_client == NULL) {
        free(encoded_refresh);
        free(encoded_client);
        return ESP_ERR_NO_MEM;
    }
    const size_t form_cap = strlen(encoded_refresh) + strlen(encoded_client) + 96;
    char *form = malloc(form_cap);
    if (form == NULL) {
        free(encoded_refresh);
        free(encoded_client);
        return ESP_ERR_NO_MEM;
    }
    snprintf(form, form_cap, "grant_type=refresh_token&refresh_token=%s&client_id=%s", encoded_refresh, encoded_client);
    free(encoded_refresh);
    free(encoded_client);
    char *body = NULL;
    int status = 0;
    esp_err_t err = http_request("https://accounts.spotify.com/api/token", HTTP_METHOD_POST, NULL,
                                 "application/x-www-form-urlencoded", form, &body, &status);
    free(form);
    if (err != ESP_OK) return err;
    cJSON *json = cJSON_Parse(body);
    free(body);
    if (status != 200 || json == NULL) {
        cJSON_Delete(json);
        return ESP_FAIL;
    }
    const cJSON *access = cJSON_GetObjectItemCaseSensitive(json, "access_token");
    const cJSON *expires = cJSON_GetObjectItemCaseSensitive(json, "expires_in");
    const cJSON *rotated = cJSON_GetObjectItemCaseSensitive(json, "refresh_token");
    if (!cJSON_IsString(access) || access->valuestring == NULL || strlen(access->valuestring) >= sizeof(s_access_token)) {
        cJSON_Delete(json);
        return ESP_FAIL;
    }
    strlcpy(s_access_token, access->valuestring, sizeof(s_access_token));
    const int expires_seconds = cJSON_IsNumber(expires) ? expires->valueint : 3600;
    s_access_expires_ms = now + (int64_t)(expires_seconds > 60 ? expires_seconds : 3600) * 1000;
    if (cJSON_IsString(rotated) && rotated->valuestring != NULL && rotated->valuestring[0] != '\0' &&
        strlen(rotated->valuestring) < sizeof(s_refresh_token)) {
        strlcpy(s_refresh_token, rotated->valuestring, sizeof(s_refresh_token));
        nvs_handle_t nvs;
        if (nvs_open(NVS_NS, NVS_READWRITE, &nvs) == ESP_OK) {
            if (nvs_set_str(nvs, NVS_REFRESH, s_refresh_token) == ESP_OK) (void)nvs_commit(nvs);
            nvs_close(nvs);
        }
    }
    cJSON_Delete(json);
    return ESP_OK;
}

static esp_err_t api_request(const char *path, esp_http_client_method_t method, char **body, int *status)
{
    esp_err_t err = refresh_access_token();
    if (err != ESP_OK) return err;
    char url[160];
    snprintf(url, sizeof(url), "https://api.spotify.com/v1%s", path);
    char *authorization = malloc(strlen(s_access_token) + 8);
    if (authorization == NULL) return ESP_ERR_NO_MEM;
    snprintf(authorization, strlen(s_access_token) + 8, "Bearer %s", s_access_token);
    err = http_request(url, method, authorization, NULL, NULL, body, status);
    free(authorization);
    return err;
}

static esp_err_t read_player(void)
{
    char *body = NULL;
    int status = 0;
    esp_err_t err = api_request("/me/player", HTTP_METHOD_GET, &body, &status);
    if (err != ESP_OK) {
        free(body);
        return err;
    }
    if (status == 204) {
        portENTER_CRITICAL(&s_lock);
        s_status.ok = true;
        s_status.configured = true;
        s_status.is_playing = false;
        s_status.track[0] = '\0';
        s_status.artist[0] = '\0';
        s_status.device[0] = '\0';
        strlcpy(s_status.error, "Open Spotify on a speaker or phone", sizeof(s_status.error));
        s_status.updated_ms = now_ms();
        portEXIT_CRITICAL(&s_lock);
        free(body);
        return ESP_OK;
    }
    cJSON *json = cJSON_Parse(body);
    free(body);
    if (status != 200 || json == NULL) {
        cJSON_Delete(json);
        return ESP_FAIL;
    }
    const cJSON *playing = cJSON_GetObjectItemCaseSensitive(json, "is_playing");
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(json, "item");
    const cJSON *device = cJSON_GetObjectItemCaseSensitive(json, "device");
    const cJSON *track = cJSON_IsObject(item) ? cJSON_GetObjectItemCaseSensitive(item, "name") : NULL;
    const cJSON *artists = cJSON_IsObject(item) ? cJSON_GetObjectItemCaseSensitive(item, "artists") : NULL;
    const cJSON *artist0 = cJSON_IsArray(artists) ? cJSON_GetArrayItem(artists, 0) : NULL;
    const cJSON *artist = cJSON_IsObject(artist0) ? cJSON_GetObjectItemCaseSensitive(artist0, "name") : NULL;
    const cJSON *device_name = cJSON_IsObject(device) ? cJSON_GetObjectItemCaseSensitive(device, "name") : NULL;
    portENTER_CRITICAL(&s_lock);
    s_status.ok = true;
    s_status.configured = true;
    s_status.is_playing = cJSON_IsTrue(playing);
    strlcpy(s_status.track, cJSON_IsString(track) ? track->valuestring : "", sizeof(s_status.track));
    strlcpy(s_status.artist, cJSON_IsString(artist) ? artist->valuestring : "", sizeof(s_status.artist));
    strlcpy(s_status.device, cJSON_IsString(device_name) ? device_name->valuestring : "", sizeof(s_status.device));
    s_status.error[0] = '\0';
    s_status.updated_ms = now_ms();
    portEXIT_CRITICAL(&s_lock);
    cJSON_Delete(json);
    return ESP_OK;
}

static esp_err_t toggle_player(void)
{
    bool is_playing;
    portENTER_CRITICAL(&s_lock);
    is_playing = s_status.is_playing;
    portEXIT_CRITICAL(&s_lock);
    char *body = NULL;
    int status = 0;
    esp_err_t err = api_request(is_playing ? "/me/player/pause" : "/me/player/play", HTTP_METHOD_PUT, &body, &status);
    free(body);
    if (err != ESP_OK || status != 204) return err != ESP_OK ? err : ESP_FAIL;
    vTaskDelay(pdMS_TO_TICKS(450));
    return read_player();
}

static void spotify_task(void *arg)
{
    const spotify_task_action_t action = (spotify_task_action_t)(uintptr_t)arg;
    const esp_err_t err = action == SPOTIFY_TASK_TOGGLE ? toggle_player() : read_player();
    if (err != ESP_OK) {
        status_error(action == SPOTIFY_TASK_TOGGLE ? "Playback command failed" : "Spotify connection failed");
        ESP_LOGW(TAG, "%s failed: %s", action == SPOTIFY_TASK_TOGGLE ? "toggle" : "status", esp_err_to_name(err));
    }
    portENTER_CRITICAL(&s_lock);
    s_task_busy = false;
    portEXIT_CRITICAL(&s_lock);
    vTaskDelete(NULL);
}

static bool start_task(spotify_task_action_t action)
{
    load_credentials();
    portENTER_CRITICAL(&s_lock);
    const bool can_start = s_status.configured && !s_task_busy;
    if (can_start) s_task_busy = true;
    portEXIT_CRITICAL(&s_lock);
    if (!can_start) return false;
    if (xTaskCreate(spotify_task, "spotify", 9216, (void *)(uintptr_t)action, 4, NULL) != pdPASS) {
        portENTER_CRITICAL(&s_lock);
        s_task_busy = false;
        portEXIT_CRITICAL(&s_lock);
        return false;
    }
    return true;
}

void faculty175_spotify_poll(void)
{
    faculty175_spotify_status_t status;
    faculty175_spotify_status(&status);
    if (!status.configured || status.busy) return;
    if (status.updated_ms == 0 || now_ms() - status.updated_ms >= POLL_INTERVAL_MS) {
        (void)start_task(SPOTIFY_TASK_STATUS);
    }
}

bool faculty175_spotify_toggle(void)
{
    return start_task(SPOTIFY_TASK_TOGGLE);
}
