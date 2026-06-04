#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "astrolabe_audio_pipeline.h"
#include "astrolabe_faculty_atom_face.h"
#include "astrolabe_time.h"
#include "atom_board.h"
#include "atom_faculty.h"
#include "atom_log.h"
#include "atom_ota.h"
#include "atom_qa.h"
#include "atom_serial.h"
#include "atom_util.h"

#if __has_include("secrets.local.h")
#include "secrets.local.h"
#else
#include "secrets.example.h"
#endif

static const char *TAG = "facultyatom";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

static EventGroupHandle_t s_wifi_events;
static atom_ui_state_t s_ui = ATOM_UI_BOOT;
static char s_faculty_slug[64] = ASTROLABE_FACULTY_ATOM_DEFAULT_FACULTY_SLUG;
static char s_faculty_name[96] = ASTROLABE_FACULTY_ATOM_DEFAULT_FACULTY_NAME;
static char s_history[512];
static char s_detail[96];
static astrolabe_audio_pipeline_t *s_pipeline;
static uint32_t s_voice_turn;
static char s_voice_pipeline_url[256];
static char s_voice_stream_url[256];

static void facultyatom_log_ready(void);
static void make_supabase_ws_url(char *out, size_t out_len, const char *base_url, const char *path);

static void save_faculty_to_nvs(void);
static QueueHandle_t s_ui_queue;

typedef struct {
    atom_ui_state_t state;
    char detail[96];
} atom_ui_msg_t;

static void ui_set(atom_ui_state_t state, const char *detail)
{
    s_ui = state;
    if (detail != NULL) {
        atom_strlcpy(s_detail, detail, sizeof(s_detail));
    } else {
        s_detail[0] = '\0';
    }
    if (s_ui_queue == NULL) {
        return;
    }
    atom_ui_msg_t msg = {
        .state = state,
    };
    atom_strlcpy(msg.detail, s_detail, sizeof(msg.detail));
    (void)xQueueOverwrite(s_ui_queue, &msg);
}

static void ui_redraw(void)
{
    if (s_ui_queue == NULL) {
        return;
    }
    atom_ui_msg_t msg = {
        .state = s_ui,
    };
    atom_strlcpy(msg.detail, s_detail, sizeof(msg.detail));
    (void)xQueueOverwrite(s_ui_queue, &msg);
}

static void bust_ui_refresh(void)
{
    ui_redraw();
}

static void make_supabase_ws_url(char *out, size_t out_len, const char *base_url, const char *path)
{
    if (strncmp(base_url, "https://", 8) == 0) {
        snprintf(out, out_len, "wss://%s%s", base_url + 8, path);
    } else if (strncmp(base_url, "http://", 7) == 0) {
        snprintf(out, out_len, "ws://%s%s", base_url + 7, path);
    } else {
        snprintf(out, out_len, "%s%s", base_url, path);
    }
}


static void ui_task(void *arg)
{
    (void)arg;
    atom_ui_msg_t msg = {
        .state = ATOM_UI_BOOT,
    };
    while (true) {
        (void)xQueueReceive(s_ui_queue, &msg, portMAX_DELAY);
        s_ui = msg.state;
        atom_strlcpy(s_detail, msg.detail, sizeof(s_detail));
        const uint32_t anim_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        atom_display_draw_status(s_ui, s_faculty_name, s_detail, anim_ms, NULL, 0);
    }
}

static void append_history(const char *user, const char *reply)
{
    char chunk[256];
    snprintf(chunk, sizeof(chunk), "User: %s | Faculty: %s", user != NULL ? user : "", reply != NULL ? reply : "");
    if (s_history[0] != '\0') {
        atom_strlcpy(s_history + strlen(s_history), " || ", sizeof(s_history) - strlen(s_history));
    }
    atom_strlcpy(s_history + strlen(s_history), chunk, sizeof(s_history) - strlen(s_history));
    if (strlen(s_history) > sizeof(s_history) / 2) {
        memmove(s_history, s_history + strlen(s_history) / 2, strlen(s_history) / 2 + 1);
    }
}

