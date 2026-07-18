#include "faculty175_screen_http.h"

#include <stdio.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "esp_check.h"
#include "esp_app_desc.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "cJSON.h"
#include "mdns.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "lwip/tcpip.h"

#include "astrolabe_time.h"
#include "faculty175_ble.h"
#include "faculty175_board.h"
#include "faculty175_breath.h"
#include "faculty175_charts.h"
#include "faculty175_device_settings.h"
#include "faculty175_deep_sleep.h"
#include "faculty175_face_profile.h"
#include "faculty175_faces.h"
#include "faculty175_power_metrics.h"
#include "faculty175_power_history.h"
#include "faculty175_km_http.h"
#include "faculty175_serial.h"
#include "faculty175_wifi_lab.h"
#include "faculty175_wifi_monitor.h"
#include "faculty175_wifi_settings.h"

static const char *TAG = "faculty175_screen_http";
static httpd_handle_t s_httpd;
static esp_ip4_addr_t s_ip;
static bool s_mdns_started;
static char s_mdns_hostname[FACULTY175_WIFI_HOSTNAME_MAX + 1] = "astrolabe-0000";
static struct tcp_pcb *s_wake_listener;
static atomic_bool s_wake_requested;

// The 1.75C QA path relies on the lightweight screen/settings HTTP server.
#define FACULTY175_SCREEN_HTTP_RUNTIME_ENABLED 1
#if defined(ASTROLABE_FORCE_VARIANT_LUNASAY)
#define FACULTY175_SCREEN_HTTP_STACK_SIZE 3584
#define FACULTY175_SCREEN_HTTP_FALLBACK_STACK_SIZE 3072
#else
#define FACULTY175_SCREEN_HTTP_STACK_SIZE 6144
#define FACULTY175_SCREEN_HTTP_FALLBACK_STACK_SIZE 4096
#endif
#define FACULTY175_SCREEN_HTTP_START_ATTEMPTS 4

static void add_json_string(cJSON *obj, const char *key, const char *value);
static err_t wake_listener_release(struct tcp_pcb *pcb);
static err_t wake_listener_sent(void *arg, struct tcp_pcb *pcb, uint16_t len);
static err_t wake_listener_poll(void *arg, struct tcp_pcb *pcb);

static void wake_listener_close_in_core(void)
{
    if (s_wake_listener == NULL) {
        return;
    }
    struct tcp_pcb *listener = s_wake_listener;
    s_wake_listener = NULL;
    tcp_accept(listener, NULL);
    if (tcp_close(listener) != ERR_OK) {
        tcp_abort(listener);
    }
}

static err_t wake_listener_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
    (void)arg;
    if (p == NULL || err != ERR_OK) {
        if (p != NULL) {
            pbuf_free(p);
        }
        if (tcp_close(pcb) != ERR_OK) {
            tcp_abort(pcb);
        }
        return ERR_OK;
    }

    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);
    wake_listener_close_in_core();

    static const char response[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Cache-Control: no-store\r\n"
        "Connection: close\r\n"
        "Content-Length: 250\r\n\r\n"
        "<!doctype html><meta name=viewport content='width=device-width'><title>LunaSay</title>"
        "<body style='background:#100b19;color:#eee;font:18px system-ui;padding:2rem'>"
        "Opening LunaSay settings&hellip;<script>setTimeout(()=>location.reload(),1800)</script>";
    const err_t write_err = tcp_write(pcb, response, sizeof(response) - 1u, TCP_WRITE_FLAG_COPY);
    if (write_err != ERR_OK) {
        return wake_listener_release(pcb);
    }
    tcp_sent(pcb, wake_listener_sent);
    tcp_poll(pcb, wake_listener_poll, 2);
    (void)tcp_output(pcb);
    return ERR_OK;
}

static err_t wake_listener_accept(void *arg, struct tcp_pcb *pcb, err_t err)
{
    (void)arg;
    if (err != ERR_OK || pcb == NULL) {
        return ERR_VAL;
    }
    tcp_setprio(pcb, TCP_PRIO_MIN);
    tcp_nagle_disable(pcb);
    tcp_recv(pcb, wake_listener_recv);
    return ERR_OK;
}

static err_t wake_listener_release(struct tcp_pcb *pcb)
{
    tcp_sent(pcb, NULL);
    tcp_recv(pcb, NULL);
    tcp_poll(pcb, NULL, 0);
    tcp_abort(pcb);
    atomic_store_explicit(&s_wake_requested, true, memory_order_release);
    return ERR_ABRT;
}

static err_t wake_listener_sent(void *arg, struct tcp_pcb *pcb, uint16_t len)
{
    (void)arg;
    (void)len;
    return wake_listener_release(pcb);
}

static err_t wake_listener_poll(void *arg, struct tcp_pcb *pcb)
{
    (void)arg;
    return wake_listener_release(pcb);
}

static void wake_listener_start_in_core(void *arg)
{
    (void)arg;
    if (s_wake_listener != NULL || s_httpd != NULL) {
        return;
    }
    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (pcb == NULL) {
        ESP_LOGE(TAG, "wake listener pcb allocation failed");
        return;
    }
    const err_t bind_err = tcp_bind(pcb, IP_ANY_TYPE, 80);
    if (bind_err != ERR_OK) {
        ESP_LOGE(TAG, "wake listener bind failed: %d", (int)bind_err);
        tcp_abort(pcb);
        return;
    }
    s_wake_listener = tcp_listen_with_backlog(pcb, 1);
    if (s_wake_listener == NULL) {
        ESP_LOGE(TAG, "wake listener start failed");
        tcp_abort(pcb);
        return;
    }
    tcp_accept(s_wake_listener, wake_listener_accept);
    ESP_LOGI(TAG, "wake listener ready on port 80");
}

static void wake_listener_stop_in_core(void *arg)
{
    (void)arg;
    wake_listener_close_in_core();
}

esp_err_t faculty175_screen_http_wake_listener_start(void)
{
    atomic_store_explicit(&s_wake_requested, false, memory_order_release);
    return tcpip_callback(wake_listener_start_in_core, NULL) == ERR_OK ? ESP_OK : ESP_FAIL;
}

void faculty175_screen_http_wake_listener_stop(void)
{
    (void)tcpip_callback(wake_listener_stop_in_core, NULL);
}

bool faculty175_screen_http_take_wake_request(void)
{
    return atomic_exchange_explicit(&s_wake_requested, false, memory_order_acq_rel);
}

static esp_err_t send_chunk_cb(void *ctx, const uint8_t *data, size_t len)
{
    return httpd_resp_send_chunk((httpd_req_t *)ctx, (const char *)data, len);
}

static void set_api_headers(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    /* The embedded PWA is same-origin. Omitting permissive CORS prevents an
     * unrelated web page from reading profiles or issuing JSON writes. */
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(req, "Referrer-Policy", "no-referrer");
}

static esp_err_t api_options(httpd_req_t *req)
{
    set_api_headers(req);
    return httpd_resp_send(req, "", 0);
}

static esp_err_t api_breath_get(httpd_req_t *req)
{
    faculty175_breath_status_t status = {};
    faculty175_breath_status(&status);
    const uint32_t uptime_ms = (uint32_t)(esp_timer_get_time() / 1000);
    const uint32_t age_ms = status.updated_ms != 0 ? uptime_ms - status.updated_ms : 0;
    const char axis[] = { status.axis != '\0' ? status.axis : '-', '\0' };

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json alloc");
        return ESP_FAIL;
    }
    cJSON_AddNumberToObject(root, "schema", 1);
    cJSON_AddNumberToObject(root, "uptime_ms", uptime_ms);
    cJSON_AddNumberToObject(root, "updated_ms", status.updated_ms);
    cJSON_AddNumberToObject(root, "age_ms", age_ms);
    add_json_string(root, "state", faculty175_breath_state_name(status.state));
    cJSON_AddBoolToObject(root, "sensor_valid", status.state != FACULTY175_BREATH_SENSOR_MISSING);
    cJSON_AddNumberToObject(root, "rate_bpm", status.rate_bpm);
    cJSON_AddNumberToObject(root, "confidence", status.confidence);
    cJSON_AddNumberToObject(root, "amplitude_deg", status.amplitude_deg);
    cJSON_AddNumberToObject(root, "waveform", status.waveform);
    add_json_string(root, "axis", axis);
    cJSON_AddNumberToObject(root, "pitch_deg", status.pitch_deg);
    cJSON_AddNumberToObject(root, "roll_deg", status.roll_deg);
    cJSON_AddNumberToObject(root, "signal_deg", status.signal_deg);
    cJSON_AddNumberToObject(root, "motion_rate_dps", status.motion_rate_dps);
    cJSON_AddNumberToObject(root, "samples", status.samples);
    cJSON_AddNumberToObject(root, "breaths", status.breaths);
    cJSON_AddNumberToObject(root, "calibration_ms", status.calibration_ms);

    cJSON *detected = cJSON_AddObjectToObject(root, "detected");
    if (detected != NULL) {
        add_json_string(detected, "phase", faculty175_breath_phase_name(status.detected_phase));
        cJSON_AddNumberToObject(detected, "phase_ms", status.detected_phase_ms);
        cJSON_AddNumberToObject(detected, "confidence", status.phase_confidence);
        cJSON_AddNumberToObject(detected, "velocity", status.phase_velocity);
    }

    cJSON *guide = cJSON_AddObjectToObject(root, "guide");
    if (guide != NULL) {
        add_json_string(guide, "phase", faculty175_breath_guide_phase_name(status.guide_phase));
        cJSON_AddNumberToObject(guide, "phase_ms", status.guide_phase_ms);
        cJSON_AddNumberToObject(guide, "cycle", status.guide_cycle);
        cJSON_AddNumberToObject(guide, "target", status.guide_target);
        cJSON_AddNumberToObject(guide, "alignment", status.guide_alignment);
    }

    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (body == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json print");
        return ESP_FAIL;
    }
    set_api_headers(req);
    const esp_err_t err = httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
    free(body);
    return err;
}

