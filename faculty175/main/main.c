#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "astrolabe_faculty175_face.h"
#include "faculty175_board.h"
#include "atom_listen.h"
#include "atom_voice.h"
#include "atom_faculty.h"
#include "atom_faculty_roster.h"
#include "faculty175_gesture.h"
#include "faculty175_touch.h"
#include "atom_log.h"
#include "atom_device_auth.h"
#include "atom_qa.h"
#include "atom_ota.h"
#include "atom_serial.h"
#include "atom_util.h"

#if __has_include("secrets.local.h")
#include "secrets.local.h"
#else
#include "secrets.example.h"
#endif

static const char *TAG = "faculty175";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

static EventGroupHandle_t s_wifi_events;
static faculty175_ui_state_t s_ui = FACULTY175_UI_BOOT;
static char s_faculty_slug[64] = ASTROLABE_FACULTY_DEFAULT_FACULTY_SLUG;
static char s_faculty_name[96] = ASTROLABE_FACULTY_DEFAULT_FACULTY_NAME;
static char s_history[512];
static char s_detail[96];
static atom_listen_t s_listen;
static uint32_t s_voice_turn;
static QueueHandle_t s_voice_queue;
static volatile bool s_voice_capture_enabled;

typedef enum {
    VOICE_EVT_START,
    VOICE_EVT_PCM,
    VOICE_EVT_END,
} voice_evt_type_t;

typedef struct {
    voice_evt_type_t type;
    size_t sample_count;
    int16_t *samples;
} voice_event_t;

static void listen_stream_start(void *ctx)
{
    (void)ctx;
    s_voice_capture_enabled = atom_voice_heap_ready("capture-start");
    if (!s_voice_capture_enabled) {
        ATOM_LOG_STAGE_W(TAG, "capture", "voice capture skipped: low heap");
        return;
    }
    const voice_event_t ev = { .type = VOICE_EVT_START };
    if (xQueueSend(s_voice_queue, &ev, 0) != pdTRUE) {
        s_voice_capture_enabled = false;
    }
}

static void listen_stream_frame(const int16_t *frame, size_t frame_samples, void *ctx)
{
    (void)ctx;
    if (!s_voice_capture_enabled || frame == NULL || frame_samples == 0 || s_voice_queue == NULL) {
        return;
    }
    const size_t bytes = frame_samples * sizeof(int16_t);
    int16_t *copy = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (copy == NULL) {
        copy = malloc(bytes);
    }
    if (copy == NULL) {
        return;
    }
    memcpy(copy, frame, bytes);
    const voice_event_t ev = {
        .type = VOICE_EVT_PCM,
        .sample_count = frame_samples,
        .samples = copy,
    };
    if (xQueueSend(s_voice_queue, &ev, pdMS_TO_TICKS(20)) != pdTRUE) {
        free(copy);
    }
}

static void listen_stream_end(size_t sample_count, void *ctx)
{
    (void)ctx;
    if (!s_voice_capture_enabled) {
        return;
    }
    s_voice_capture_enabled = false;
    const voice_event_t ev = {
        .type = VOICE_EVT_END,
        .sample_count = sample_count,
    };
    (void)xQueueSend(s_voice_queue, &ev, 0);
}

static void faculty_log_ready(void);

static void save_faculty_to_nvs(void);

static void ui_set(faculty175_ui_state_t state, const char *detail);

static void activate_roster_entry(const atom_faculty_roster_entry_t *entry)
{
    if (entry == NULL) {
        return;
    }
    atom_strlcpy(s_faculty_slug, entry->slug, sizeof(s_faculty_slug));
    atom_strlcpy(s_faculty_name, entry->name, sizeof(s_faculty_name));
    s_history[0] = '\0';
    save_faculty_to_nvs();
    atom_faculty_request_bust(s_faculty_slug);
    ui_set(FACULTY175_UI_LISTEN, NULL);
    ATOM_LOG_STAGE(TAG, "faculty", "roster -> %s (%s)", s_faculty_name, s_faculty_slug);
}

static void cycle_roster_by_delta(int delta)
{
    atom_faculty_roster_entry_t entry = {};
    if (atom_faculty_roster_cycle_delta(delta) >= 0 && atom_faculty_roster_active(&entry)) {
        activate_roster_entry(&entry);
    }
}
static QueueHandle_t s_ui_queue;

typedef struct {
    faculty175_ui_state_t state;
    char detail[96];
} atom_ui_msg_t;

static void ui_set(faculty175_ui_state_t state, const char *detail)
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


