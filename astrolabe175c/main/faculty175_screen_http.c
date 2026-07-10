#include "faculty175_screen_http.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "astrolabe_time.h"
#include "faculty175_ble.h"
#include "faculty175_board.h"
#include "faculty175_device_settings.h"
#include "faculty175_face_profile.h"
#include "faculty175_faces.h"
#include "faculty175_serial.h"
#include "faculty175_wifi_lab.h"
#include "faculty175_wifi_monitor.h"
#include "faculty175_wifi_settings.h"

static const char *TAG = "faculty175_screen_http";
static httpd_handle_t s_httpd;
static esp_ip4_addr_t s_ip;

// The 1.75C QA path relies on the lightweight screen/settings HTTP server.
#define FACULTY175_SCREEN_HTTP_RUNTIME_ENABLED 1
#define FACULTY175_SCREEN_HTTP_STACK_SIZE 5120
#define FACULTY175_SCREEN_HTTP_FALLBACK_STACK_SIZE 4096
#define FACULTY175_SCREEN_HTTP_START_ATTEMPTS 4

static esp_err_t send_chunk_cb(void *ctx, const uint8_t *data, size_t len)
{
    return httpd_resp_send_chunk((httpd_req_t *)ctx, (const char *)data, len);
}

static void set_api_headers(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET,POST,OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
}

static esp_err_t api_options(httpd_req_t *req)
{
    set_api_headers(req);
    return httpd_resp_send(req, "", 0);
}

static esp_err_t root_get(httpd_req_t *req)
{
    char body[768];
    snprintf(body,
             sizeof(body),
             "<!doctype html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" "
             "content=\"width=device-width,initial-scale=1\"><title>Astrolabe</title></head>"
             "<body style=\"margin:0;background:#111;color:#ccc;font-family:system-ui,sans-serif;\">"
             "<p style=\"padding:10px\">Faculty175 ESP-IDF "
             "| <a style=\"color:#8cf\" href=\"/wifi\">wifi settings</a> "
             "| <a style=\"color:#8cf\" href=\"/screen.bmp\">screen.bmp</a></p>"
             "<img src=\"/screen.bmp\" style=\"width:100%%;max-width:466px;height:auto;display:block;margin:0 auto\" "
             "alt=\"screen\"></body></html>");
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}

static void restart_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(900));
    esp_restart();
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static void url_decode_in_place(char *s)
{
    char *w = s;
    for (char *r = s; *r != '\0'; ++r) {
        if (*r == '+') {
            *w++ = ' ';
        } else if (*r == '%' && hex_digit(r[1]) >= 0 && hex_digit(r[2]) >= 0) {
            *w++ = (char)((hex_digit(r[1]) << 4) | hex_digit(r[2]));
            r += 2;
        } else {
            *w++ = *r;
        }
    }
    *w = '\0';
}

static void form_value(const char *body, const char *key, char *out, size_t out_cap)
{
    if (out == NULL || out_cap == 0) {
        return;
    }
    out[0] = '\0';
    const size_t key_len = strlen(key);
    const char *p = body;
    while (p != NULL && *p != '\0') {
        if (strncmp(p, key, key_len) == 0 && p[key_len] == '=') {
            p += key_len + 1;
            size_t n = 0;
            while (p[n] != '\0' && p[n] != '&' && n + 1 < out_cap) {
                out[n] = p[n];
                ++n;
            }
            out[n] = '\0';
            url_decode_in_place(out);
            return;
        }
        p = strchr(p, '&');
        if (p != NULL) {
            ++p;
        }
    }
}

static esp_err_t wifi_get(httpd_req_t *req)
{
    char body[2800];
    char known_html[620] = {};
    size_t known_off = 0;
    const bool ap = faculty175_wifi_settings_ap_active();
    const bool router = faculty175_wifi_settings_travel_router_enabled();
    const char *ssid = faculty175_wifi_settings_ssid();
    const char *upstream = faculty175_wifi_settings_upstream_ssid();
    const char *url = faculty175_wifi_settings_url();
    faculty175_wifi_known_t known[FACULTY175_WIFI_KNOWN_MAX] = {};
    const size_t known_count = faculty175_wifi_settings_load_known(known, FACULTY175_WIFI_KNOWN_MAX);
    for (size_t i = 0; i < known_count && known_off + 80 < sizeof(known_html); ++i) {
        const int wrote = snprintf(known_html + known_off,
                                   sizeof(known_html) - known_off,
                                   "<li>%s%s <span class=\"muted\">%s</span></li>",
                                   i == 0 ? "<strong>" : "",
                                   known[i].ssid,
                                   i == 0 ? "primary</strong>" : "");
        if (wrote < 0) {
            break;
        }
        known_off += (size_t)wrote < sizeof(known_html) - known_off
            ? (size_t)wrote
            : sizeof(known_html) - known_off - 1;
    }
    snprintf(body,
             sizeof(body),
             "<!doctype html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" "
             "content=\"width=device-width,initial-scale=1\"><title>Astrolabe WiFi</title>"
             "<style>body{margin:0;background:#10141b;color:#eef3ff;font-family:system-ui,sans-serif}"
             "main{max-width:34rem;margin:auto;padding:24px}input,button{box-sizing:border-box;width:100%%;"
             "font:inherit;padding:12px;margin:8px 0;border-radius:8px;border:1px solid #334050;background:#0a0e14;"
             "color:#eef3ff}button{background:#d9b45f;color:#17120a;border:0;font-weight:700}"
             "a{color:#94d7ff}.muted{color:#a9b3c3}ul{padding-left:1.2rem}</style></head><body><main>"
             "<h1>Astrolabe WiFi</h1>"
             "<p class=\"muted\">%s%s%s</p>"
             "<p class=\"muted\">Travel router: <strong>%s</strong>%s%s%s</p>"
             "<h2>Known networks</h2><ul>%s%s</ul>"
             "<form method=\"post\" action=\"/wifi\">"
             "<label>Network SSID<input name=\"ssid\" maxlength=\"32\" autocomplete=\"off\"></label>"
             "<label>Password<input name=\"password\" maxlength=\"64\" type=\"password\"></label>"
             "<label><input style=\"width:auto\" name=\"router\" value=\"1\" type=\"checkbox\" %s> "
             "Keep setup AP online as a travel router</label>"
             "<button type=\"submit\">Save as primary and reboot</button></form>"
             "<p><a href=\"/wifi/scan\">Scan WiFi from ESP32-S3</a> | <a href=\"/\">Screen capture</a>%s%s%s</p>"
             "</main></body></html>",
             ap ? "Join the setup AP, then open " : "Connected to ",
             ap ? (url[0] != '\0' ? url : "http://192.168.4.1/wifi") : (ssid[0] != '\0' ? ssid : "WiFi"),
             ap ? "." : ".",
             router ? "enabled" : "off",
             upstream[0] != '\0' ? " via " : "",
             upstream[0] != '\0' ? upstream : "",
             router && ap ? " (AP remains available)" : "",
             known_count > 0 ? known_html : "",
             known_count > 0 ? "" : "<li class=\"muted\">No saved networks</li>",
             router ? "checked" : "",
             url[0] != '\0' ? " | " : "",
             url[0] != '\0' ? url : "",
             "");
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}