static esp_err_t pipeline_read(int16_t *samples, size_t sample_count, size_t *out_read, uint32_t timeout_ms, void *user)
{
    (void)user;
    if (s_ui == ATOM_UI_THINK || s_ui == ATOM_UI_SPEAK) {
        if (out_read != NULL) {
            *out_read = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(40));
        return ESP_ERR_TIMEOUT;
    }
    if (!atom_board_audio_ready()) {
        if (out_read != NULL) {
            *out_read = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
        return ESP_ERR_INVALID_STATE;
    }
    return atom_audio_read(samples, sample_count, out_read, timeout_ms);
}

static esp_err_t pipeline_write(const int16_t *samples, size_t sample_count, uint32_t timeout_ms, void *user)
{
    (void)user;
    return atom_audio_write_pcm(samples, sample_count, timeout_ms);
}

static esp_err_t pipeline_set_rate(uint32_t sample_rate_hz, void *user)
{
    (void)user;
    return atom_audio_set_sample_rate(sample_rate_hz);
}

static void pipeline_mute(bool mute, void *user)
{
    (void)user;
    atom_audio_set_speaker_mute(mute);
}

static void pipeline_result(const char *transcript,
                            const char *reply,
                            const char *faculty_slug,
                            const char *faculty_name,
                            void *user)
{
    (void)user;
    bool faculty_changed = false;
    if (faculty_slug != NULL && faculty_slug[0] != '\0' && strcmp(s_faculty_slug, faculty_slug) != 0) {
        ATOM_LOG_STAGE(TAG, "faculty", "active %s -> %s", s_faculty_slug, faculty_slug);
        atom_strlcpy(s_faculty_slug, faculty_slug, sizeof(s_faculty_slug));
        faculty_changed = true;
    }
    if (faculty_name != NULL && faculty_name[0] != '\0') {
        atom_strlcpy(s_faculty_name, faculty_name, sizeof(s_faculty_name));
    }
    append_history(transcript, reply);
    if (faculty_changed || (faculty_slug != NULL && faculty_slug[0] != '\0') ||
        (faculty_name != NULL && faculty_name[0] != '\0')) {
        save_faculty_to_nvs();
    }
    if (faculty_changed || atom_faculty_bust_status() != ATOM_FACULTY_BUST_READY ||
        strcmp(atom_faculty_loaded_slug(), s_faculty_slug) != 0) {
        atom_faculty_request_bust(s_faculty_slug);
    }
}

static void pipeline_event(astrolabe_audio_pipeline_event_t event, const char *detail, void *user)
{
    (void)user;
    switch (event) {
        case ASTROLABE_AUDIO_PIPELINE_EVENT_LISTENING:
            ui_set(ATOM_UI_LISTEN, NULL);
            ATOM_LOG_STAGE(TAG, "listen", "ready");
            break;
        case ASTROLABE_AUDIO_PIPELINE_EVENT_CAPTURE_START:
            ui_set(ATOM_UI_CAPTURE, NULL);
            ATOM_LOG_STAGE(TAG, "capture", "speech detected — streaming to flash");
            break;
        case ASTROLABE_AUDIO_PIPELINE_EVENT_CAPTURE_QUEUED:
            s_voice_turn++;
            ATOM_LOG_STAGE(TAG, "capture", "utterance queued from flash (turn #%u)", (unsigned)s_voice_turn);
            break;
        case ASTROLABE_AUDIO_PIPELINE_EVENT_THINKING:
            ui_set(ATOM_UI_THINK, "Castalia...");
            ATOM_LOG_STAGE(TAG, "pipeline", "streaming flash capture to voice-pipeline (face=%s)",
                           ASTROLABE_FACULTY_ATOM_FACE_NAME);
            break;
        case ASTROLABE_AUDIO_PIPELINE_EVENT_TRANSCRIPT:
            if (detail != NULL && detail[0] != '\0') {
                ATOM_LOG_STAGE(TAG, "stt", "%.80s", detail);
            }
            break;
        case ASTROLABE_AUDIO_PIPELINE_EVENT_REPLY:
            if (detail != NULL && detail[0] != '\0') {
                ATOM_LOG_STAGE(TAG, "reply", "%.80s", detail);
            }
            break;
        case ASTROLABE_AUDIO_PIPELINE_EVENT_SPEAKING:
            ui_set(ATOM_UI_SPEAK, detail != NULL && detail[0] != '\0' ? detail : s_faculty_name);
            ATOM_LOG_STAGE(TAG, "speak", "%s", detail != NULL && detail[0] != '\0' ? detail : s_faculty_name);
            break;
        case ASTROLABE_AUDIO_PIPELINE_EVENT_TURN_DONE:
            ATOM_LOG_STAGE(TAG, "turn", "done #%u", (unsigned)s_voice_turn);
            break;
        case ASTROLABE_AUDIO_PIPELINE_EVENT_ERROR:
            ATOM_LOG_STAGE_E(TAG, "pipeline", "%s", detail != NULL ? detail : "error");
            ui_set(ATOM_UI_ERROR, "voice fail");
            break;
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    if (id == WIFI_EVENT_STA_START) {
        ATOM_LOG_STAGE(TAG, "wifi", "STA start — connecting to %s", MYNAH_WIFI_SSID);
        esp_wifi_connect();
    } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *disc = (const wifi_event_sta_disconnected_t *)data;
        const int reason = disc != NULL ? (int)disc->reason : -1;
        ATOM_LOG_STAGE_W(TAG, "wifi", "disconnected reason=%d — retrying", reason);
        xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT);
        esp_wifi_connect();
    } else if (id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)data;
        if (event != NULL) {
            ATOM_LOG_STAGE(TAG, "wifi", "connected ip=" IPSTR " gw=" IPSTR, IP2STR(&event->ip_info.ip),
                           IP2STR(&event->ip_info.gw));
        } else {
            ATOM_LOG_STAGE(TAG, "wifi", "connected (got IP)");
        }
        (void)astrolabe_time_start(NULL);
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
    }
}