static esp_err_t api_battery_get(httpd_req_t *req)
{
    faculty175_power_metrics_t status = {};
    faculty175_power_metrics_status(&status);
    const bool on_battery = status.pmu.present && status.pmu.battery_present &&
                            !status.pmu.vbus_in && !status.pmu.charging;
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json alloc");
        return ESP_FAIL;
    }
    cJSON_AddNumberToObject(root, "schema", 2);
    cJSON_AddNumberToObject(root,
                           "epoch_s",
                           astrolabe_time_valid() ? (double)astrolabe_time_now() : 0);
    cJSON_AddNumberToObject(root, "uptime_ms", status.uptime_ms);
    cJSON_AddNumberToObject(root, "reset_reason", esp_reset_reason());
    add_json_string(root, "mode", faculty175_power_mode_name(status.mode));
    add_json_string(root, "source", on_battery ? "battery" : "usb");
    cJSON_AddBoolToObject(root, "wifi_active", status.wifi_active);
    cJSON_AddBoolToObject(root, "ble_active", status.ble_active);

    const esp_app_desc_t *app = esp_app_get_description();
    cJSON *firmware = cJSON_AddObjectToObject(root, "firmware");
    if (firmware != NULL && app != NULL) {
        add_json_string(firmware, "project", app->project_name);
        add_json_string(firmware, "version", app->version);
        add_json_string(firmware, "build_date", app->date);
        add_json_string(firmware, "build_time", app->time);
        add_json_string(firmware, "idf", app->idf_ver);
    }

    faculty175_deep_sleep_status_t sleep = {0};
    faculty175_deep_sleep_status(&sleep);
    cJSON *deep_sleep = cJSON_AddObjectToObject(root, "deep_sleep");
    if (deep_sleep != NULL) {
        cJSON_AddBoolToObject(deep_sleep, "pending", sleep.pending);
        cJSON_AddBoolToObject(deep_sleep, "retained", sleep.retained);
        cJSON_AddBoolToObject(deep_sleep, "completed", sleep.completed);
        cJSON_AddNumberToObject(deep_sleep, "requested_s", sleep.requested_sleep_s);
        cJSON_AddNumberToObject(deep_sleep, "started_epoch_s", sleep.started_epoch_s);
        cJSON_AddNumberToObject(deep_sleep, "start_percent", sleep.start_battery_percent);
        cJSON_AddNumberToObject(deep_sleep, "start_voltage_mv", sleep.start_battery_mv);
        cJSON_AddNumberToObject(deep_sleep, "wake_cause", sleep.wake_cause);
    }

    cJSON *scenario = cJSON_AddObjectToObject(root, "scenario");
    if (scenario != NULL) {
        add_json_string(scenario, "name", faculty175_power_scenario_name(status.scenario));
        cJSON_AddBoolToObject(scenario,
                              "active",
                              status.scenario != FACULTY175_POWER_SCENARIO_NORMAL);
        cJSON_AddNumberToObject(scenario, "elapsed_ms", status.scenario_elapsed_ms);
        cJSON_AddNumberToObject(scenario, "remaining_ms", status.scenario_remaining_ms);
        cJSON_AddBoolToObject(scenario,
                              "wifi_expected",
                              faculty175_power_scenario_wifi_enabled(status.scenario));
        cJSON *scenario_usage = cJSON_AddObjectToObject(scenario, "usage");
        if (scenario_usage != NULL) {
            cJSON_AddNumberToObject(scenario_usage, "awake_ms", status.scenario_awake_ms);
            cJSON_AddNumberToObject(scenario_usage, "breathing_ms", status.scenario_breathing_ms);
            cJSON_AddNumberToObject(scenario_usage, "dimmed_ms", status.scenario_dimmed_ms);
            cJSON_AddNumberToObject(scenario_usage, "asleep_ms", status.scenario_asleep_ms);
            cJSON_AddNumberToObject(scenario_usage, "battery_ms", status.scenario_battery_ms);
            cJSON_AddNumberToObject(scenario_usage, "wifi_ms", status.scenario_wifi_ms);
            cJSON_AddNumberToObject(scenario_usage, "ble_ms", status.scenario_ble_ms);
        }
    }

    cJSON *battery = cJSON_AddObjectToObject(root, "battery");
    if (battery != NULL) {
        cJSON_AddBoolToObject(battery, "pmu_present", status.pmu.present);
        cJSON_AddBoolToObject(battery, "present", status.pmu.battery_present);
        cJSON_AddNumberToObject(battery, "percent", status.pmu.battery_percent);
        cJSON_AddNumberToObject(battery, "voltage_mv", status.pmu.battery_mv);
        cJSON_AddBoolToObject(battery, "vbus", status.pmu.vbus_in);
        cJSON_AddBoolToObject(battery, "charging", status.pmu.charging);
        cJSON_AddBoolToObject(battery, "discharging", status.pmu.discharging);
        cJSON_AddNumberToObject(battery, "power_on_source_flags", status.pmu.power_on_source_flags);
        cJSON_AddNumberToObject(battery, "power_off_source_flags", status.pmu.power_off_source_flags);
    }
    cJSON *usage = cJSON_AddObjectToObject(root, "usage");
    if (usage != NULL) {
        cJSON_AddNumberToObject(usage, "awake_ms", status.awake_ms);
        cJSON_AddNumberToObject(usage, "breathing_ms", status.breathing_ms);
        cJSON_AddNumberToObject(usage, "dimmed_ms", status.dimmed_ms);
        cJSON_AddNumberToObject(usage, "asleep_ms", status.asleep_ms);
    }

    faculty175_power_history_sample_t *samples = calloc(FACULTY175_POWER_HISTORY_CAPACITY,
                                                        sizeof(*samples));
    const size_t history_count = samples != NULL
                                     ? faculty175_power_history_load(samples,
                                                                     FACULTY175_POWER_HISTORY_CAPACITY)
                                     : 0;
    bool estimate_valid = status.estimate_valid;
    uint32_t estimate_elapsed_ms = status.discharge_elapsed_ms;
    int estimate_drop_percent = status.discharge_drop_percent;
    float estimate_percent_per_hour = status.discharge_percent_per_hour;
    float estimate_remaining_hours = status.remaining_hours;
    const char *estimate_basis = status.estimate_valid ? "observed-discharge-slope"
                                                       : (on_battery ? "collecting-discharge-data"
                                                                     : "external-power");
    if (on_battery && history_count > 0 && astrolabe_time_valid()) {
        const uint32_t now_s = (uint32_t)astrolabe_time_now();
        for (size_t i = history_count; i-- > 0;) {
            const faculty175_power_history_sample_t *sample = &samples[i];
            if ((sample->flags & FACULTY175_POWER_HISTORY_BATTERY_PRESENT) == 0 ||
                (sample->flags & (FACULTY175_POWER_HISTORY_VBUS |
                                  FACULTY175_POWER_HISTORY_CHARGING)) != 0) {
                break;
            }
            if (sample->epoch_s > now_s || sample->battery_percent < status.pmu.battery_percent) {
                continue;
            }
            const uint32_t elapsed_s = now_s - sample->epoch_s;
            const int drop = (int)sample->battery_percent - status.pmu.battery_percent;
            if (elapsed_s >= 15u * 60u && drop >= 1 && elapsed_s * 1000u > estimate_elapsed_ms) {
                estimate_valid = true;
                estimate_elapsed_ms = elapsed_s * 1000u;
                estimate_drop_percent = drop;
                estimate_percent_per_hour = (float)drop * 3600.0f / (float)elapsed_s;
                estimate_remaining_hours = estimate_percent_per_hour > 0.0f
                                               ? (float)status.pmu.battery_percent /
                                                     estimate_percent_per_hour
                                               : 0.0f;
                estimate_basis = "flash-history-discharge-slope";
            }
        }
    }
    cJSON *estimate = cJSON_AddObjectToObject(root, "estimate");
    if (estimate != NULL) {
        cJSON_AddBoolToObject(estimate, "valid", estimate_valid);
        cJSON_AddNumberToObject(estimate, "discharge_elapsed_ms", estimate_elapsed_ms);
        cJSON_AddNumberToObject(estimate, "drop_percent", estimate_drop_percent);
        cJSON_AddNumberToObject(estimate, "percent_per_hour", estimate_percent_per_hour);
        cJSON_AddNumberToObject(estimate, "remaining_hours", estimate_remaining_hours);
        cJSON_AddNumberToObject(estimate, "remaining_ms", estimate_remaining_hours * 3600000.0f);
        add_json_string(estimate, "basis", estimate_basis);
    }

    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (body == NULL) {
        free(samples);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json print");
        return ESP_FAIL;
    }
    const size_t body_len = strlen(body);
    if (body_len == 0 || body[body_len - 1] != '}') {
        free(samples);
        free(body);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json shape");
        return ESP_FAIL;
    }
    body[body_len - 1] = '\0';
    set_api_headers(req);
    esp_err_t err = httpd_resp_send_chunk(req, body, body_len - 1);
    free(body);
    if (err == ESP_OK) {
        err = httpd_resp_send_chunk(req, ",\"history\":[", HTTPD_RESP_USE_STRLEN);
    }
    char entry[320];
    for (size_t i = 0; err == ESP_OK && i < history_count; ++i) {
        const faculty175_power_history_sample_t *sample = &samples[i];
        const int wrote = snprintf(
            entry,
            sizeof(entry),
            "%s{\"epoch_s\":%lu,\"percent\":%u,\"voltage_mv\":%u,"
            "\"battery_present\":%s,\"vbus\":%s,\"charging\":%s,\"discharging\":%s,"
            "\"mode\":\"%s\",\"scenario\":\"%s\",\"wifi\":%s,\"ble\":%s}",
            i == 0 ? "" : ",",
            (unsigned long)sample->epoch_s,
            (unsigned)sample->battery_percent,
            (unsigned)sample->battery_mv,
            (sample->flags & FACULTY175_POWER_HISTORY_BATTERY_PRESENT) != 0 ? "true" : "false",
            (sample->flags & FACULTY175_POWER_HISTORY_VBUS) != 0 ? "true" : "false",
            (sample->flags & FACULTY175_POWER_HISTORY_CHARGING) != 0 ? "true" : "false",
            (sample->flags & FACULTY175_POWER_HISTORY_DISCHARGING) != 0 ? "true" : "false",
            faculty175_power_mode_name((faculty175_power_mode_t)sample->mode),
            faculty175_power_scenario_name((faculty175_power_scenario_t)sample->scenario),
            (sample->flags & FACULTY175_POWER_HISTORY_WIFI) != 0 ? "true" : "false",
            (sample->flags & FACULTY175_POWER_HISTORY_BLE) != 0 ? "true" : "false");
        if (wrote < 0 || (size_t)wrote >= sizeof(entry)) {
            err = ESP_ERR_INVALID_SIZE;
            break;
        }
        err = httpd_resp_send_chunk(req, entry, (ssize_t)wrote);
    }
    free(samples);
    if (err == ESP_OK) {
        err = httpd_resp_send_chunk(req, "]}", 2);
    }
    if (err == ESP_OK) {
        err = httpd_resp_send_chunk(req, NULL, 0);
    }
    return err;
}