static const char *wifi_auth_label(wifi_auth_mode_t auth)
{
    switch (auth) {
        case WIFI_AUTH_OPEN: return "open";
        case WIFI_AUTH_WEP: return "WEP";
        case WIFI_AUTH_WPA_PSK: return "WPA";
        case WIFI_AUTH_WPA2_PSK: return "WPA2";
        case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2";
        case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2 Enterprise";
        case WIFI_AUTH_WPA3_PSK: return "WPA3";
        case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/WPA3";
        default: return "locked";
    }
}

static esp_err_t wifi_scan_get(httpd_req_t *req)
{
    faculty175_wifi_settings_set_scan_suppressed(true);
    wifi_scan_config_t scan = {
        .show_hidden = true,
    };
    esp_err_t err = esp_wifi_scan_start(&scan, true);
    if (err != ESP_OK) {
        faculty175_wifi_settings_set_scan_suppressed(false);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, esp_err_to_name(err));
        return err;
    }

    uint16_t count = 0;
    (void)esp_wifi_scan_get_ap_num(&count);
    if (count > 24) {
        count = 24;
    }
    wifi_ap_record_t aps[24] = {};
    err = esp_wifi_scan_get_ap_records(&count, aps);
    faculty175_wifi_settings_set_scan_suppressed(false);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, esp_err_to_name(err));
        return err;
    }
    faculty175_wifi_monitor_record_scan(count);

    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr_chunk(req,
                             "<!doctype html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" "
                             "content=\"width=device-width,initial-scale=1\"><title>Astrolabe WiFi Scan</title>"
                             "<style>body{margin:0;background:#10141b;color:#eef3ff;font-family:system-ui,sans-serif}"
                             "main{max-width:40rem;margin:auto;padding:24px}table{width:100%;border-collapse:collapse}"
                             "td,th{border-bottom:1px solid #334050;padding:8px;text-align:left}a{color:#94d7ff}"
                             ".muted{color:#a9b3c3}</style></head><body><main><h1>WiFi Scan</h1>"
                             "<p><a href=\"/wifi\">Back to WiFi settings</a></p>"
                             "<table><tr><th>SSID</th><th>RSSI</th><th>Ch</th><th>Security</th></tr>");
    for (uint16_t i = 0; i < count; ++i) {
        char row[240];
        snprintf(row,
                 sizeof(row),
                 "<tr><td>%s</td><td>%d</td><td>%u</td><td>%s</td></tr>",
                 aps[i].ssid[0] != '\0' ? (const char *)aps[i].ssid : "<span class=\"muted\">hidden</span>",
                 (int)aps[i].rssi,
                 (unsigned)aps[i].primary,
                 wifi_auth_label(aps[i].authmode));
        httpd_resp_sendstr_chunk(req, row);
    }
    httpd_resp_sendstr_chunk(req,
                             "</table><p class=\"muted\">ESP32-S3 can only use 2.4 GHz WiFi. "
                             "If the hotel SSID is missing here, it may be 5 GHz only or too weak.</p>"
                             "</main></body></html>");
    return httpd_resp_sendstr_chunk(req, NULL);
}