static void facultyatom_log_ready(void)
{
    wifi_ap_record_t ap = {};
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        ATOM_LOG_STAGE(TAG, "ready", "wifi ok rssi=%d ch=%u", (int)ap.rssi, (unsigned)ap.primary);
    } else {
        ATOM_LOG_STAGE(TAG, "ready", "wifi ok");
    }
    char utc[32] = {};
    char local[32] = {};
    (void)astrolabe_time_format_utc(utc, sizeof(utc));
    (void)astrolabe_time_format_local(local, sizeof(local));
    ATOM_LOG_STAGE(TAG, "ready", "time %s utc=%s local=%s tz=%s",
                   astrolabe_time_valid() ? "ok" : "stale",
                   utc[0] != '\0' ? utc : "-",
                   local[0] != '\0' ? local : "-",
                   astrolabe_time_timezone());
    ATOM_LOG_STAGE(TAG, "ready", "faculty %s (%s)", s_faculty_name, s_faculty_slug);
    ATOM_LOG_STAGE(TAG, "ready", "pipeline %s/functions/v1/voice-pipeline face=%s", MYNAH_SUPABASE_URL,
                   ASTROLABE_FACULTY_ATOM_FACE_NAME);
    ATOM_LOG_STAGE(TAG, "ready", "listening — speak to run STT->LLM->TTS");
    ATOM_LOG_STAGE(TAG, "ready", "serial: help | qa audio | ./scripts/facultyatom_screenshot.py -p PORT");
    ATOM_LOG_STAGE(TAG, "ready", "monitor: ./scripts/facultyatom_build.sh -p PORT monitor");
}