static const char k_battery_page[] =
    "<!doctype html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<meta name=\"theme-color\" content=\"#061018\"><link rel=\"manifest\" href=\"/manifest.webmanifest\">"
    "<link rel=\"icon\" href=\"/favicon.svg\" type=\"image/svg+xml\"><link rel=\"apple-touch-icon\" href=\"/favicon.svg\">"
    "<title>Astrolabe Battery</title><style>:root{color-scheme:dark;--bg:#05080d;--panel:#0c151d;--line:#203947;"
    "--green:#73f0a7;--cyan:#62d7ff;--gold:#ffd167;--text:#e5f8ff;--muted:#8ba7b2}*{box-sizing:border-box}"
    "html,body{min-height:100%;height:auto;overflow-x:hidden;overflow-y:auto}body{margin:0;background:radial-gradient(circle at 30% -10%,"
    "#19352e,var(--bg) 48%);color:var(--text);font:15px system-ui,sans-serif;-webkit-overflow-scrolling:touch}"
    "main{width:min(1050px,94vw);margin:auto;padding:24px 0 max(72px,env(safe-area-inset-bottom))}header{display:flex;"
    "justify-content:space-between;align-items:end;gap:12px;margin-bottom:18px}h1{margin:0;font-size:clamp(1.4rem,4vw,2.2rem);"
    "font-weight:500;letter-spacing:.13em}.status{color:var(--green);text-align:right}.bad{color:#ff7c88}.cards{display:grid;"
    "grid-template-columns:repeat(5,1fr);gap:10px;margin-bottom:12px}.card,.plot{background:#0c151df0;border:1px solid var(--line);"
    "border-radius:14px;box-shadow:0 12px 40px #0005}.card{padding:14px}.label{color:var(--muted);font-size:.76rem;"
    "letter-spacing:.09em;text-transform:uppercase}.value{font-size:1.5rem;margin-top:5px}.plots{display:grid;grid-template-columns:2fr 1fr;"
    "gap:12px}.plot{padding:14px}.plot h2{font-size:.82rem;color:var(--muted);font-weight:500;letter-spacing:.08em;"
    "text-transform:uppercase;margin:0 0 8px}.charge{grid-row:span 2}.charge canvas{height:390px}canvas{width:100%;height:180px;"
    "display:block}.usage{margin-top:14px}.bar{height:18px;border-radius:9px;overflow:hidden;display:flex;background:#14232c}.bar i{height:100%}"
    ".legend{display:flex;gap:14px;flex-wrap:wrap;color:var(--muted);font-size:.78rem;margin-top:8px}.foot{color:var(--muted);"
    "font-size:.78rem;margin-top:6px}@media(max-width:760px){.cards{grid-template-columns:repeat(2,1fr)}.plots{grid-template-columns:1fr}"
    ".charge{grid-row:auto}.charge canvas{height:270px}}</style></head><body><main><header><div><h1>ASTROLABE · BATTERY</h1>"
    "<div class=\"foot\">24-hour flash history · 15-minute samples · discharge-based forecast</div></div><div id=\"status\" class=\"status\">Connecting…</div>"
    "</header><section class=\"cards\"><div class=\"card\"><div class=\"label\">Charge</div><div class=\"value\"><span id=\"percent\">—</span>%</div>"
    "</div><div class=\"card\"><div class=\"label\">Voltage</div><div class=\"value\"><span id=\"voltage\">—</span> V</div></div>"
    "<div class=\"card\"><div class=\"label\">Power</div><div id=\"source\" class=\"value\">—</div></div><div class=\"card\">"
    "<div class=\"label\">Estimated remaining</div><div id=\"remaining\" class=\"value\">—</div></div><div class=\"card\">"
    "<div class=\"label\">Discharge rate</div><div id=\"rate\" class=\"value\">—</div></div></section><section class=\"plots\">"
    "<div class=\"plot charge\"><h2>Battery charge · solid measured / dotted forecast</h2><canvas id=\"chargePlot\"></canvas>"
    "<div id=\"forecastNote\" class=\"foot\"></div></div><div class=\"plot\"><h2>Battery voltage</h2><canvas id=\"voltagePlot\"></canvas></div>"
    "<div class=\"plot\"><h2>Mode utilization</h2><div class=\"usage\"><div class=\"bar\"><i id=\"awakeBar\" style=\"background:#62d7ff\"></i>"
    "<i id=\"breathingBar\" style=\"background:#73f0a7\"></i><i id=\"dimmedBar\" style=\"background:#ffd167\"></i>"
    "<i id=\"asleepBar\" style=\"background:#765eaa\"></i></div><div class=\"legend\"><span>Awake</span><span>Breathing</span>"
    "<span>Dimmed</span><span>Asleep</span></div></div></div></section></main><script>"
    "const N=1500,data={percent:[],voltage:[],time:[]},el=id=>document.getElementById(id),statusEl=el('status');let latest=null,historyLoaded=false;"
    "function size(c){const d=devicePixelRatio||1,w=c.clientWidth,h=c.clientHeight;if(c.width!==Math.round(w*d)||c.height!==Math.round(h*d)){"
    "c.width=Math.round(w*d);c.height=Math.round(h*d)}const x=c.getContext('2d');x.setTransform(d,0,0,d,0,0);x.clearRect(0,0,w,h);return{x,w,h}}"
    "function grid(x,w,h,lo,hi){x.strokeStyle='#203947';x.lineWidth=1;for(let i=0;i<=4;i++){let y=12+(h-28)*i/4;x.beginPath();x.moveTo(36,y);"
    "x.lineTo(w-12,y);x.stroke();x.fillStyle='#8ba7b2';x.font='11px system-ui';x.fillText((hi-(hi-lo)*i/4).toFixed(0),3,y+4)}}"
    "function chargeChart(){const c=el('chargePlot'),{x,w,h}=size(c),left=36,right=w-12,bottom=h-16,top=12,measuredEnd=left+(right-left)*.64;"
    "grid(x,w,h,0,100);if(data.percent.length>1){x.strokeStyle='#73f0a7';x.lineWidth=3;x.beginPath();data.percent.forEach((v,i)=>{"
    "let t0=data.time[0],span=Math.max(1,data.time[data.time.length-1]-t0),px=left+(data.time[i]-t0)*(measuredEnd-left)/span,py=bottom-v*(bottom-top)/100;i?x.lineTo(px,py):x.moveTo(px,py)});x.stroke()}"
    "if(latest&&latest.estimate.valid&&data.percent.length){const v=data.percent[data.percent.length-1],y=bottom-v*(bottom-top)/100;x.save();"
    "x.strokeStyle='#ffd167';x.lineWidth=3;x.setLineDash([8,7]);x.beginPath();x.moveTo(measuredEnd,y);x.lineTo(right,bottom);x.stroke();x.restore();"
    "x.fillStyle='#ffd167';x.font='12px system-ui';x.textAlign='right';x.fillText(latest.estimate.remaining_hours.toFixed(1)+'h forecast',right,bottom-8);x.textAlign='left'}}"
    "function voltageChart(){const c=el('voltagePlot'),{x,w,h}=size(c);if(data.voltage.length<2)return;let lo=Math.min(...data.voltage)-.03,"
    "hi=Math.max(...data.voltage)+.03;grid(x,w,h,lo,hi);x.strokeStyle='#62d7ff';x.lineWidth=2;x.beginPath();data.voltage.forEach((v,i)=>{"
    "let t0=data.time[0],span=Math.max(1,data.time[data.time.length-1]-t0),px=36+(data.time[i]-t0)*(w-48)/span,py=h-16-(v-lo)*(h-28)/(hi-lo);i?x.lineTo(px,py):x.moveTo(px,py)});x.stroke()}"
    "function draw(){chargeChart();voltageChart()}function fmtHours(h){if(h<1)return Math.round(h*60)+'m';return h.toFixed(1)+'h'}"
    "async function update(){try{let r=await fetch('/api/battery',{cache:'no-store'});if(!r.ok)throw Error('HTTP '+r.status);latest=await r.json();"
    "statusEl.textContent=latest.mode+' · '+(latest.wifi_active?'Wi-Fi on':'Wi-Fi off');statusEl.className='status';el('percent').textContent=latest.battery.percent;"
    "el('voltage').textContent=(latest.battery.voltage_mv/1000).toFixed(3);el('source').textContent=latest.battery.charging?'charging':latest.source;"
    "el('remaining').textContent=latest.estimate.valid?fmtHours(latest.estimate.remaining_hours):'collecting';el('rate').textContent=latest.estimate.valid?"
    "latest.estimate.percent_per_hour.toFixed(2)+'%/h':'—';el('forecastNote').textContent=latest.estimate.valid?'Forecast based on '+latest.estimate.drop_percent+"
    "'% drop over '+fmtHours(latest.estimate.discharge_elapsed_ms/3600000):latest.source==='usb'?'Forecast begins after unplugging USB and observing at least 1% drop.':"
    "'Collecting at least 15 minutes and 1% discharge before forecasting.';if(!historyLoaded){(latest.history||[]).forEach(s=>{data.percent.push(s.percent);data.voltage.push(s.voltage_mv/1000);data.time.push(s.epoch_s)});historyLoaded=true}"
    "let now=latest.epoch_s||Math.floor(Date.now()/1000),last=data.time.length-1;if(last<0||now-data.time[last]>=60||data.percent[last]!==latest.battery.percent||data.voltage[last]!==latest.battery.voltage_mv/1000){data.percent.push(latest.battery.percent);data.voltage.push(latest.battery.voltage_mv/1000);data.time.push(now)}"
    "while(data.percent.length>N){data.percent.shift();data.voltage.shift();data.time.shift()}let u=latest.usage,total=u.awake_ms+u.breathing_ms+u.dimmed_ms+u.asleep_ms||1;"
    "el('awakeBar').style.width=100*u.awake_ms/total+'%';el('breathingBar').style.width=100*u.breathing_ms/total+'%';el('dimmedBar').style.width=100*u.dimmed_ms/total+'%';"
    "el('asleepBar').style.width=100*u.asleep_ms/total+'%';draw()}catch(e){statusEl.textContent='Device unavailable · '+e.message;statusEl.className='status bad'}}"
    "addEventListener('resize',draw);update();setInterval(update,5000);</script></body></html>";