static esp_err_t wifi_post(httpd_req_t *req)
{
    char body[256];
    size_t remaining = req->content_len;
    if (remaining >= sizeof(body)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "form too large");
        return ESP_FAIL;
    }
    size_t off = 0;
    while (remaining > 0) {
        const int got = httpd_req_recv(req, body + off, remaining);
        if (got <= 0) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "read failed");
            return ESP_FAIL;
        }
        off += (size_t)got;
        remaining -= (size_t)got;
    }
    body[off] = '\0';

    char ssid[FACULTY175_WIFI_SSID_MAX + 1];
    char pass[FACULTY175_WIFI_PASS_MAX + 1];
    char router[4];
    form_value(body, "ssid", ssid, sizeof(ssid));
    form_value(body, "password", pass, sizeof(pass));
    form_value(body, "router", router, sizeof(router));
    if (ssid[0] == '\0') {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID required");
        return ESP_FAIL;
    }
    esp_err_t err = faculty175_wifi_settings_set_travel_router_enabled(router[0] != '\0');
    if (err == ESP_OK) {
        err = faculty175_wifi_settings_save(ssid, pass);
    }
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, esp_err_to_name(err));
        return err;
    }
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req,
                       "<!doctype html><html><body style=\"font-family:system-ui;background:#10141b;color:#eef3ff\">"
                       "<main style=\"padding:24px\"><h1>Saved</h1><p>Astrolabe is rebooting with the new WiFi settings.</p>"
                       "</main></body></html>");
    xTaskCreate(restart_task, "wifi_restart", 2048, NULL, 1, NULL);
    return ESP_OK;
}

static void append_json_string(char *out, size_t out_cap, size_t *off, const char *s)
{
    if (out == NULL || out_cap == 0 || off == NULL || *off >= out_cap) {
        return;
    }
    int wrote = snprintf(out + *off, out_cap - *off, "\"");
    if (wrote < 0) {
        return;
    }
    *off += (size_t)wrote < out_cap - *off ? (size_t)wrote : out_cap - *off - 1;
    for (const char *p = s != NULL ? s : ""; *p != '\0' && *off + 2 < out_cap; ++p) {
        if (*p == '"' || *p == '\\') {
            out[(*off)++] = '\\';
            out[(*off)++] = *p;
        } else if ((unsigned char)*p >= 0x20) {
            out[(*off)++] = *p;
        }
    }
    if (*off + 1 < out_cap) {
        out[(*off)++] = '"';
        out[*off] = '\0';
    }
}

static void append_json_face(char *out, size_t out_cap, size_t *off, const faculty175_face_desc_t *face)
{
    if (face == NULL || out == NULL || off == NULL || *off >= out_cap) {
        return;
    }
    const int wrote = snprintf(out + *off,
                               out_cap - *off,
                               "{\"slug\":");
    if (wrote < 0) {
        return;
    }
    *off += (size_t)wrote < out_cap - *off ? (size_t)wrote : out_cap - *off - 1;
    append_json_string(out, out_cap, off, face->slug);
    if (*off >= out_cap) {
        return;
    }
    const int tail = snprintf(out + *off,
                              out_cap - *off,
                              ",\"label\":");
    if (tail < 0) {
        return;
    }
    *off += (size_t)tail < out_cap - *off ? (size_t)tail : out_cap - *off - 1;
    append_json_string(out, out_cap, off, face->label);
    if (*off >= out_cap) {
        return;
    }
    const int rest = snprintf(out + *off,
                              out_cap - *off,
                              ",\"order\":%u,\"enabled\":%s,\"nav\":%s,\"ported\":%s,\"categories\":%lu}",
                              (unsigned)faculty175_faces_order(face->id),
                              faculty175_faces_enabled(face->id) ? "true" : "false",
                              faculty175_faces_navigation_enabled(face->id) ? "true" : "false",
                              face->ported ? "true" : "false",
                              (unsigned long)face->categories);
    if (rest > 0) {
        *off += (size_t)rest < out_cap - *off ? (size_t)rest : out_cap - *off - 1;
    }
}

static esp_err_t api_faces_get(httpd_req_t *req)
{
    const faculty175_face_desc_t *current = faculty175_faces_current();
    char chunk[512];
    int wrote = snprintf(chunk, sizeof(chunk), "{\"current\":");
    if (wrote < 0) {
        return ESP_FAIL;
    }
    size_t off = (size_t)wrote;
    append_json_string(chunk, sizeof(chunk), &off, current != NULL ? current->slug : "");
    wrote = snprintf(chunk + off,
                     sizeof(chunk) - off,
                     ",\"count\":%u,\"nav_count\":%u,\"faces\":[",
                     (unsigned)faculty175_faces_count(),
                     (unsigned)faculty175_faces_enabled_count());
    if (wrote < 0) {
        return ESP_FAIL;
    }
    off += (size_t)wrote < sizeof(chunk) - off ? (size_t)wrote : sizeof(chunk) - off - 1;
    chunk[off] = '\0';
    for (size_t i = 0; i < faculty175_faces_count(); ++i) {
        const faculty175_face_desc_t *face = faculty175_faces_get((faculty175_face_id_t)i);
        if (face == NULL) {
            continue;
        }
        set_api_headers(req);
        if (i == 0) {
            ESP_RETURN_ON_ERROR(httpd_resp_sendstr_chunk(req, chunk), TAG, "send faces header");
        } else {
            ESP_RETURN_ON_ERROR(httpd_resp_sendstr_chunk(req, ","), TAG, "send faces comma");
        }

        off = 0;
        chunk[0] = '\0';
        append_json_face(chunk, sizeof(chunk), &off, face);
        ESP_RETURN_ON_ERROR(httpd_resp_sendstr_chunk(req, chunk), TAG, "send face");
    }
    set_api_headers(req);
    ESP_RETURN_ON_ERROR(httpd_resp_sendstr_chunk(req, "]}"), TAG, "send faces tail");
    return httpd_resp_send_chunk(req, NULL, 0);
}