static void ui_task(void *arg)
{
    (void)arg;
    atom_ui_msg_t msg = {
        .state = FACULTY175_UI_BOOT,
    };
    while (true) {
        if (xQueueReceive(s_ui_queue, &msg, pdMS_TO_TICKS(50)) == pdTRUE) {
            s_ui = msg.state;
            atom_strlcpy(s_detail, msg.detail, sizeof(s_detail));
        }
        const uint32_t anim_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        uint8_t waveform[ATOM_LISTEN_WAVEFORM_LEN];
        uint8_t waveform_stream[ATOM_LISTEN_WAVEFORM_LEN];
        atom_listen_waveform_copy(&s_listen, waveform, sizeof(waveform));
        atom_listen_waveform_stream_copy(&s_listen, waveform_stream, sizeof(waveform_stream));
        faculty175_display_draw_status(s_ui,
                                       s_faculty_name,
                                       s_detail,
                                       anim_ms,
                                       waveform,
                                       waveform_stream,
                                       sizeof(waveform));
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

static void handle_voice_result(uint32_t turn, uint32_t t0, atom_voice_result_t *result)
{
    bool faculty_changed = false;
    if (result->faculty_slug[0] != '\0' && strcmp(s_faculty_slug, result->faculty_slug) != 0) {
        ATOM_LOG_STAGE(TAG, "faculty", "active %s -> %s", s_faculty_slug, result->faculty_slug);
        atom_strlcpy(s_faculty_slug, result->faculty_slug, sizeof(s_faculty_slug));
        faculty_changed = true;
    }
    if (result->faculty_name[0] != '\0') {
        atom_strlcpy(s_faculty_name, result->faculty_name, sizeof(s_faculty_name));
    }
    append_history(result->transcript, result->reply);
    if (faculty_changed) {
        atom_faculty_request_bust(s_faculty_slug);
        save_faculty_to_nvs();
    } else if (result->faculty_slug[0] != '\0' || result->faculty_name[0] != '\0') {
        save_faculty_to_nvs();
    }
    if (atom_faculty_bust_status() != ATOM_FACULTY_BUST_READY ||
        strcmp(atom_faculty_loaded_slug(), s_faculty_slug) != 0) {
        atom_faculty_request_bust(s_faculty_slug);
    }

    if ((result->mp3 != NULL && result->mp3_len > 0) || (result->mp3_path[0] != '\0' && result->mp3_len > 0)) {
        ui_set(FACULTY175_UI_SPEAK, result->faculty_name[0] ? result->faculty_name : s_faculty_name);
        const esp_err_t play_err = result->mp3_path[0] != '\0'
            ? atom_voice_play_mp3_file(result->mp3_path, result->mp3_len)
            : atom_voice_play_mp3(result->mp3, result->mp3_len);
        if (play_err != ESP_OK) {
            ATOM_LOG_STAGE_E(TAG, "tts", "playback failed: %s", esp_err_to_name(play_err));
        }
    }
    atom_voice_result_free(result);
    ui_set(FACULTY175_UI_LISTEN, NULL);
    ATOM_LOG_STAGE(TAG, "listen", "ready (turn #%u total %ums)", (unsigned)turn,
                   (unsigned)(atom_log_ms() - t0));
}

static void voice_worker_task(void *arg)
{
    (void)arg;
    while (true) {
        voice_event_t ev = {};
        if (xQueueReceive(s_voice_queue, &ev, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        switch (ev.type) {
            case VOICE_EVT_START: {
                const esp_err_t err = atom_voice_stream_begin(s_faculty_slug, s_faculty_name, s_history);
                if (err != ESP_OK) {
                    ATOM_LOG_STAGE_E(TAG, "stream", "begin failed: %s", esp_err_to_name(err));
                    atom_voice_stream_cancel();
                }
                break;
            }
            case VOICE_EVT_PCM:
                if (ev.samples != NULL) {
                    if (atom_voice_stream_write(ev.samples, ev.sample_count) != ESP_OK) {
                        ATOM_LOG_STAGE_W(TAG, "stream", "pcm write failed — cancelled");
                    }
                    free(ev.samples);
                }
                break;
            case VOICE_EVT_END: {
                const uint32_t turn = ++s_voice_turn;
                const float dur_s = (float)ev.sample_count / (float)FACULTY175_AUDIO_RATE;
                ATOM_LOG_STAGE(TAG, "turn", "#%u streamed %.2fs (%u samples)", (unsigned)turn, dur_s,
                               (unsigned)ev.sample_count);
                ui_set(FACULTY175_UI_THINK, "Castalia…");
                ATOM_LOG_STAGE(TAG, "pipeline", "STT->LLM->TTS via voice-pipeline (face=%s)",
                               ASTROLABE_FACULTY_FACE_NAME);

                atom_voice_result_t result = {};
                const uint32_t t0 = atom_log_ms();
                const esp_err_t err =
                    atom_voice_stream_finish(s_faculty_slug, s_faculty_name, &result);
                if (err != ESP_OK) {
                    ATOM_LOG_STAGE_E(TAG, "pipeline", "turn #%u failed after %ums", (unsigned)turn,
                                     (unsigned)(atom_log_ms() - t0));
                    ui_set(FACULTY175_UI_ERROR, "voice fail");
                    vTaskDelay(pdMS_TO_TICKS(1200));
                    ui_set(FACULTY175_UI_LISTEN, NULL);
                    ATOM_LOG_STAGE(TAG, "listen", "ready");
                    break;
                }
                handle_voice_result(turn, t0, &result);
                break;
            }
            default:
                break;
        }
    }
}

static void listen_task(void *arg)
{
    (void)arg;
    int16_t frame[ATOM_LISTEN_FRAME_SAMPLES];
    bool was_capturing = false;
    uint32_t capture_clear_ms = 0;
    while (true) {
        if (atom_ota_active() || s_ui == FACULTY175_UI_SPEAK || s_ui == FACULTY175_UI_THINK) {
            was_capturing = false;
            capture_clear_ms = 0;
            vTaskDelay(pdMS_TO_TICKS(40));
            continue;
        }

        size_t got = 0;
        if (!faculty175_board_audio_ready()) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        if (faculty175_audio_read(frame, ATOM_LISTEN_FRAME_SAMPLES, &got, 100) != ESP_OK || got == 0) {
            vTaskDelay(1);
            continue;
        }

        (void)atom_listen_push_frame(&s_listen, frame, got);

        const bool capturing = s_listen.speech_active;
        const uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        if ((s_ui == FACULTY175_UI_LISTEN || s_ui == FACULTY175_UI_CAPTURE) && capturing && !was_capturing) {
            ATOM_LOG_STAGE(TAG, "capture", "speech detected — hearing");
            ui_set(FACULTY175_UI_CAPTURE, NULL);
            was_capturing = true;
            capture_clear_ms = 0;
        } else if ((s_ui == FACULTY175_UI_LISTEN || s_ui == FACULTY175_UI_CAPTURE) && !capturing && was_capturing) {
            if (capture_clear_ms == 0) {
                capture_clear_ms = now_ms;
            } else if (now_ms - capture_clear_ms >= 400) {
                ATOM_LOG_STAGE(TAG, "capture", "silence — back to listening");
                ui_set(FACULTY175_UI_LISTEN, NULL);
                was_capturing = false;
                capture_clear_ms = 0;
            }
        } else if (capturing) {
            capture_clear_ms = 0;
        }
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
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
    }
}

static void faculty_log_ready(void)
{
    wifi_ap_record_t ap = {};
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        ATOM_LOG_STAGE(TAG, "ready", "wifi ok rssi=%d ch=%u", (int)ap.rssi, (unsigned)ap.primary);
    } else {
        ATOM_LOG_STAGE(TAG, "ready", "wifi ok");
    }
    ATOM_LOG_STAGE(TAG, "ready", "faculty %s (%s)", s_faculty_name, s_faculty_slug);
    ATOM_LOG_STAGE(TAG, "ready", "pipeline %s/functions/v1/voice-pipeline face=%s", MYNAH_SUPABASE_URL,
                   ASTROLABE_FACULTY_FACE_NAME);
    ATOM_LOG_STAGE(TAG, "ready", "always-on VAD → voice-stream STT (face=%s)", ASTROLABE_FACULTY_FACE_NAME);
    ATOM_LOG_STAGE(TAG, "ready", "serial: help | qa audio | ota status | screen.bmp");
    ATOM_LOG_STAGE(TAG, "ready", "monitor: ./scripts/faculty175_build.sh -p PORT monitor");
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

    ui_set(FACULTY175_UI_WIFI, MYNAH_WIFI_SSID);
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
    atom_faculty_roster_ensure_default();

    nvs_handle_t nvs;
    bool has_slug = false;
    if (nvs_open("faculty", NVS_READONLY, &nvs) == ESP_OK) {
        size_t len = sizeof(s_faculty_slug);
        if (nvs_get_str(nvs, "faculty_slug", s_faculty_slug, &len) == ESP_OK && s_faculty_slug[0] != '\0') {
            has_slug = true;
        }
        len = sizeof(s_faculty_name);
        if (nvs_get_str(nvs, "faculty_name", s_faculty_name, &len) != ESP_OK || s_faculty_name[0] == '\0') {
            atom_strlcpy(s_faculty_name, ASTROLABE_FACULTY_DEFAULT_FACULTY_NAME, sizeof(s_faculty_name));
        }
        len = sizeof(s_history);
        nvs_get_str(nvs, "history", s_history, &len);
        nvs_close(nvs);
    }

    if (!has_slug || strcmp(s_faculty_slug, "a.einstein") == 0) {
        atom_faculty_roster_entry_t active = {};
        if (atom_faculty_roster_active(&active)) {
            atom_strlcpy(s_faculty_slug, active.slug, sizeof(s_faculty_slug));
            atom_strlcpy(s_faculty_name, active.name, sizeof(s_faculty_name));
        } else {
            atom_strlcpy(s_faculty_slug, ASTROLABE_FACULTY_DEFAULT_FACULTY_SLUG, sizeof(s_faculty_slug));
            atom_strlcpy(s_faculty_name, ASTROLABE_FACULTY_DEFAULT_FACULTY_NAME, sizeof(s_faculty_name));
        }
    }
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
    ATOM_LOG_STAGE(TAG, "boot", "Astrolabe Faculty — Waveshare ESP32-S3 Touch AMOLED 1.75C");

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(atom_device_auth_init());
    atom_ota_init();
    load_faculty_from_nvs();
    ATOM_LOG_STAGE(TAG, "boot", "faculty %s (%s)", s_faculty_name, s_faculty_slug);

    ESP_ERROR_CHECK(faculty175_board_init());
    (void)faculty175_touch_init();
    faculty175_gesture_start_task();
    atom_serial_init();
    ATOM_LOG_STAGE(TAG, "boot", "board audio=%s", faculty175_board_audio_ready() ? "ok" : "off");
    ESP_ERROR_CHECK(atom_faculty_init());
    atom_faculty_set_ui_notify(bust_ui_refresh);
    s_ui_queue = xQueueCreate(1, sizeof(atom_ui_msg_t));
    xTaskCreate(ui_task, "ui", 8192, NULL, 4, NULL);
    ui_set(FACULTY175_UI_BOOT, ASTROLABE_FACULTY_OTA_CHANNEL);
    atom_faculty_request_bust(s_faculty_slug);

    if (wifi_start() != ESP_OK) {
        ui_set(FACULTY175_UI_ERROR, "wifi");
        return;
    }
    atom_ota_maybe_start_recovery_request();

    atom_faculty_request_bust(s_faculty_slug);
    atom_faculty_prefetch_roster();
    ATOM_LOG_STAGE(TAG, "faculty", "bust preload %s", s_faculty_slug);

    ESP_ERROR_CHECK(atom_listen_init(&s_listen));
    s_voice_queue = xQueueCreate(16, sizeof(voice_event_t));
    atom_listen_set_stream_cb(&s_listen, &(atom_listen_stream_cb_t){
        .on_speech_start = listen_stream_start,
        .on_frame = listen_stream_frame,
        .on_speech_end = listen_stream_end,
        .ctx = NULL,
    });
    atom_qa_bind(&(atom_qa_bind_t){
        .listen = &s_listen,
        .ui = &s_ui,
        .faculty_slug = s_faculty_slug,
        .faculty_name = s_faculty_name,
        .ui_detail = s_detail,
    });
    xTaskCreate(listen_task, "listen", 4096, NULL, 5, NULL);
    xTaskCreate(voice_worker_task, "voice", 12288, NULL, 4, NULL);
    ui_set(FACULTY175_UI_LISTEN, NULL);
    faculty_log_ready();

    uint32_t last_bust_retry_ms = 0;
    while (true) {
        const uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

        faculty175_gesture_t gesture = {};
        if (faculty175_gesture_consume(&gesture) &&
            (s_ui == FACULTY175_UI_LISTEN || s_ui == FACULTY175_UI_CAPTURE)) {
            if (gesture.kind == FACULTY175_GESTURE_SWIPE_LEFT) {
                cycle_roster_by_delta(1);
            } else if (gesture.kind == FACULTY175_GESTURE_SWIPE_RIGHT) {
                cycle_roster_by_delta(-1);
            }
        }

        if (atom_faculty_bust_status() == ATOM_FACULTY_BUST_ERROR) {
            if (now_ms - last_bust_retry_ms >= 15000) {
                last_bust_retry_ms = now_ms;
                atom_faculty_request_bust(s_faculty_slug);
                ATOM_LOG_STAGE(TAG, "faculty", "bust retry %s", s_faculty_slug);
            }
        }
        if (faculty175_button_just_pressed()) {
            atom_faculty_roster_entry_t entry = {};
            if (atom_faculty_roster_cycle_next() >= 0 && atom_faculty_roster_active(&entry)) {
                activate_roster_entry(&entry);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