static esp_err_t battery_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, k_battery_page, HTTPD_RESP_USE_STRLEN);
}

#if defined(ASTROLABE_FORCE_VARIANT_LUNASAY)
static const char k_pwa_manifest[] =
    "{\"name\":\"LunaSay\",\"short_name\":\"LunaSay\","
    "\"description\":\"LunaSay family astrology and device settings\","
    "\"start_url\":\"/settings\",\"scope\":\"/\",\"display\":\"standalone\","
    "\"background_color\":\"#0e0b17\",\"theme_color\":\"#4e3568\","
    "\"icons\":[{\"src\":\"/favicon.svg\",\"sizes\":\"any\","
    "\"type\":\"image/svg+xml\",\"purpose\":\"any maskable\"}]}";
#else
static const char k_pwa_manifest[] =
    "{\"name\":\"Astrolabe Arc Reactor\",\"short_name\":\"Astrolabe\","
    "\"description\":\"Arc Reactor breathing and battery monitor\","
    "\"start_url\":\"/\",\"scope\":\"/\",\"display\":\"standalone\","
    "\"background_color\":\"#06080c\",\"theme_color\":\"#061018\","
    "\"icons\":[{\"src\":\"/favicon.svg\",\"sizes\":\"any\","
    "\"type\":\"image/svg+xml\",\"purpose\":\"any maskable\"}]}";
#endif

static const char k_arc_reactor_icon[] =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 512 512\">"
    "<defs><radialGradient id=\"g\"><stop offset=\"0\" stop-color=\"#fff\"/>"
    "<stop offset=\".28\" stop-color=\"#b8fcff\"/><stop offset=\".68\" stop-color=\"#168da5\"/>"
    "<stop offset=\"1\" stop-color=\"#061018\"/></radialGradient>"
    "<filter id=\"b\"><feGaussianBlur stdDeviation=\"9\"/></filter></defs>"
    "<rect width=\"512\" height=\"512\" rx=\"112\" fill=\"#06080c\"/>"
    "<circle cx=\"256\" cy=\"256\" r=\"171\" fill=\"none\" stroke=\"#165868\" stroke-width=\"18\"/>"
    "<circle cx=\"256\" cy=\"256\" r=\"137\" fill=\"none\" stroke=\"#aaf8ff\" stroke-width=\"16\" opacity=\".35\" filter=\"url(#b)\"/>"
    "<g fill=\"#aaf8ff\"><path d=\"M245 67h22l14 87h-50z\"/><path d=\"M245 445h22l14-87h-50z\"/>"
    "<path d=\"M67 245v22l87 14v-50z\"/><path d=\"M445 245v22l-87 14v-50z\"/>"
    "<path d=\"M122 106l16-16 72 52-36 36z\"/><path d=\"M390 406l-16 16-72-52 36-36z\"/>"
    "<path d=\"M106 390l-16-16 52-72 36 36z\"/><path d=\"M406 122l16 16-52 72-36-36z\"/></g>"
    "<circle cx=\"256\" cy=\"256\" r=\"111\" fill=\"url(#g)\" stroke=\"#b8fcff\" stroke-width=\"12\"/>"
    "<circle cx=\"256\" cy=\"256\" r=\"53\" fill=\"#efffff\" stroke=\"#177f96\" stroke-width=\"10\"/>"
    "</svg>";

static esp_err_t manifest_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/manifest+json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    return httpd_resp_send(req, k_pwa_manifest, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t favicon_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "image/svg+xml");
    httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=86400");
    return httpd_resp_send(req, k_arc_reactor_icon, HTTPD_RESP_USE_STRLEN);
}