static void json_value(const char *body, const char *key, char *out, size_t out_cap)
{
    if (out == NULL || out_cap == 0) {
        return;
    }
    out[0] = '\0';
    char needle[32];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char *p = strstr(body, needle);
    if (p == NULL) {
        return;
    }
    p = strchr(p + strlen(needle), ':');
    if (p == NULL) {
        return;
    }
    ++p;
    while (*p == ' ' || *p == '\t') {
        ++p;
    }
    if (*p != '"') {
        return;
    }
    ++p;
    size_t n = 0;
    while (*p != '\0' && *p != '"' && n + 1 < out_cap) {
        if (*p == '\\' && p[1] != '\0') {
            ++p;
        }
        out[n++] = *p++;
    }
    out[n] = '\0';
}

static esp_err_t read_request_body(httpd_req_t *req, char *body, size_t body_cap)
{
    if (body == NULL || body_cap == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t remaining = req->content_len;
    if (remaining >= body_cap) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body too large");
        return ESP_FAIL;
    }
    size_t off = 0;
    while (remaining > 0) {
        const int got = httpd_req_recv(req, body + off, remaining);
        if (got <= 0) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "read failed");
            return ESP_FAIL;
        }
        off += (size_t)got;
        remaining -= (size_t)got;
    }
    body[off] = '\0';
    return ESP_OK;
}

static esp_err_t api_face_reply(httpd_req_t *req, const faculty175_face_desc_t *face, int64_t set_us, esp_err_t set_err)
{
    char body[1024];
    size_t off = 0;
    int wrote = snprintf(body,
                         sizeof(body),
                         "{\"ok\":%s,\"err\":\"%s\",\"set_us\":%lld,\"face\":",
                         set_err == ESP_OK ? "true" : "false",
                         esp_err_to_name(set_err),
                         (long long)set_us);
    if (wrote < 0) {
        return ESP_FAIL;
    }
    off = (size_t)wrote;
    append_json_face(body, sizeof(body), &off, face);
    if (off + 2 >= sizeof(body)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "face response too large");
        return ESP_FAIL;
    }
    body[off++] = '}';
    body[off] = '\0';
    set_api_headers(req);
    return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}

static void add_json_string(cJSON *obj, const char *key, const char *value)
{
    cJSON_AddStringToObject(obj, key, value != NULL ? value : "");
}

static esp_err_t api_settings_send(httpd_req_t *req, esp_err_t apply_err)
{
    astrolabe_time_status_t time_status = {};
    astrolabe_time_status(&time_status);
    faculty175_location_settings_t loc = {};
    const bool loc_ok = faculty175_location_settings_load(&loc) == ESP_OK;

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json alloc");
        return ESP_FAIL;
    }
    cJSON_AddBoolToObject(root, "ok", apply_err == ESP_OK);
    add_json_string(root, "err", esp_err_to_name(apply_err));
    add_json_string(root, "profile", faculty175_face_profile_slug(faculty175_face_profile_current()));

    cJSON *wifi = cJSON_AddObjectToObject(root, "wifi");
    if (wifi != NULL) {
        cJSON_AddBoolToObject(wifi, "ap", faculty175_wifi_settings_ap_active());
        cJSON_AddBoolToObject(wifi, "travelRouter", faculty175_wifi_settings_travel_router_enabled());
        add_json_string(wifi, "ssid", faculty175_wifi_settings_ssid());
        add_json_string(wifi, "upstreamSsid", faculty175_wifi_settings_upstream_ssid());
        add_json_string(wifi, "url", faculty175_wifi_settings_url());
        add_json_string(wifi, "qr", faculty175_wifi_settings_qr_payload());
        add_json_string(wifi, "status", faculty175_wifi_settings_status());
        cJSON *known_array = cJSON_AddArrayToObject(wifi, "known");
        if (known_array != NULL) {
            faculty175_wifi_known_t known[FACULTY175_WIFI_KNOWN_MAX] = {};
            const size_t known_count = faculty175_wifi_settings_load_known(known, FACULTY175_WIFI_KNOWN_MAX);
            for (size_t i = 0; i < known_count; ++i) {
                cJSON *entry = cJSON_CreateObject();
                if (entry == NULL) {
                    break;
                }
                add_json_string(entry, "ssid", known[i].ssid);
                cJSON_AddBoolToObject(entry, "hasPassword", known[i].pass[0] != '\0');
                cJSON_AddBoolToObject(entry, "primary", i == 0);
                cJSON_AddItemToArray(known_array, entry);
            }
        }
    }

    cJSON *ble = cJSON_AddObjectToObject(root, "ble");
    if (ble != NULL) {
        cJSON_AddBoolToObject(ble, "enabled", faculty175_ble_enabled());
        cJSON_AddBoolToObject(ble, "advertising", faculty175_ble_advertising());
    }

    cJSON *time = cJSON_AddObjectToObject(root, "time");
    if (time != NULL) {
        cJSON_AddBoolToObject(time, "synced", time_status.synced);
        cJSON_AddNumberToObject(time, "epoch", (double)time_status.epoch);
        add_json_string(time, "tz", time_status.tz);
    }

    cJSON *location = cJSON_AddObjectToObject(root, "location");
    if (location != NULL) {
        cJSON_AddBoolToObject(location, "valid", loc_ok);
        if (loc_ok) {
            cJSON_AddNumberToObject(location, "lat", loc.lat_deg);
            cJSON_AddNumberToObject(location, "lon", loc.lon_deg);
            cJSON_AddNumberToObject(location, "updated_epoch", (double)loc.updated_epoch);
            add_json_string(location, "source", loc.source);
        }
    }

    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (body == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json print");
        return ESP_FAIL;
    }
    set_api_headers(req);
    esp_err_t err = httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
    free(body);
    return err;
}

