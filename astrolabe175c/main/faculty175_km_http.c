#include "faculty175_km_http.h"

#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "faculty175_km.h"
#include "faculty175_usb_screen.h"

#ifndef ASTROLABE_KM_ENABLED
#define ASTROLABE_KM_ENABLED 0
#endif

#if ASTROLABE_KM_ENABLED && CONFIG_HTTPD_WS_SUPPORT

static const char *TAG = "faculty175_km_http";
extern const uint8_t faculty175_km_page_html_start[] asm("_binary_faculty175_km_page_html_start");
extern const uint8_t faculty175_km_page_html_end[] asm("_binary_faculty175_km_page_html_end");

typedef struct {
    int fd;
    bool authorized;
    uint8_t failures;
} km_session_t;

static km_session_t s_sessions[4];

static km_session_t *session_for(int fd, bool create)
{
    km_session_t *empty = NULL;
    for (size_t i = 0; i < sizeof(s_sessions) / sizeof(s_sessions[0]); ++i) {
        if (s_sessions[i].fd == fd) {
            return &s_sessions[i];
        }
        if (empty == NULL && s_sessions[i].fd <= 0) {
            empty = &s_sessions[i];
        }
    }
    if (create && empty != NULL) {
        *empty = (km_session_t){.fd = fd};
        return empty;
    }
    return NULL;
}

static esp_err_t ws_json(httpd_req_t *req, const char *json)
{
    httpd_ws_frame_t frame = {
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)json,
        .len = strlen(json),
    };
    return httpd_ws_send_frame(req, &frame);
}

static int json_int(cJSON *root, const char *name, int fallback)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);
    return cJSON_IsNumber(item) ? item->valueint : fallback;
}

static int clamp_int(int value, int low, int high)
{
    return value < low ? low : (value > high ? high : value);
}

static esp_err_t km_page_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req,
                           (const char *)faculty175_km_page_html_start,
                           faculty175_km_page_html_end - faculty175_km_page_html_start);
}