static const char k_breathing_page[] =
    "<!doctype html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" "
    "content=\"width=device-width,initial-scale=1\"><meta name=\"theme-color\" content=\"#061018\">"
    "<link rel=\"manifest\" href=\"/manifest.webmanifest\"><link rel=\"icon\" href=\"/favicon.svg\" type=\"image/svg+xml\">"
    "<link rel=\"apple-touch-icon\" href=\"/favicon.svg\"><title>Astrolabe Breathing</title><style>"
    ":root{color-scheme:dark;--bg:#030810;--panel:#091622;--line:#173747;--cyan:#58dcff;"
    "--gold:#ffd46a;--green:#70f4ac;--text:#dbf8ff;--muted:#87a9b5}*{box-sizing:border-box}"
    "html,body{min-height:100%;height:auto;overflow-x:hidden;overflow-y:auto;overscroll-behavior-y:auto}"
    "body{margin:0;background:radial-gradient(circle at 50% -20%,#12334a 0,var(--bg) 48%);"
    "color:var(--text);font:15px system-ui,sans-serif;-webkit-overflow-scrolling:touch}main{width:min(1080px,94vw);"
    "margin:auto;padding:24px 0 max(72px,env(safe-area-inset-bottom))}header{display:flex;align-items:end;justify-content:space-between;"
    "gap:16px;margin-bottom:18px}h1{font-size:clamp(1.35rem,4vw,2.2rem);font-weight:500;"
    "letter-spacing:.13em;margin:0}.status{color:var(--green);text-align:right}.bad{color:#ff7b86}"
    ".cards{display:grid;grid-template-columns:repeat(6,1fr);gap:10px;margin-bottom:12px}.card,.plot{"
    "background:color-mix(in srgb,var(--panel) 94%,transparent);border:1px solid var(--line);"
    "border-radius:14px;box-shadow:0 12px 40px #0005}.card{padding:14px}.label{color:var(--muted);"
    "font-size:.78rem;text-transform:uppercase;letter-spacing:.09em}.value{font-size:1.55rem;"
    "margin-top:4px}.phase{color:var(--gold)}.plots{display:grid;grid-template-columns:2fr 1fr;gap:12px}"
    ".plot{padding:12px 14px}.plot h2{font-size:.82rem;color:var(--muted);font-weight:500;"
    "letter-spacing:.08em;text-transform:uppercase;margin:0 0 8px}.plot.wave{grid-row:span 3}.plot.wave canvas{"
    "height:430px}.plot.timeline{grid-column:1/-1}.plot.timeline canvas{height:105px}canvas{display:block;width:100%;height:112px}.foot{color:var(--muted);font-size:.78rem;"
    "margin-top:10px}@media(max-width:760px){.cards{grid-template-columns:repeat(2,1fr)}.plots{"
    "grid-template-columns:1fr}.plot.wave{grid-row:auto}.plot.wave canvas{height:230px}}"
    "</style></head><body><main><header><div><h1>ARC REACTOR · BREATHING</h1>"
    "<div class=\"foot\">Rolling 90-second IMU telemetry</div></div><div id=\"status\" class=\"status\">Connecting…</div>"
    "</header><section class=\"cards\"><div class=\"card\"><div class=\"label\">Detected phase</div>"
    "<div id=\"detectedPhase\" class=\"value phase\">—</div><small id=\"phaseConfidence\"></small></div>"
    "<div class=\"card\"><div class=\"label\">Guide phase</div><div id=\"guidePhase\" class=\"value\">—</div></div>"
    "<div class=\"card\"><div class=\"label\">Rate</div>"
    "<div class=\"value\"><span id=\"rate\">—</span> <small>bpm</small></div></div><div class=\"card\">"
    "<div class=\"label\">Confidence</div><div id=\"confidence\" class=\"value\">—</div></div>"
    "<div class=\"card\"><div class=\"label\">Breaths</div><div id=\"breaths\" class=\"value\">—</div></div>"
    "<div class=\"card\"><div class=\"label\">Axis</div><div id=\"axis\" class=\"value\">—</div></div>"
    "</section><section class=\"plots\"><div class=\"plot timeline\"><h2>Detected inhale · hold · exhale durations</h2>"
    "<canvas id=\"phasePlot\"></canvas></div><div class=\"plot wave\"><h2>Breath waveform</h2><canvas id=\"wave\"></canvas>"
    "</div><div class=\"plot\"><h2>Rate · bpm</h2><canvas id=\"ratePlot\"></canvas></div>"
    "<div class=\"plot\"><h2>Confidence</h2><canvas id=\"confidencePlot\"></canvas></div>"
    "<div class=\"plot\"><h2>Amplitude · degrees</h2><canvas id=\"amplitudePlot\"></canvas></div>"
    "</section></main><script>"
    "const N=180,series={wave:[],rate:[],confidence:[],amplitude:[],phase:[],time:[],phaseMs:[]};"
    "const el=id=>document.getElementById(id);const statusEl=el('status');"
    "function push(a,v){a.push(Number.isFinite(v)?v:0);if(a.length>N)a.shift()}"
    "function chart(id,a,color,fixedMin,fixedMax,bands){const c=el(id),dpr=devicePixelRatio||1,w=c.clientWidth,h=c.clientHeight;"
    "if(c.width!==Math.round(w*dpr)||c.height!==Math.round(h*dpr)){c.width=Math.round(w*dpr);c.height=Math.round(h*dpr)}"
    "const x=c.getContext('2d');x.setTransform(dpr,0,0,dpr,0,0);x.clearRect(0,0,w,h);"
    "if(bands)bands.forEach((v,i)=>{x.fillStyle=v==='inhale'?'#58dcff20':v==='exhale'?'#c895ff20':v==='hold'?'#ffd46a20':'#0000';x.fillRect(i*w/(N-1),0,w/(N-1)+1,h)});"
    "let lo=fixedMin,hi=fixedMax;if(lo==null){lo=Math.min(...a,0);hi=Math.max(...a,1);const p=Math.max((hi-lo)*.12,.05);lo-=p;hi+=p}"
    "x.strokeStyle='#173747';x.lineWidth=1;for(let i=1;i<4;i++){let y=h*i/4;x.beginPath();x.moveTo(0,y);x.lineTo(w,y);x.stroke()}"
    "if(a.length<2)return;x.strokeStyle=color;x.lineWidth=2;x.beginPath();a.forEach((v,i)=>{let px=i*w/(N-1),"
    "py=h-(v-lo)*h/Math.max(hi-lo,.001);if(i)x.lineTo(px,py);else x.moveTo(px,py)});x.stroke();"
    "x.fillStyle='#87a9b5';x.font='11px system-ui';x.fillText(hi.toFixed(1),4,12);x.fillText(lo.toFixed(1),4,h-4)}"
    "function phaseTimeline(){const c=el('phasePlot'),dpr=devicePixelRatio||1,w=c.clientWidth,h=c.clientHeight;"
    "if(c.width!==Math.round(w*dpr)||c.height!==Math.round(h*dpr)){c.width=Math.round(w*dpr);c.height=Math.round(h*dpr)}"
    "const x=c.getContext('2d');x.setTransform(dpr,0,0,dpr,0,0);x.clearRect(0,0,w,h);if(!series.phase.length)return;"
    "const color=p=>p==='inhale'?'#58dcff':p==='hold'?'#ffd46a':p==='exhale'?'#c895ff':'#425563';"
    "let start=0;for(let i=1;i<=series.phase.length;i++){if(i<series.phase.length&&series.phase[i]===series.phase[start])continue;"
    "const x0=start*w/N,x1=i*w/N,p=series.phase[start],current=i===series.phase.length;let seconds=current?series.phaseMs[i-1]/1000:"
    "Math.max(.5,(series.time[i-1]-series.time[start])/1000+.5);x.fillStyle=color(p)+'bb';x.fillRect(x0,14,Math.max(1,x1-x0),h-28);"
    "if(x1-x0>48){x.fillStyle='#061018';x.font='600 12px system-ui';x.textAlign='center';x.fillText(p+' '+seconds.toFixed(1)+'s',(x0+x1)/2,h/2+4)}start=i}"
    "x.textAlign='left';x.fillStyle='#87a9b5';x.font='11px system-ui';x.fillText('90s ago',3,h-2);x.textAlign='right';x.fillText('now',w-3,h-2)}"
    "function draw(){phaseTimeline();chart('wave',series.wave,'#58dcff',0,1,series.phase);chart('ratePlot',series.rate,'#ffd46a',0,30);"
    "chart('confidencePlot',series.confidence,'#70f4ac',0,1);chart('amplitudePlot',series.amplitude,'#c895ff',null,null)}"
    "async function update(){try{const r=await fetch('/api/breath',{cache:'no-store'});if(!r.ok)throw Error('HTTP '+r.status);"
    "const d=await r.json();statusEl.textContent=d.state+' · '+d.age_ms+' ms old';statusEl.className='status';"
    "el('detectedPhase').textContent=d.detected.phase;el('phaseConfidence').textContent=Math.round(d.detected.confidence*100)+'% · '+(d.detected.phase_ms/1000).toFixed(1)+'s';"
    "el('guidePhase').textContent=d.guide.phase;el('rate').textContent=d.rate_bpm.toFixed(1);"
    "el('confidence').textContent=Math.round(d.confidence*100)+'%';el('breaths').textContent=d.breaths;"
    "el('axis').textContent=d.axis;push(series.wave,d.waveform);push(series.rate,d.rate_bpm);"
    "push(series.confidence,d.confidence);push(series.amplitude,d.amplitude_deg);series.phase.push(d.detected.phase);"
    "series.time.push(d.uptime_ms);series.phaseMs.push(d.detected.phase_ms);if(series.phase.length>N){series.phase.shift();series.time.shift();series.phaseMs.shift()}draw()}catch(e){"
    "statusEl.textContent='Device unavailable · '+e.message;statusEl.className='status bad'}}"
    "addEventListener('resize',draw);update();setInterval(update,500);"
    "</script></body></html>";