static esp_err_t api_settings_get(httpd_req_t *req)
{
    return api_settings_send(req, ESP_OK);
}

static esp_err_t api_settings_post(httpd_req_t *req)
{
    char body[768];
    esp_err_t err = read_request_body(req, body, sizeof(body));
    if (err != ESP_OK) {
        return err;
    }

    cJSON *root = cJSON_Parse(body);
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid json");
        return ESP_FAIL;
    }

    const cJSON *tz = cJSON_GetObjectItemCaseSensitive(root, "tz");
    if (cJSON_IsString(tz) && tz->valuestring != NULL && tz->valuestring[0] != '\0') {
        err = astrolabe_time_set_timezone(tz->valuestring);
    }

    const cJSON *epoch = cJSON_GetObjectItemCaseSensitive(root, "epoch");
    if (err == ESP_OK && cJSON_IsNumber(epoch) && epoch->valuedouble > 1704067200.0) {
        err = astrolabe_time_set_epoch((time_t)epoch->valuedouble);
    }

    const cJSON *location = cJSON_GetObjectItemCaseSensitive(root, "location");
    if (err == ESP_OK && cJSON_IsObject(location)) {
        const cJSON *lat = cJSON_GetObjectItemCaseSensitive(location, "lat");
        const cJSON *lon = cJSON_GetObjectItemCaseSensitive(location, "lon");
        const cJSON *source = cJSON_GetObjectItemCaseSensitive(location, "source");
        if (cJSON_IsNumber(lat) && cJSON_IsNumber(lon)) {
            err = faculty175_location_settings_save(lat->valuedouble,
                                                    lon->valuedouble,
                                                    cJSON_IsString(source) ? source->valuestring : "pwa");
        }
    }

    const cJSON *profile = cJSON_GetObjectItemCaseSensitive(root, "profile");
    if (err == ESP_OK && cJSON_IsString(profile) && profile->valuestring != NULL && profile->valuestring[0] != '\0') {
        faculty175_face_profile_t profile_id;
        if (faculty175_face_profile_from_slug(profile->valuestring, &profile_id)) {
            err = faculty175_face_profile_apply(profile_id, true);
        } else {
            err = ESP_ERR_INVALID_ARG;
        }
    }

    const cJSON *wifi = cJSON_GetObjectItemCaseSensitive(root, "wifi");
    if (err == ESP_OK && cJSON_IsObject(wifi)) {
        const cJSON *travel_router = cJSON_GetObjectItemCaseSensitive(wifi, "travelRouter");
        if (cJSON_IsBool(travel_router)) {
            err = faculty175_wifi_settings_set_travel_router_enabled(cJSON_IsTrue(travel_router));
        }
        const cJSON *ssid = cJSON_GetObjectItemCaseSensitive(wifi, "ssid");
        const cJSON *pass = cJSON_GetObjectItemCaseSensitive(wifi, "password");
        if (err == ESP_OK && cJSON_IsString(ssid) && ssid->valuestring != NULL && ssid->valuestring[0] != '\0') {
            const cJSON *primary = cJSON_GetObjectItemCaseSensitive(wifi, "primary");
            err = faculty175_wifi_settings_add_known(ssid->valuestring,
                                                     cJSON_IsString(pass) && pass->valuestring != NULL ? pass->valuestring : "",
                                                     !cJSON_IsBool(primary) || cJSON_IsTrue(primary));
        }
        const cJSON *known = cJSON_GetObjectItemCaseSensitive(wifi, "known");
        if (err == ESP_OK && cJSON_IsArray(known)) {
            cJSON *entry = NULL;
            cJSON_ArrayForEach(entry, known) {
                const cJSON *known_ssid = cJSON_GetObjectItemCaseSensitive(entry, "ssid");
                const cJSON *known_pass = cJSON_GetObjectItemCaseSensitive(entry, "password");
                const cJSON *known_primary = cJSON_GetObjectItemCaseSensitive(entry, "primary");
                if (cJSON_IsString(known_ssid) && known_ssid->valuestring != NULL && known_ssid->valuestring[0] != '\0') {
                    err = faculty175_wifi_settings_add_known(known_ssid->valuestring,
                                                             cJSON_IsString(known_pass) && known_pass->valuestring != NULL ? known_pass->valuestring : "",
                                                             cJSON_IsTrue(known_primary));
                    if (err != ESP_OK) {
                        break;
                    }
                }
            }
        }
    }

    cJSON_Delete(root);
    return api_settings_send(req, err);
}

static void add_bssid_string(cJSON *obj, const char *key, const uint8_t bssid[6])
{
    char text[18];
    snprintf(text,
             sizeof(text),
             "%02x:%02x:%02x:%02x:%02x:%02x",
             bssid[0],
             bssid[1],
             bssid[2],
             bssid[3],
             bssid[4],
             bssid[5]);
    cJSON_AddStringToObject(obj, key, text);
}