static esp_err_t wifi_start(void)
{
    if (strlen(MYNAH_WIFI_SSID) == 0) {
        ATOM_LOG_STAGE_W(TAG, "wifi", "MYNAH_WIFI_SSID empty — set include/secrets.local.h");
        return ESP_ERR_INVALID_STATE;
    }
    if (strlen(MYNAH_SUPABASE_URL) == 0 || strlen(MYNAH_SUPABASE_ANON_KEY) == 0) {
        ATOM_LOG_STAGE_W(TAG, "wifi", "Supabase secrets missing — voice pipeline will fail");
    }

    s_wifi_events = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL, NULL));

    wifi_config_t wifi = {};
    atom_strlcpy((char *)wifi.sta.ssid, MYNAH_WIFI_SSID, sizeof(wifi.sta.ssid));
    atom_strlcpy((char *)wifi.sta.password, MYNAH_WIFI_PASSWORD, sizeof(wifi.sta.password));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi));
    ESP_ERROR_CHECK(esp_wifi_start());

    ui_set(ATOM_UI_WIFI, MYNAH_WIFI_SSID);
    ATOM_LOG_STAGE(TAG, "wifi", "waiting for IP (20s)…");
    const EventBits_t bits = xEventGroupWaitBits(s_wifi_events, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE,
                                                 pdMS_TO_TICKS(20000));
    if (bits & WIFI_CONNECTED_BIT) {
        return ESP_OK;
    }
    ATOM_LOG_STAGE_E(TAG, "wifi", "connect timeout");
    return ESP_FAIL;
}

static void load_faculty_from_nvs(void)
{
    nvs_handle_t nvs;
    if (nvs_open("faculty", NVS_READONLY, &nvs) != ESP_OK) {
        return;
    }
    size_t len = sizeof(s_faculty_slug);
    if (nvs_get_str(nvs, "faculty_slug", s_faculty_slug, &len) != ESP_OK) {
        atom_strlcpy(s_faculty_slug, ASTROLABE_FACULTY_ATOM_DEFAULT_FACULTY_SLUG, sizeof(s_faculty_slug));
    }
    len = sizeof(s_faculty_name);
    if (nvs_get_str(nvs, "faculty_name", s_faculty_name, &len) != ESP_OK) {
        atom_strlcpy(s_faculty_name, ASTROLABE_FACULTY_ATOM_DEFAULT_FACULTY_NAME, sizeof(s_faculty_name));
    }
    len = sizeof(s_history);
    nvs_get_str(nvs, "history", s_history, &len);
    nvs_close(nvs);
}

static void save_faculty_to_nvs(void)
{
    nvs_handle_t nvs;
    if (nvs_open("faculty", NVS_READWRITE, &nvs) != ESP_OK) {
        return;
    }
    nvs_set_str(nvs, "faculty_slug", s_faculty_slug);
    nvs_set_str(nvs, "faculty_name", s_faculty_name);
    nvs_set_str(nvs, "history", s_history);
    nvs_commit(nvs);
    nvs_close(nvs);
}