static esp_err_t breathing_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, k_breathing_page, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t root_get(httpd_req_t *req)
{
    char body[1024];
    snprintf(body,
             sizeof(body),
             "<!doctype html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" "
             "content=\"width=device-width,initial-scale=1\"><meta name=\"theme-color\" content=\"#061018\">"
             "<link rel=\"manifest\" href=\"/manifest.webmanifest\"><link rel=\"icon\" href=\"/favicon.svg\" type=\"image/svg+xml\">"
             "<link rel=\"apple-touch-icon\" href=\"/favicon.svg\"><title>Astrolabe</title></head>"
             "<body style=\"margin:0;background:#111;color:#ccc;font-family:system-ui,sans-serif;\">"
             "<p style=\"padding:10px\">Faculty175 ESP-IDF "
             "| <a style=\"color:#8cf\" href=\"/wifi\">wifi settings</a> "
             "| <a style=\"color:#8cf\" href=\"/breathing\">breathing monitor</a> "
             "| <a style=\"color:#8cf\" href=\"/battery\">battery monitor</a> "
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
    char *body = malloc(2800);
    char *known_html = calloc(1, 620);
    faculty175_wifi_known_t *known = calloc(FACULTY175_WIFI_KNOWN_MAX, sizeof(*known));
    if (body == NULL || known_html == NULL || known == NULL) {
        free(body);
        free(known_html);
        free(known);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "out of memory");
    }
    size_t known_off = 0;
    const bool ap = faculty175_wifi_settings_ap_active();
    const bool router = faculty175_wifi_settings_travel_router_enabled();
    const char *ssid = faculty175_wifi_settings_ssid();
    const char *upstream = faculty175_wifi_settings_upstream_ssid();
    const char *url = faculty175_wifi_settings_url();
    const size_t known_count = faculty175_wifi_settings_load_known(known, FACULTY175_WIFI_KNOWN_MAX);
    for (size_t i = 0; i < known_count && known_off + 80 < 620; ++i) {
        const int wrote = snprintf(known_html + known_off,
                                   620 - known_off,
                                   "<li>%s%s <span class=\"muted\">%s</span></li>",
                                   i == 0 ? "<strong>" : "",
                                   known[i].ssid,
                                   i == 0 ? "primary</strong>" : "");
        if (wrote < 0) {
            break;
        }
        known_off += (size_t)wrote < 620 - known_off
            ? (size_t)wrote
            : 620 - known_off - 1;
    }
    snprintf(body,
             2800,
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
    const esp_err_t err = httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
    free(known);
    free(known_html);
    free(body);
    return err;
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
    for (size_t i = 0; i < FACULTY175_FACE_COUNT; ++i) {
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

static void family_chart_add_json(cJSON *parent, const char *key, const faculty175_birth_chart_t *chart)
{
    cJSON *obj = key != NULL ? cJSON_AddObjectToObject(parent, key) : cJSON_CreateObject();
    if (obj == NULL) {
        return;
    }
    if (key == NULL) {
        cJSON_AddItemToArray(parent, obj);
    }
    add_json_string(obj, "name", chart->name);
    add_json_string(obj, "role", faculty175_charts_role_label(chart->role));
    char value[24];
    snprintf(value, sizeof(value), "%04u-%02u-%02u", chart->year, chart->month, chart->day);
    add_json_string(obj, "date", value);
    snprintf(value, sizeof(value), "%02u:%02u", chart->hour, chart->minute);
    add_json_string(obj, "time", value);
    cJSON_AddNumberToObject(obj, "lat", chart->lat_deg);
    cJSON_AddNumberToObject(obj, "lon", chart->lon_deg);
    cJSON_AddNumberToObject(obj, "tzOffsetMinutes", chart->tz_offset_sec / 60);
    add_json_string(obj, "place", chart->place);
}

static bool family_chart_from_json(const cJSON *obj, faculty175_birth_chart_t *chart, bool primary)
{
    if (!cJSON_IsObject(obj) || chart == NULL) {
        return false;
    }
    memset(chart, 0, sizeof(*chart));
    const cJSON *name = cJSON_GetObjectItemCaseSensitive(obj, "name");
    const cJSON *role = cJSON_GetObjectItemCaseSensitive(obj, "role");
    const cJSON *date = cJSON_GetObjectItemCaseSensitive(obj, "date");
    const cJSON *time = cJSON_GetObjectItemCaseSensitive(obj, "time");
    const cJSON *lat = cJSON_GetObjectItemCaseSensitive(obj, "lat");
    const cJSON *lon = cJSON_GetObjectItemCaseSensitive(obj, "lon");
    const cJSON *tz = cJSON_GetObjectItemCaseSensitive(obj, "tzOffsetMinutes");
    const cJSON *place = cJSON_GetObjectItemCaseSensitive(obj, "place");
    if (!cJSON_IsString(name) || name->valuestring == NULL || name->valuestring[0] == '\0' ||
        !cJSON_IsString(date) || date->valuestring == NULL ||
        !cJSON_IsString(time) || time->valuestring == NULL ||
        !cJSON_IsNumber(lat) || !cJSON_IsNumber(lon) || !cJSON_IsNumber(tz)) {
        return false;
    }
    int year = 0, month = 0, day = 0, hour = 0, minute = 0;
    if (sscanf(date->valuestring, "%d-%d-%d", &year, &month, &day) != 3 ||
        sscanf(time->valuestring, "%d:%d", &hour, &minute) != 2) {
        return false;
    }
    strlcpy(chart->name, name->valuestring, sizeof(chart->name));
    chart->role = primary ? FACULTY175_CHART_ROLE_SELF : FACULTY175_CHART_ROLE_PARTNER;
    if (!primary && cJSON_IsString(role) && role->valuestring != NULL && strcasecmp(role->valuestring, "child") == 0) {
        chart->role = FACULTY175_CHART_ROLE_CHILD;
    }
    chart->year = (uint16_t)year;
    chart->month = (uint8_t)month;
    chart->day = (uint8_t)day;
    chart->hour = (uint8_t)hour;
    chart->minute = (uint8_t)minute;
    chart->lat_deg = (float)lat->valuedouble;
    chart->lon_deg = (float)lon->valuedouble;
    chart->tz_offset_sec = (int32_t)(tz->valuedouble * 60.0);
    if (cJSON_IsString(place) && place->valuestring != NULL) {
        strlcpy(chart->place, place->valuestring, sizeof(chart->place));
    }
    chart->valid = true;
    time_t ignored = 0;
    return faculty175_charts_birth_to_utc(chart, &ignored);
}

static esp_err_t api_family_send(httpd_req_t *req, esp_err_t apply_err)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddBoolToObject(root, "ok", apply_err == ESP_OK);
    add_json_string(root, "err", esp_err_to_name(apply_err));
    cJSON_AddNumberToObject(root, "schema", 1);
    faculty175_birth_chart_t chart = {};
    if (faculty175_charts_primary(&chart)) {
        family_chart_add_json(root, "primary", &chart);
    }
    cJSON *profiles = cJSON_AddArrayToObject(root, "profiles");
    for (int slot = 0; profiles != NULL && slot < FACULTY175_CHART_PROFILE_SLOTS; ++slot) {
        if (faculty175_charts_profile_get(slot, &chart)) {
            family_chart_add_json(profiles, NULL, &chart);
        }
    }
    cJSON_AddNumberToObject(root, "active", faculty175_charts_active_slot());
    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (body == NULL) {
        return ESP_ERR_NO_MEM;
    }
    set_api_headers(req);
    const esp_err_t err = httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
    free(body);
    return err;
}

static esp_err_t api_family_get(httpd_req_t *req)
{
    return api_family_send(req, ESP_OK);
}

static esp_err_t api_family_post(httpd_req_t *req)
{
    char *body = malloc((size_t)req->content_len + 1);
    if (body == NULL || req->content_len <= 0 || req->content_len > 8192) {
        free(body);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "family body must be 1..8192 bytes");
        return ESP_FAIL;
    }
    size_t off = 0;
    while (off < (size_t)req->content_len) {
        const int got = httpd_req_recv(req, body + off, (size_t)req->content_len - off);
        if (got <= 0) {
            free(body);
            return ESP_FAIL;
        }
        off += (size_t)got;
    }
    body[off] = '\0';
    cJSON *root = cJSON_Parse(body);
    free(body);
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid family json");
        return ESP_FAIL;
    }
    faculty175_birth_chart_t primary = {};
    faculty175_birth_chart_t profiles[FACULTY175_CHART_PROFILE_SLOTS] = {};
    const cJSON *primary_obj = cJSON_GetObjectItemCaseSensitive(root, "primary");
    const cJSON *items = cJSON_GetObjectItemCaseSensitive(root, "profiles");
    const cJSON *active_obj = cJSON_GetObjectItemCaseSensitive(root, "active");
    bool valid = family_chart_from_json(primary_obj, &primary, true) && cJSON_IsArray(items);
    int count = 0;
    const cJSON *item = NULL;
    if (valid) {
        cJSON_ArrayForEach(item, items) {
            if (count >= FACULTY175_CHART_PROFILE_SLOTS || !family_chart_from_json(item, &profiles[count], false)) {
                valid = false;
                break;
            }
            ++count;
        }
    }
    int active = cJSON_IsNumber(active_obj) ? active_obj->valueint : (count > 0 ? 0 : -1);
    if (active < -1 || active >= count) {
        valid = false;
    }
    esp_err_t err = valid ? faculty175_charts_save_primary(&primary) : ESP_ERR_INVALID_ARG;
    for (int slot = 0; err == ESP_OK && slot < FACULTY175_CHART_PROFILE_SLOTS; ++slot) {
        err = slot < count ? faculty175_charts_profile_save(slot, &profiles[slot])
                           : faculty175_charts_profile_clear(slot);
    }
    if (err == ESP_OK && active >= 0 && !faculty175_charts_set_active_slot(active)) {
        err = ESP_FAIL;
    }
    cJSON_Delete(root);
    return api_family_send(req, err);
}