static esp_err_t api_wifi_incidents_get(httpd_req_t *req)
{
    faculty175_wifi_incident_t events[FACULTY175_WIFI_INCIDENT_MAX] = {};
    const size_t count = faculty175_wifi_monitor_copy(events, FACULTY175_WIFI_INCIDENT_MAX);
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json alloc");
        return ESP_FAIL;
    }
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddNumberToObject(root, "count", (double)count);
    cJSON *array = cJSON_AddArrayToObject(root, "events");
    if (array != NULL) {
        for (size_t i = 0; i < count; ++i) {
            cJSON *entry = cJSON_CreateObject();
            if (entry == NULL) {
                break;
            }
            cJSON_AddNumberToObject(entry, "seq", (double)events[i].seq);
            cJSON_AddNumberToObject(entry, "uptimeMs", (double)events[i].uptime_ms);
            add_json_string(entry, "type", events[i].type);
            add_json_string(entry, "ssid", events[i].ssid);
            add_bssid_string(entry, "bssid", events[i].bssid);
            cJSON_AddNumberToObject(entry, "reason", events[i].reason);
            cJSON_AddNumberToObject(entry, "rssi", events[i].rssi);
            cJSON_AddNumberToObject(entry, "channel", events[i].channel);
            add_json_string(entry, "auth", wifi_auth_label(events[i].authmode));
            add_json_string(entry, "detail", events[i].detail);
            cJSON_AddItemToArray(array, entry);
        }
    }
    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (body == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json print");
        return ESP_FAIL;
    }
    set_api_headers(req);
    esp_err_t err = httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
    free(body);
    return err;
}

static esp_err_t lab_portal_get(httpd_req_t *req)
{
    faculty175_wifi_lab_state_t state = {};
    faculty175_wifi_lab_get_state(&state);
    const char *ssid = state.target_ssid[0] != '\0' ? state.target_ssid : "WiFi Network";
    char body[2048];
    snprintf(body,
             sizeof(body),
             "<!doctype html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" "
             "content=\"width=device-width,initial-scale=1\"><title>%s</title>"
             "<style>body{margin:0;background:#eef2f7;color:#1a2433;font-family:system-ui,sans-serif}"
             "main{max-width:24rem;margin:48px auto;padding:24px;background:#fff;border-radius:12px;"
             "box-shadow:0 8px 24px rgba(0,0,0,.08)}input,button{box-sizing:border-box;width:100%%;"
             "font:inherit;padding:12px;margin:8px 0;border-radius:8px;border:1px solid #c7d0dc}"
             "button{background:#007aff;color:#fff;border:0;font-weight:700}.muted{color:#667085;font-size:.9rem}"
             "</style></head><body><main>"
             "<h1>Sign in to WiFi</h1>"
             "<p class=\"muted\">Enter your credentials to connect to <strong>%s</strong>.</p>"
             "<form method=\"post\" action=\"/lab/portal\">"
             "<input type=\"hidden\" name=\"ssid\" value=\"%s\">"
             "<label>Email or username<input name=\"username\" maxlength=\"64\" autocomplete=\"username\"></label>"
             "<label>Password<input name=\"password\" maxlength=\"64\" type=\"password\" autocomplete=\"current-password\"></label>"
             "<button type=\"submit\">Connect</button></form>"
             "<p class=\"muted\">Authorized lab capture only.</p>"
             "</main></body></html>",
             ssid,
             ssid,
             ssid);
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t lab_portal_post(httpd_req_t *req)
{
    char body[384];
    esp_err_t err = read_request_body(req, body, sizeof(body));
    if (err != ESP_OK) {
        return err;
    }

    char username[68];
    char password[68];
    char ssid[36];
    form_value(body, "username", username, sizeof(username));
    if (username[0] == '\0') {
        form_value(body, "email", username, sizeof(username));
    }
    form_value(body, "password", password, sizeof(password));
    form_value(body, "ssid", ssid, sizeof(ssid));

    faculty175_wifi_lab_state_t state = {};
    faculty175_wifi_lab_get_state(&state);
    const char *cred_ssid = ssid[0] != '\0' ? ssid
                                                 : (state.target_ssid[0] != '\0' ? state.target_ssid : "unknown");
    faculty175_wifi_lab_record_capture(cred_ssid, password);

    char success[768];
    snprintf(success,
             sizeof(success),
             "<!doctype html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" "
             "content=\"width=device-width,initial-scale=1\"><title>Connected</title>"
             "<style>body{margin:0;background:#eef2f7;color:#1a2433;font-family:system-ui,sans-serif}"
             "main{max-width:24rem;margin:48px auto;padding:24px;background:#fff;border-radius:12px;"
             "box-shadow:0 8px 24px rgba(0,0,0,.08)}.muted{color:#667085}</style></head><body><main>"
             "<h1>Connected</h1><p>Your device should reconnect to <strong>%s</strong> shortly.</p>"
             "<p class=\"muted\">Captured for authorized lab review.</p>"
             "</main></body></html>",
             cred_ssid);
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, success, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t lab_handshake_pcap_get(httpd_req_t *req)
{
    FILE *f = fopen(FACULTY175_WIFI_LAB_PCAP_PATH, "rb");
    if (f == NULL) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "no handshake pcap yet");
        return ESP_FAIL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "pcap seek");
        return ESP_FAIL;
    }
    const long size = ftell(f);
    if (size <= 0 || size > (512 * 1024)) {
        fclose(f);
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "pcap empty");
        return ESP_FAIL;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "pcap seek");
        return ESP_FAIL;
    }

    uint8_t *buf = (uint8_t *)malloc((size_t)size);
    if (buf == NULL) {
        fclose(f);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "pcap alloc");
        return ESP_FAIL;
    }
    const size_t got = fread(buf, 1, (size_t)size, f);
    fclose(f);
    if (got != (size_t)size) {
        free(buf);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "pcap read");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/vnd.tcpdump.pcap");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"handshake.pcap\"");
    const esp_err_t err = httpd_resp_send(req, (const char *)buf, (size_t)size);
    free(buf);
    return err;
}

