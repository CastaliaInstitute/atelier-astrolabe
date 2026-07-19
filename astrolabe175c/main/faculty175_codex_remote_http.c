#include "faculty175_codex_remote_http.h"

#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_check.h"
#include "faculty175_codex.h"

#ifndef ASTROLABE_CYBER_FEATURES
#define ASTROLABE_CYBER_FEATURES 0
#endif

#if ASTROLABE_CYBER_FEATURES

extern const uint8_t faculty175_codex_remote_page_html_start[]
    asm("_binary_faculty175_codex_remote_page_html_start");
extern const uint8_t faculty175_codex_remote_page_html_end[]
    asm("_binary_faculty175_codex_remote_page_html_end");

static esp_err_t codex_remote_page_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Content-Security-Policy",
                       "default-src 'self'; img-src 'self' blob: data:; media-src 'self' blob:; "
                       "style-src 'unsafe-inline'; script-src 'unsafe-inline'; "
                       "connect-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'none'");
    httpd_resp_set_hdr(req, "Referrer-Policy", "no-referrer");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    return httpd_resp_send(req,
                           (const char *)faculty175_codex_remote_page_html_start,
                           faculty175_codex_remote_page_html_end - faculty175_codex_remote_page_html_start);
}

static void codex_api_headers(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(req, "Referrer-Policy", "no-referrer");
}

static bool codex_write_authorized(httpd_req_t *req)
{
    char version[16] = {};
    return httpd_req_get_hdr_value_str(req, "X-Astrolabe-Codex", version, sizeof(version)) == ESP_OK &&
           strcmp(version, "sync-v1") == 0;
}

static cJSON *codex_read_json(httpd_req_t *req)
{
    if (req->content_len == 0 || req->content_len > 8192) return NULL;
    char *body = calloc(1, req->content_len + 1);
    if (body == NULL) return NULL;
    size_t received = 0;
    while (received < req->content_len) {
        const int n = httpd_req_recv(req, body + received, req->content_len - received);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (n <= 0) {
            free(body);
            return NULL;
        }
        received += (size_t)n;
    }
    cJSON *root = cJSON_ParseWithLength(body, received);
    free(body);
    return root;
}

static esp_err_t codex_send_state(httpd_req_t *req, esp_err_t result)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) return ESP_ERR_NO_MEM;
    cJSON_AddBoolToObject(root, "ok", result == ESP_OK);
    if (result != ESP_OK) cJSON_AddStringToObject(root, "error", esp_err_to_name(result));
    esp_err_t err = faculty175_codex_to_json(root);
    char *body = err == ESP_OK ? cJSON_PrintUnformatted(root) : NULL;
    cJSON_Delete(root);
    if (body == NULL) return ESP_ERR_NO_MEM;
    codex_api_headers(req);
    err = httpd_resp_sendstr(req, body);
    free(body);
    return err;
}

static esp_err_t codex_state_get(httpd_req_t *req)
{
    return codex_send_state(req, ESP_OK);
}

static esp_err_t codex_state_post(httpd_req_t *req)
{
    if (!codex_write_authorized(req)) {
        httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "sync header required");
        return ESP_ERR_INVALID_STATE;
    }
    cJSON *root = codex_read_json(req);
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid state json");
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t result = faculty175_codex_apply_json(root);
    cJSON_Delete(root);
    return codex_send_state(req, result);
}

static esp_err_t codex_action_post(httpd_req_t *req)
{
    if (!codex_write_authorized(req)) {
        httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "sync header required");
        return ESP_ERR_INVALID_STATE;
    }
    cJSON *root = codex_read_json(req);
    cJSON *action = root != NULL ? cJSON_GetObjectItemCaseSensitive(root, "action") : NULL;
    bool accepted = false;
    if (cJSON_IsString(action)) {
        if (strcmp(action->valuestring, "next") == 0) accepted = faculty175_codex_select_delta(1);
        else if (strcmp(action->valuestring, "previous") == 0) accepted = faculty175_codex_select_delta(-1);
        else if (strcmp(action->valuestring, "select") == 0) {
            const cJSON *task_id = cJSON_GetObjectItemCaseSensitive(root, "taskId");
            accepted = cJSON_IsString(task_id) && faculty175_codex_select_id(task_id->valuestring);
        }
        else if (strcmp(action->valuestring, "prompt") == 0) {
            const cJSON *input = cJSON_GetObjectItemCaseSensitive(root, "input");
            accepted = cJSON_IsString(input) && faculty175_codex_queue_prompt(input->valuestring);
        }
        else if (strcmp(action->valuestring, "pin-toggle") == 0) {
            const cJSON *task_id = cJSON_GetObjectItemCaseSensitive(root, "taskId");
            accepted = cJSON_IsString(task_id)
                ? faculty175_codex_toggle_pin_id(task_id->valuestring)
                : faculty175_codex_toggle_pin();
        }
        else if (strcmp(action->valuestring, "emoji-set") == 0) {
            const cJSON *task_id = cJSON_GetObjectItemCaseSensitive(root, "taskId");
            const cJSON *emoji_index = cJSON_GetObjectItemCaseSensitive(root, "emojiIndex");
            accepted = cJSON_IsString(task_id) && cJSON_IsNumber(emoji_index) &&
                faculty175_codex_set_emoji(task_id->valuestring, emoji_index->valueint);
        }
        else accepted = faculty175_codex_queue_action(action->valuestring);
    }
    cJSON_Delete(root);
    return codex_send_state(req, accepted ? ESP_OK : ESP_ERR_INVALID_ARG);
}

esp_err_t faculty175_codex_remote_http_register(httpd_handle_t server)
{
    if (server == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    const httpd_uri_t page = {
        .uri = "/codex",
        .method = HTTP_GET,
        .handler = codex_remote_page_get,
    };
    const httpd_uri_t state_get = {
        .uri = "/api/codex/state",
        .method = HTTP_GET,
        .handler = codex_state_get,
    };
    const httpd_uri_t state_post = {
        .uri = "/api/codex/state",
        .method = HTTP_POST,
        .handler = codex_state_post,
    };
    const httpd_uri_t action_post = {
        .uri = "/api/codex/action",
        .method = HTTP_POST,
        .handler = codex_action_post,
    };
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &page), "codex_http", "register /codex");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &state_get), "codex_http", "register GET state");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &state_post), "codex_http", "register POST state");
    return httpd_register_uri_handler(server, &action_post);
}

#else

esp_err_t faculty175_codex_remote_http_register(httpd_handle_t server)
{
    (void)server;
    return ESP_ERR_NOT_SUPPORTED;
}

#endif