static esp_err_t family_page_get(httpd_req_t *req)
{
    static const char page[] =
        "<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'><meta name=theme-color content='#4e3568'><link rel=manifest href='/manifest.webmanifest'>"
        "<title>LunaSay Family</title><style>body{font:16px system-ui;max-width:760px;margin:auto;padding:24px;background:#10101a;color:#eee}"
        "h1{color:#f0d7ff}.person{border:1px solid #554866;border-radius:14px;padding:14px;margin:12px 0;display:grid;grid-template-columns:1fr 1fr;gap:10px}"
        "label{font-size:12px;color:#cbbbd5}input,select,button{box-sizing:border-box;width:100%;padding:10px;border-radius:8px;border:1px solid #675878;background:#1c1825;color:#fff}"
        ".wide{grid-column:1/-1}button{background:#7650a0;font-weight:700;cursor:pointer}.remove{background:#472636}.lookup{background:#36556b}.note{font-size:12px;color:#ad9fba}#status{min-height:24px}</style>"
        "<p><a href='/settings' style='color:#dcc2ef'>Settings</a></p><h1>LunaSay Family</h1><p>Birth charts stay in this device's NVS and drive the Synastry face.</p><div id=people></div>"
        "<button onclick='add()'>Add family member</button><p><button onclick='save()'>Save family to LunaSay</button></p><p id=status></p>"
        "<script>let data={primary:null,profiles:[],active:0};const E=document.getElementById('people'),S=document.getElementById('status');"
        "function card(p,i,self){let d=document.createElement('div');d.className='person';d.innerHTML=`<label>Name<input data-k=name value='${p.name||''}'></label>"
        "<label>Role<select data-k=role ${self?'disabled':''}><option value=self>Self</option><option value=partner>Partner</option><option value=child>Child</option></select></label>"
        "<label>Birth date<input data-k=date type=date value='${p.date||''}'></label><label>Birth time<input data-k=time type=time value='${p.time||'12:00'}'></label>"
        "<label>Latitude<input data-k=lat type=number step=.0001 value='${p.lat??0}'></label><label>Longitude<input data-k=lon type=number step=.0001 value='${p.lon??0}'></label>"
        "<label>UTC offset minutes<input data-k=tzOffsetMinutes type=number value='${p.tzOffsetMinutes??0}'></label><label>Birth place<input data-k=place value='${p.place||''}'></label>"
        "<button type=button class='wide lookup'>Look up birthplace coordinates</button><span class='wide note'>One lookup per tap. Results © OpenStreetMap contributors.</span>"
        "${self?'':`<label class=wide><input data-active type=radio name=active ${i===data.active?'checked':''}> Featured on Synastry</label><button class='wide remove' onclick='data.profiles.splice(${i},1);render()'>Remove</button>`}`;"
        "d.querySelector('[data-k=role]').value=(self?'self':(p.role||'partner'));d.querySelectorAll('[data-k]').forEach(x=>x.oninput=()=>{let v=x.type==='number'?+x.value:x.value;p[x.dataset.k]=v});"
        "let b=d.querySelector('.lookup');b.onclick=async()=>{let q=d.querySelector('[data-k=place]').value.trim();if(!q){S.textContent='Enter a birthplace first.';return}b.disabled=true;S.textContent='Looking up '+q+'…';try{let u='https://nominatim.openstreetmap.org/search?format=jsonv2&limit=1&addressdetails=1&q='+encodeURIComponent(q);let a=await(await fetch(u,{headers:{Accept:'application/json'}})).json();if(!a.length)throw Error('No matching place');p.lat=+a[0].lat;p.lon=+a[0].lon;p.place=a[0].display_name;S.textContent='Coordinates found. Review them, then save.';render()}catch(e){S.textContent='Lookup failed: '+e.message+'. You can enter coordinates manually.'}finally{b.disabled=false}};if(!self)d.querySelector('[data-active]').onchange=()=>data.active=i;return d}"
        "function render(){E.innerHTML='<h2>You</h2>';E.append(card(data.primary||{},0,true));E.insertAdjacentHTML('beforeend','<h2>Family</h2>');data.profiles.forEach((p,i)=>E.append(card(p,i,false)))}"
        "function add(){data.profiles.push({role:'partner',time:'12:00',lat:0,lon:0,tzOffsetMinutes:0});data.active=data.profiles.length-1;render()}"
        "async function load(){data=await(await fetch('/api/family')).json();render()}async function save(){let r=await fetch('/api/family',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data)});let j=await r.json();S.textContent=j.ok?'Saved. Synastry is ready.':'Could not save: '+j.err;if(j.ok){data=j;render()}}load()</script>";
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, page, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t settings_page_get(httpd_req_t *req)
{
    static const char page[] =
        "<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'><meta name=theme-color content='#4e3568'>"
        "<link rel=manifest href='/manifest.webmanifest'><title>LunaSay Settings</title><style>body{font:16px system-ui;max-width:760px;margin:auto;padding:24px;background:#0e0b17;color:#f5effa}"
        "h1{color:#f0d7ff}.grid{display:grid;grid-template-columns:repeat(2,1fr);gap:12px}.card{display:block;text-decoration:none;color:#fff;border:1px solid #5c4b6d;border-radius:16px;padding:18px;background:#1a1423}"
        ".card b{display:block;font-size:20px;margin-bottom:6px}.card span,#status{color:#bdaec8}@media(max-width:520px){.grid{grid-template-columns:1fr}}</style>"
        "<h1>LunaSay Settings</h1><p id=status>Connecting to LunaSay...</p><div class=grid>"
        "<a class=card href='/battery'><b>Battery</b><span>Charge, power source, and history</span></a>"
        "<a class=card href='/wifi'><b>Wi-Fi</b><span>Network and travel-router setup</span></a>"
        "<a class=card href='/family'><b>Family</b><span>Birth charts for Synastry</span></a>"
        "<a class=card href='/'><b>Faces</b><span>Screen preview and face control</span></a>"
        "<a class=card href='/api/settings'><b>Bluetooth</b><span id=ble>Checking status...</span></a>"
        "<a class=card href='/api/faces'><b>Diagnostics</b><span>Firmware face inventory</span></a></div>"
        "<script>fetch('/api/settings').then(r=>r.json()).then(s=>{status.textContent='LunaSay - '+(s.wifi?.status||'local device');ble.textContent=s.ble?.enabled?(s.ble.advertising?'Enabled and advertising':'Enabled'):'Disabled'}).catch(()=>status.textContent='LunaSay is offline')</script>";
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, page, HTTPD_RESP_USE_STRLEN);
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
#if defined(ASTROLABE_FORCE_VARIANT_LUNASAY)
        (void)known_array;
#else
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
#endif
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
    const esp_err_t err = faculty175_faces_set(face->id);
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
        if (ms_text[0] == '\0' && body[0] == '{') {
            cJSON *json = cJSON_Parse(body);
            const cJSON *ms = json != NULL ? cJSON_GetObjectItemCaseSensitive(json, "ms") : NULL;
            if (cJSON_IsNumber(ms)) {
                snprintf(ms_text, sizeof(ms_text), "%lu", (unsigned long)ms->valuedouble);
            }
            cJSON_Delete(json);
        }
        if (ms_text[0] != '\0') {
            const unsigned long parsed = strtoul(ms_text, NULL, 10);
            if (parsed >= 1000 && parsed <= 30000) {
                capture_ms = (uint32_t)parsed;
            }
        }
#if defined(ASTROLABE_FORCE_VARIANT_LUNASAY)
        err = faculty175_request_qa_stt_deferred(capture_ms);
#else
        err = faculty175_request_qa_stt(capture_ms);
#endif
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

static esp_err_t api_voice_get(httpd_req_t *req)
{
    faculty175_qa_voice_status_t status = {};
    faculty175_qa_voice_status(&status);
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json alloc");
        return ESP_FAIL;
    }
    cJSON_AddNumberToObject(root, "schema", 1);
    cJSON_AddNumberToObject(root, "sequence", status.sequence);
    cJSON_AddBoolToObject(root, "busy", status.busy);
    cJSON_AddNumberToObject(root, "capture_ms", status.capture_ms);
    cJSON_AddNumberToObject(root, "started_ms", status.started_ms);
    cJSON_AddNumberToObject(root, "completed_ms", status.completed_ms);
    cJSON_AddNumberToObject(root,
                           "elapsed_ms",
                           status.completed_ms >= status.started_ms
                               ? status.completed_ms - status.started_ms
                               : 0u);
    add_json_string(root, "err", esp_err_to_name(status.err));
    add_json_string(root, "transcript", status.transcript);
    add_json_string(root, "reply", status.reply);
    char *reply = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (reply == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json print");
        return ESP_FAIL;
    }
    set_api_headers(req);
    const esp_err_t err = httpd_resp_send(req, reply, HTTPD_RESP_USE_STRLEN);
    free(reply);
    return err;
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
    faculty175_screen_http_wake_listener_stop();
    if (ip != NULL) {
        s_ip = *ip;
    }
    if (s_httpd != NULL) {
        ESP_LOGI(TAG, "ready http://" IPSTR "/ (GET /screen.bmp)", IP2STR(&s_ip));
        return ESP_OK;
    }

#if !defined(ASTROLABE_FORCE_VARIANT_LUNASAY)
    if (!s_mdns_started) {
        if (faculty175_wifi_settings_load_hostname(s_mdns_hostname, sizeof(s_mdns_hostname)) != ESP_OK) {
            uint8_t sta_mac[6] = {};
            if (esp_wifi_get_mac(WIFI_IF_STA, sta_mac) == ESP_OK) {
                snprintf(s_mdns_hostname,
                         sizeof(s_mdns_hostname),
                         "astrolabe-%02x%02x",
                         sta_mac[4],
                         sta_mac[5]);
            }
        }
        esp_err_t mdns_err = mdns_init();
        if (mdns_err == ESP_OK) {
            mdns_err = mdns_hostname_set(s_mdns_hostname);
        }
        if (mdns_err == ESP_OK) {
            mdns_err = mdns_instance_name_set("Astrolabe Faculty 1.75C");
        }
        if (mdns_err == ESP_OK) {
            mdns_err = mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
        }
        if (mdns_err != ESP_OK) {
            ESP_LOGW(TAG, "mDNS start failed: %s", esp_err_to_name(mdns_err));
            mdns_free();
        } else {
            s_mdns_started = true;
            ESP_LOGI(TAG, "mDNS ready http://%s.local/", s_mdns_hostname);
        }
    }
#endif

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.stack_size = FACULTY175_SCREEN_HTTP_STACK_SIZE;
    config.max_open_sockets = 4;
    config.max_uri_handlers = 40;
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
    const httpd_uri_t api_breath_uri = {
        .uri = "/api/breath",
        .method = HTTP_GET,
        .handler = api_breath_get,
        .user_ctx = NULL,
    };
    const httpd_uri_t breathing_uri = {
        .uri = "/breathing",
        .method = HTTP_GET,
        .handler = breathing_get,
        .user_ctx = NULL,
    };
    const httpd_uri_t api_battery_uri = {
        .uri = "/api/battery",
        .method = HTTP_GET,
        .handler = api_battery_get,
        .user_ctx = NULL,
    };
    const httpd_uri_t battery_uri = {
        .uri = "/battery",
        .method = HTTP_GET,
        .handler = battery_get,
        .user_ctx = NULL,
    };
    const httpd_uri_t manifest_uri = {
        .uri = "/manifest.webmanifest",
        .method = HTTP_GET,
        .handler = manifest_get,
        .user_ctx = NULL,
    };
    const httpd_uri_t favicon_svg_uri = {
        .uri = "/favicon.svg",
        .method = HTTP_GET,
        .handler = favicon_get,
        .user_ctx = NULL,
    };
    const httpd_uri_t favicon_ico_uri = {
        .uri = "/favicon.ico",
        .method = HTTP_GET,
        .handler = favicon_get,
        .user_ctx = NULL,
    };
    const httpd_uri_t api_breath_options_uri = {
        .uri = "/api/breath",
        .method = HTTP_OPTIONS,
        .handler = api_options,
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
    const httpd_uri_t api_voice_get_uri = {
        .uri = "/api/voice",
        .method = HTTP_GET,
        .handler = api_voice_get,
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
    const httpd_uri_t family_page_uri = {
        .uri = "/family",
        .method = HTTP_GET,
        .handler = family_page_get,
        .user_ctx = NULL,
    };
    const httpd_uri_t settings_page_uri = {
        .uri = "/settings",
        .method = HTTP_GET,
        .handler = settings_page_get,
        .user_ctx = NULL,
    };
    const httpd_uri_t api_family_get_uri = {
        .uri = "/api/family",
        .method = HTTP_GET,
        .handler = api_family_get,
        .user_ctx = NULL,
    };
    const httpd_uri_t api_family_post_uri = {
        .uri = "/api/family",
        .method = HTTP_POST,
        .handler = api_family_post,
        .user_ctx = NULL,
    };
    const httpd_uri_t api_family_options_uri = {
        .uri = "/api/family",
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
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_httpd, &api_breath_uri), TAG, "register GET /api/breath");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_httpd, &breathing_uri), TAG, "register GET /breathing");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_httpd, &api_battery_uri), TAG, "register GET /api/battery");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_httpd, &battery_uri), TAG, "register GET /battery");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_httpd, &manifest_uri), TAG, "register GET /manifest.webmanifest");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_httpd, &favicon_svg_uri), TAG, "register GET /favicon.svg");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_httpd, &favicon_ico_uri), TAG, "register GET /favicon.ico");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_httpd, &api_breath_options_uri), TAG, "register OPTIONS /api/breath");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_httpd, &api_face_get_uri), TAG, "register GET /api/face");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_httpd, &api_face_post_uri), TAG, "register POST /api/face");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_httpd, &api_voice_post_uri), TAG, "register POST /api/voice");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_httpd, &api_voice_get_uri), TAG, "register GET /api/voice");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_httpd, &api_settings_get_uri), TAG, "register GET /api/settings");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_httpd, &api_settings_post_uri), TAG, "register POST /api/settings");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_httpd, &api_settings_options_uri), TAG, "register OPTIONS /api/settings");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_httpd, &family_page_uri), TAG, "register GET /family");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_httpd, &settings_page_uri), TAG, "register GET /settings");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_httpd, &api_family_get_uri), TAG, "register GET /api/family");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_httpd, &api_family_post_uri), TAG, "register POST /api/family");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_httpd, &api_family_options_uri), TAG, "register OPTIONS /api/family");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_httpd, &lab_portal_get_uri), TAG, "register GET /lab/portal");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_httpd, &lab_portal_post_uri), TAG, "register POST /lab/portal");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_httpd, &lab_handshake_pcap_uri), TAG, "register GET /lab/handshake.pcap");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_httpd, &api_wifi_incidents_uri), TAG, "register GET /api/wifi/incidents");
    const esp_err_t km_err = faculty175_km_http_register(s_httpd);
    if (km_err != ESP_OK && km_err != ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW(TAG, "KM HTTP unavailable: %s", esp_err_to_name(km_err));
    }

    ESP_LOGI(TAG, "ready http://" IPSTR "/ (GET /screen.bmp, /wifi, /api/faces, /api/breath)", IP2STR(&s_ip));
    printf("Astrolabe WiFi settings: http://" IPSTR "/wifi\n", IP2STR(&s_ip));
    printf("Screen over WiFi: http://" IPSTR "/  (GET /screen.bmp)\n", IP2STR(&s_ip));
    printf("Face control over WiFi: http://" IPSTR "/api/faces  (GET /api/faces, POST /api/face slug=<slug>)\n",
           IP2STR(&s_ip));
    printf("Breathing metrics over WiFi: http://" IPSTR "/api/breath\n", IP2STR(&s_ip));
    printf("Arc Reactor monitor: http://%s.local/breathing\n", s_mdns_hostname);
    printf("Keyboard/mouse over WiFi: http://" IPSTR "/km\n", IP2STR(&s_ip));
    printf("LunaSay family setup: http://" IPSTR "/family\n", IP2STR(&s_ip));
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
    if (s_mdns_started) {
        mdns_free();
        s_mdns_started = false;
    }
#endif
}