void app_main(void)
{
    ATOM_LOG_STAGE(TAG, "boot", "Astrolabe FacultyAtom — M5 AtomS3R + Atomic Voice Base");

    ESP_ERROR_CHECK(nvs_flash_init());
    atom_ota_init();
    load_faculty_from_nvs();
    ATOM_LOG_STAGE(TAG, "boot", "faculty %s (%s)", s_faculty_name, s_faculty_slug);

    ESP_ERROR_CHECK(atom_board_init());
    atom_serial_init();
    ATOM_LOG_STAGE(TAG, "boot", "board audio=%s", atom_board_audio_ready() ? "ok" : "off");
    ESP_ERROR_CHECK(atom_faculty_init());
    atom_faculty_set_ui_notify(bust_ui_refresh);
    s_ui_queue = xQueueCreate(1, sizeof(atom_ui_msg_t));
    xTaskCreate(ui_task, "ui", 6144, NULL, 4, NULL);
    ui_set(ATOM_UI_BOOT, ASTROLABE_FACULTY_ATOM_OTA_CHANNEL);

    if (wifi_start() != ESP_OK) {
        ui_set(ATOM_UI_ERROR, "wifi");
        return;
    }
    atom_ota_maybe_start_recovery_request();

    atom_faculty_request_bust(s_faculty_slug);
    ATOM_LOG_STAGE(TAG, "faculty", "bust preload %s", s_faculty_slug);

    snprintf(s_voice_pipeline_url, sizeof(s_voice_pipeline_url), "%s/functions/v1/voice-pipeline", MYNAH_SUPABASE_URL);
    make_supabase_ws_url(s_voice_stream_url, sizeof(s_voice_stream_url), MYNAH_SUPABASE_URL,
                         "/functions/v1/voice-stream");
    astrolabe_audio_pipeline_config_t pipeline_cfg = {
        .io = {
            .read = pipeline_read,
            .write = pipeline_write,
            .set_rate = pipeline_set_rate,
            .mute = pipeline_mute,
        },
        .on_event = pipeline_event,
        .on_result = pipeline_result,
        .endpoint_url = s_voice_pipeline_url,
        .stream_url = s_voice_stream_url,
        .transport = ASTROLABE_AUDIO_PIPELINE_TRANSPORT_FLASH_POST,
        .api_key = MYNAH_SUPABASE_ANON_KEY,
        .face = ASTROLABE_FACULTY_ATOM_FACE_NAME,
        .faculty_slug = s_faculty_slug,
        .faculty_name = s_faculty_name,
        .system_instruction = ASTROLABE_FACULTY_ATOM_SYSTEM_INSTRUCTION,
        .history = s_history,
        .capture_mount_path = "/spiffs",
        .capture_partition_label = "storage",
        .capture_file_path = "/spiffs/facultyatom_utterance.pcm",
        .sample_rate_hz = ATOM_AUDIO_RATE,
        .frame_samples = 320,
        .rms_start = 2800,
        .rms_end = 280,
        .start_frames = 3,
        .silence_frames = 100,
        .max_seconds = 10,
        .min_ms = 400,
        .capture_cooldown_ms = 2500,
        .capture_ring_slots = 8,
        .capture_segment_ms = 0,
        .listen_priority = 5,
        .voice_priority = 4,
        .listen_stack = 6144,
        .voice_stack = 24576,
    };
    ESP_ERROR_CHECK(astrolabe_audio_pipeline_create(&pipeline_cfg, &s_pipeline));
    atom_qa_bind(&(atom_qa_bind_t){
        .pipeline = s_pipeline,
        .ui = &s_ui,
        .faculty_slug = s_faculty_slug,
        .faculty_name = s_faculty_name,
        .ui_detail = s_detail,
    });
    ESP_ERROR_CHECK(astrolabe_audio_pipeline_start(s_pipeline));
    ui_set(ATOM_UI_LISTEN, NULL);
    facultyatom_log_ready();

    uint32_t last_bust_retry_ms = 0;
    uint32_t last_time_retry_ms = 0;
    while (true) {
        const uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        if (!astrolabe_time_valid() && (last_time_retry_ms == 0 || now_ms - last_time_retry_ms >= 60000)) {
            last_time_retry_ms = now_ms;
            (void)astrolabe_time_retry_if_stale();
        }
        if (atom_faculty_bust_status() == ATOM_FACULTY_BUST_ERROR) {
            if (now_ms - last_bust_retry_ms >= 15000) {
                last_bust_retry_ms = now_ms;
                atom_faculty_request_bust(s_faculty_slug);
                ATOM_LOG_STAGE(TAG, "faculty", "bust retry %s", s_faculty_slug);
            }
        }
        if (atom_button_just_pressed()) {
            atom_strlcpy(s_faculty_slug, ASTROLABE_FACULTY_ATOM_DEFAULT_FACULTY_SLUG, sizeof(s_faculty_slug));
            atom_strlcpy(s_faculty_name, ASTROLABE_FACULTY_ATOM_DEFAULT_FACULTY_NAME, sizeof(s_faculty_name));
            s_history[0] = '\0';
            save_faculty_to_nvs();
            atom_faculty_request_bust(s_faculty_slug);
            ui_set(ATOM_UI_LISTEN, NULL);
            ATOM_LOG_STAGE(TAG, "faculty", "reset to default %s", s_faculty_name);
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