static esp_err_t api_face_get(httpd_req_t *req)
{
    char query[96] = {};
    char slug[32] = {};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        if (httpd_query_key_value(query, "slug", slug, sizeof(slug)) == ESP_OK) {
            url_decode_in_place(slug);
        }
    }

    const faculty175_face_desc_t *face = slug[0] != '\0' ? faculty175_faces_find(slug) : faculty175_faces_current();
    if (face == NULL) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "unknown face");
        return ESP_FAIL;
    }
    int64_t set_us = 0;
    esp_err_t err = ESP_OK;
    if (slug[0] != '\0') {
        const int64_t start = esp_timer_get_time();
        err = faculty175_faces_set_runtime(face->id);
        set_us = esp_timer_get_time() - start;
    }
    return api_face_reply(req, faculty175_faces_current(), set_us, err);
}

static esp_err_t api_face_post(httpd_req_t *req)
{
    char body[192];
    const esp_err_t read_err = read_request_body(req, body, sizeof(body));
    if (read_err != ESP_OK) {
        return read_err;
    }

    char slug[32] = {};
    form_value(body, "slug", slug, sizeof(slug));
    if (slug[0] == '\0') {
        json_value(body, "slug", slug, sizeof(slug));
    }
    if (slug[0] == '\0') {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "slug required");
        return ESP_FAIL;
    }

    const faculty175_face_desc_t *face = faculty175_faces_find(slug);
    if (face == NULL) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "unknown face");
        return ESP_FAIL;
    }
    const int64_t start = esp_timer_get_time();
    const esp_err_t err = faculty175_faces_set_runtime(face->id);
    const int64_t set_us = esp_timer_get_time() - start;
    return api_face_reply(req, faculty175_faces_current(), set_us, err);
}

static esp_err_t api_voice_post(httpd_req_t *req)
{
    char body[192];
    const esp_err_t read_err = read_request_body(req, body, sizeof(body));
    if (read_err != ESP_OK) {
        return read_err;
    }

    char action[16] = {};
    form_value(body, "action", action, sizeof(action));
    if (action[0] == '\0') {
        json_value(body, "action", action, sizeof(action));
    }
    if (action[0] == '\0') {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "action required");
        return ESP_FAIL;
    }

    esp_err_t err = ESP_OK;
    bool accepted = false;
    if (strcasecmp(action, "tts") == 0 || strcasecmp(action, "read") == 0) {
        accepted = faculty175_request_current_face_tts();
        err = accepted ? ESP_OK : ESP_FAIL;
    } else if (strcasecmp(action, "stt") == 0 || strcasecmp(action, "capture") == 0) {
        uint32_t capture_ms = 9000;
        char ms_text[16] = {};
        form_value(body, "ms", ms_text, sizeof(ms_text));
        if (ms_text[0] == '\0') {
            json_value(body, "ms", ms_text, sizeof(ms_text));
        }
        if (ms_text[0] != '\0') {
            const unsigned long parsed = strtoul(ms_text, NULL, 10);
            if (parsed >= 1000 && parsed <= 30000) {
                capture_ms = (uint32_t)parsed;
            }
        }
        err = faculty175_request_qa_stt(capture_ms);
        accepted = err == ESP_OK;
    } else {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "unknown action");
        return ESP_FAIL;
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json alloc");
        return ESP_FAIL;
    }
    cJSON_AddBoolToObject(root, "ok", err == ESP_OK);
    cJSON_AddBoolToObject(root, "accepted", accepted);
    add_json_string(root, "action", action);
    add_json_string(root, "err", esp_err_to_name(err));
    char *reply = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (reply == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json print");
        return ESP_FAIL;
    }
    set_api_headers(req);
    esp_err_t send_err = httpd_resp_send(req, reply, HTTPD_RESP_USE_STRLEN);
    free(reply);
    return send_err;
}