static esp_err_t km_ws(httpd_req_t *req)
{
    const int fd = httpd_req_to_sockfd(req);
    if (req->method == HTTP_GET) {
        km_session_t *session = session_for(fd, true);
        if (session == NULL) {
            return ESP_ERR_NO_MEM;
        }
        session->authorized = false;
        session->failures = 0;
        return ESP_OK;
    }

    httpd_ws_frame_t frame = {};
    frame.type = HTTPD_WS_TYPE_TEXT;
    esp_err_t err = httpd_ws_recv_frame(req, &frame, 0);
    if (err != ESP_OK || frame.len == 0 || frame.len > 512) {
        return err == ESP_OK ? ESP_ERR_INVALID_SIZE : err;
    }
    char *payload = calloc(1, frame.len + 1);
    if (payload == NULL) {
        return ESP_ERR_NO_MEM;
    }
    frame.payload = (uint8_t *)payload;
    err = httpd_ws_recv_frame(req, &frame, frame.len);
    if (err != ESP_OK) {
        free(payload);
        return err;
    }
    cJSON *root = cJSON_ParseWithLength(payload, frame.len);
    free(payload);
    if (root == NULL) {
        return ws_json(req, "{\"ok\":false,\"error\":\"bad-json\"}");
    }

    km_session_t *session = session_for(fd, true);
    cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (session == NULL || !cJSON_IsString(type)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    if (strcmp(type->valuestring, "auth") == 0) {
        cJSON *code = cJSON_GetObjectItemCaseSensitive(root, "code");
        session->authorized = cJSON_IsString(code) &&
                              strcmp(code->valuestring, faculty175_km_pairing_code()) == 0;
        if (session->authorized) {
            session->failures = 0;
            ESP_LOGI(TAG, "paired browser fd=%d", fd);
            err = ws_json(req, "{\"ok\":true,\"type\":\"auth\"}");
        } else {
            ++session->failures;
            err = ws_json(req, "{\"ok\":false,\"type\":\"auth\"}");
            if (session->failures >= 5) {
                (void)httpd_sess_trigger_close(req->handle, fd);
                *session = (km_session_t){};
            }
        }
        cJSON_Delete(root);
        return err;
    }

    if (!session->authorized) {
        cJSON_Delete(root);
        return ws_json(req, "{\"ok\":false,\"error\":\"pair-first\"}");
    }

    if (strcmp(type->valuestring, "release") == 0) {
        err = faculty175_km_release_all();
    } else if (strcmp(type->valuestring, "mouse") == 0) {
        err = faculty175_km_mouse((int16_t)clamp_int(json_int(root, "dx", 0), -1024, 1024),
                                  (int16_t)clamp_int(json_int(root, "dy", 0), -1024, 1024),
                                  (int8_t)clamp_int(json_int(root, "wheel", 0), -127, 127),
                                  (uint8_t)(json_int(root, "buttons", 0) & 0x07));
    } else if (strcmp(type->valuestring, "key") == 0) {
        cJSON *code = cJSON_GetObjectItemCaseSensitive(root, "code");
        cJSON *down = cJSON_GetObjectItemCaseSensitive(root, "down");
        const uint8_t keycode = cJSON_IsString(code) ? faculty175_km_keycode_from_dom(code->valuestring) : 0;
        err = keycode != 0
            ? faculty175_km_key(keycode, cJSON_IsTrue(down), (uint8_t)(json_int(root, "modifiers", 0) & 0x0f))
            : ESP_ERR_INVALID_ARG;
    } else if (strcmp(type->valuestring, "text") == 0) {
        cJSON *text = cJSON_GetObjectItemCaseSensitive(root, "text");
        err = cJSON_IsString(text) ? faculty175_km_type_text(text->valuestring) : ESP_ERR_INVALID_ARG;
    } else {
        err = ESP_ERR_INVALID_ARG;
    }
    cJSON_Delete(root);
    return err == ESP_OK ? ws_json(req, "{\"ok\":true}")
                         : ws_json(req, "{\"ok\":false,\"error\":\"input-rejected\"}");
}

static esp_err_t screen_jpeg_post(httpd_req_t *req)
{
    char pair[16] = {};
    if (httpd_req_get_hdr_value_str(req, "X-Astrolabe-Pair", pair, sizeof(pair)) != ESP_OK ||
        strcmp(pair, faculty175_km_pairing_code()) != 0) {
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "pair code required");
        return ESP_ERR_INVALID_STATE;
    }
    if (req->content_len < 64 || req->content_len > FACULTY175_USB_SCREEN_MAX_JPEG) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "JPEG must be 64..262144 bytes");
        return ESP_ERR_INVALID_SIZE;
    }
    uint8_t *jpeg = heap_caps_malloc(req->content_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (jpeg == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
        return ESP_ERR_NO_MEM;
    }
    size_t received = 0;
    while (received < req->content_len) {
        const int n = httpd_req_recv(req, (char *)jpeg + received, req->content_len - received);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (n <= 0) {
            heap_caps_free(jpeg);
            return ESP_FAIL;
        }
        received += (size_t)n;
    }
    const esp_err_t err = faculty175_usb_screen_show_jpeg(jpeg, received);
    heap_caps_free(jpeg);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, err == ESP_OK ? "{\"ok\":true}" : "{\"ok\":false}");
}

esp_err_t faculty175_km_http_register(httpd_handle_t server)
{
    if (server == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    const httpd_uri_t page = {
        .uri = "/km",
        .method = HTTP_GET,
        .handler = km_page_get,
    };
    const httpd_uri_t ws = {
        .uri = "/api/km/ws",
        .method = HTTP_GET,
        .handler = km_ws,
        .is_websocket = true,
        .handle_ws_control_frames = true,
    };
    const httpd_uri_t screen = {
        .uri = "/api/screen/jpeg",
        .method = HTTP_POST,
        .handler = screen_jpeg_post,
    };
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &page), TAG, "register /km");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &ws), TAG, "register KM websocket");
    return httpd_register_uri_handler(server, &screen);
}

#else

esp_err_t faculty175_km_http_register(httpd_handle_t server)
{
    (void)server;
    return ESP_ERR_NOT_SUPPORTED;
}

#endif