static esp_err_t screen_bmp_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "image/bmp");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    faculty175_display_lock();
    esp_err_t err = faculty175_display_write_bmp565(send_chunk_cb, req);
    faculty175_display_unlock();
    if (err == ESP_OK) {
        err = httpd_resp_send_chunk(req, NULL, 0);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "screen bmp failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t faculty175_screen_http_start(const esp_ip4_addr_t *ip)
{
#if !FACULTY175_SCREEN_HTTP_RUNTIME_ENABLED
    static bool warned;
    if (ip != NULL) {
        s_ip = *ip;
    }
    if (!warned) {
        warned = true;
        ESP_LOGI(TAG, "runtime disabled");
    }
    return ESP_OK;
#else
    if (ip != NULL) {
        s_ip = *ip;
    }
    if (s_httpd != NULL) {
        ESP_LOGI(TAG, "ready http://" IPSTR "/ (GET /screen.bmp)", IP2STR(&s_ip));
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.stack_size = FACULTY175_SCREEN_HTTP_STACK_SIZE;
    config.max_open_sockets = 4;
    config.max_uri_handlers = 16;
    config.lru_purge_enable = true;

    esp_err_t err = ESP_FAIL;
    for (uint32_t attempt = 0; attempt < FACULTY175_SCREEN_HTTP_START_ATTEMPTS; ++attempt) {
        config.stack_size = attempt == 0 ? FACULTY175_SCREEN_HTTP_STACK_SIZE
                                         : FACULTY175_SCREEN_HTTP_FALLBACK_STACK_SIZE;
        err = httpd_start(&s_httpd, &config);
        if (err == ESP_OK) {
            break;
        }
        ESP_LOGW(TAG,
                 "start attempt %u failed stack=%u: %s",
                 (unsigned)(attempt + 1),
                 (unsigned)config.stack_size,
                 esp_err_to_name(err));
        if (s_httpd != NULL) {
            httpd_handle_t httpd = s_httpd;
            s_httpd = NULL;
            (void)httpd_stop(httpd);
        }
        vTaskDelay(pdMS_TO_TICKS(120 + (attempt * 120)));
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "start failed: %s", esp_err_to_name(err));
        return err;
    }

    const httpd_uri_t root_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_get,
        .user_ctx = NULL,
    };
    const httpd_uri_t screen_uri = {
        .uri = "/screen.bmp",
        .method = HTTP_GET,
        .handler = screen_bmp_get,
        .user_ctx = NULL,
    };
    const httpd_uri_t wifi_get_uri = {
        .uri = "/wifi",
        .method = HTTP_GET,
        .handler = wifi_get,
        .user_ctx = NULL,
    };
    const httpd_uri_t wifi_post_uri = {
        .uri = "/wifi",
        .method = HTTP_POST,
        .handler = wifi_post,
        .user_ctx = NULL,
    };
    const httpd_uri_t wifi_scan_uri = {
        .uri = "/wifi/scan",
        .method = HTTP_GET,
        .handler = wifi_scan_get,
        .user_ctx = NULL,
    };
    const httpd_uri_t api_faces_uri = {
        .uri = "/api/faces",
        .method = HTTP_GET,
        .handler = api_faces_get,
        .user_ctx = NULL,
    };
    const httpd_uri_t api_face_get_uri = {
        .uri = "/api/face",
        .method = HTTP_GET,
        .handler = api_face_get,
        .user_ctx = NULL,
    };
    const httpd_uri_t api_face_post_uri = {
        .uri = "/api/face",
        .method = HTTP_POST,
        .handler = api_face_post,
        .user_ctx = NULL,
    };
    const httpd_uri_t api_voice_post_uri = {
        .uri = "/api/voice",
        .method = HTTP_POST,
        .handler = api_voice_post,
        .user_ctx = NULL,
    };
    const httpd_uri_t api_settings_get_uri = {
        .uri = "/api/settings",
        .method = HTTP_GET,
        .handler = api_settings_get,
        .user_ctx = NULL,
    };
    const httpd_uri_t api_settings_post_uri = {
        .uri = "/api/settings",
        .method = HTTP_POST,
        .handler = api_settings_post,
        .user_ctx = NULL,
    };
    const httpd_uri_t api_settings_options_uri = {
        .uri = "/api/settings",
        .method = HTTP_OPTIONS,
        .handler = api_options,
        .user_ctx = NULL,
    };
    const httpd_uri_t lab_portal_get_uri = {
        .uri = "/lab/portal",
        .method = HTTP_GET,
        .handler = lab_portal_get,
        .user_ctx = NULL,
    };
    const httpd_uri_t lab_portal_post_uri = {
        .uri = "/lab/portal",
        .method = HTTP_POST,
        .handler = lab_portal_post,
        .user_ctx = NULL,
    };
    const httpd_uri_t lab_handshake_pcap_uri = {
        .uri = "/lab/handshake.pcap",
        .method = HTTP_GET,
        .handler = lab_handshake_pcap_get,
        .user_ctx = NULL,
    };
    const httpd_uri_t api_wifi_incidents_uri = {
        .uri = "/api/wifi/incidents",
        .method = HTTP_GET,
        .handler = api_wifi_incidents_get,
        .user_ctx = NULL,
    };
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_httpd, &root_uri), TAG, "register /");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_httpd, &screen_uri), TAG, "register /screen.bmp");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_httpd, &wifi_get_uri), TAG, "register GET /wifi");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_httpd, &wifi_post_uri), TAG, "register POST /wifi");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_httpd, &wifi_scan_uri), TAG, "register GET /wifi/scan");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_httpd, &api_faces_uri), TAG, "register GET /api/faces");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_httpd, &api_face_get_uri), TAG, "register GET /api/face");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_httpd, &api_face_post_uri), TAG, "register POST /api/face");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_httpd, &api_voice_post_uri), TAG, "register POST /api/voice");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_httpd, &api_settings_get_uri), TAG, "register GET /api/settings");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_httpd, &api_settings_post_uri), TAG, "register POST /api/settings");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_httpd, &api_settings_options_uri), TAG, "register OPTIONS /api/settings");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_httpd, &lab_portal_get_uri), TAG, "register GET /lab/portal");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_httpd, &lab_portal_post_uri), TAG, "register POST /lab/portal");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_httpd, &lab_handshake_pcap_uri), TAG, "register GET /lab/handshake.pcap");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_httpd, &api_wifi_incidents_uri), TAG, "register GET /api/wifi/incidents");

    ESP_LOGI(TAG, "ready http://" IPSTR "/ (GET /screen.bmp, /wifi, /api/faces)", IP2STR(&s_ip));
    printf("Astrolabe WiFi settings: http://" IPSTR "/wifi\n", IP2STR(&s_ip));
    printf("Screen over WiFi: http://" IPSTR "/  (GET /screen.bmp)\n", IP2STR(&s_ip));
    printf("Face control over WiFi: http://" IPSTR "/api/faces  (GET /api/faces, POST /api/face slug=<slug>)\n",
           IP2STR(&s_ip));
    fflush(stdout);
    return ESP_OK;
#endif
}

void faculty175_screen_http_stop(void)
{
#if !FACULTY175_SCREEN_HTTP_RUNTIME_ENABLED
    return;
#else
    if (s_httpd != NULL) {
        httpd_handle_t httpd = s_httpd;
        s_httpd = NULL;
        (void)httpd_stop(httpd);
    }
#endif
}
