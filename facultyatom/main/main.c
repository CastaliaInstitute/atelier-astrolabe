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

#include "astrolabe_faculty_atom_face.h"
#include "atom_board.h"
#include "atom_listen.h"
#include "atom_voice.h"
#include "atom_faculty.h"
#include "atom_log.h"
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
static atom_listen_t s_listen;
static uint32_t s_voice_turn;

static void facultyatom_log_ready(void);

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


static void ui_task(void *arg)
{
    (void)arg;
    atom_ui_msg_t msg = {
        .state = ATOM_UI_BOOT,
    };
    while (true) {
        if (xQueueReceive(s_ui_queue, &msg, pdMS_TO_TICKS(50)) == pdTRUE) {
            s_ui = msg.state;
            atom_strlcpy(s_detail, msg.detail, sizeof(s_detail));
        }
        const uint32_t anim_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        uint8_t waveform[ATOM_LISTEN_WAVEFORM_LEN];
        atom_listen_waveform_copy(&s_listen, waveform, sizeof(waveform));
        atom_display_draw_status(s_ui, s_faculty_name, s_detail, anim_ms, waveform, sizeof(waveform));
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

static void voice_worker_task(void *arg)
{
    (void)arg;
    while (true) {
        atom_utterance_t utterance = {};
        if (xQueueReceive(s_listen.utterance_queue, &utterance, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        const uint32_t turn = ++s_voice_turn;
        const float dur_s = (float)utterance.sample_count / (float)ATOM_AUDIO_RATE;
        ATOM_LOG_STAGE(TAG, "turn", "#%u utterance %.2fs (%u samples)", (unsigned)turn, dur_s,
                       (unsigned)utterance.sample_count);
        ui_set(ATOM_UI_THINK, "Castalia…");
        ATOM_LOG_STAGE(TAG, "pipeline", "STT->LLM->TTS via voice-pipeline (face=%s)",
                       ASTROLABE_FACULTY_ATOM_FACE_NAME);

        atom_voice_result_t result = {};
        const uint32_t t0 = atom_log_ms();
        const esp_err_t err = atom_voice_post_pcm((const uint8_t *)utterance.samples,
                                                  utterance.sample_count * sizeof(int16_t),
                                                  s_faculty_slug, s_faculty_name, s_history, &result);
        free(utterance.samples);

        if (err != ESP_OK) {
            ATOM_LOG_STAGE_E(TAG, "pipeline", "turn #%u failed after %ums", (unsigned)turn,
                             (unsigned)(atom_log_ms() - t0));
            ui_set(ATOM_UI_ERROR, "voice fail");
            vTaskDelay(pdMS_TO_TICKS(1200));
            ui_set(ATOM_UI_LISTEN, NULL);
            ATOM_LOG_STAGE(TAG, "listen", "ready");
            continue;
        }

        bool faculty_changed = false;
        if (result.faculty_slug[0] != '\0' && strcmp(s_faculty_slug, result.faculty_slug) != 0) {
            ATOM_LOG_STAGE(TAG, "faculty", "active %s -> %s", s_faculty_slug, result.faculty_slug);
            atom_strlcpy(s_faculty_slug, result.faculty_slug, sizeof(s_faculty_slug));
            faculty_changed = true;
        }
        if (result.faculty_name[0] != '\0') {
            atom_strlcpy(s_faculty_name, result.faculty_name, sizeof(s_faculty_name));
        }
        append_history(result.transcript, result.reply);
        if (faculty_changed) {
            atom_faculty_request_bust(s_faculty_slug);
            save_faculty_to_nvs();
        } else if (result.faculty_slug[0] != '\0' || result.faculty_name[0] != '\0') {
            save_faculty_to_nvs();
        }
        if (atom_faculty_bust_status() != ATOM_FACULTY_BUST_READY ||
            strcmp(atom_faculty_loaded_slug(), s_faculty_slug) != 0) {
            atom_faculty_request_bust(s_faculty_slug);
        }

        if (result.mp3 != NULL && result.mp3_len > 0) {
            ui_set(ATOM_UI_SPEAK, result.faculty_name[0] ? result.faculty_name : s_faculty_name);
            const esp_err_t play_err = atom_voice_play_mp3(result.mp3, result.mp3_len);
            if (play_err != ESP_OK) {
                ATOM_LOG_STAGE_E(TAG, "tts", "playback failed: %s", esp_err_to_name(play_err));
            }
        }
        atom_voice_result_free(&result);
        ui_set(ATOM_UI_LISTEN, NULL);
        ATOM_LOG_STAGE(TAG, "listen", "ready (turn #%u total %ums)", (unsigned)turn,
                       (unsigned)(atom_log_ms() - t0));
    }
}

static void listen_task(void *arg)
{
    (void)arg;
    int16_t frame[ATOM_LISTEN_FRAME_SAMPLES];
    bool was_capturing = false;
    uint32_t capture_clear_ms = 0;
    while (true) {
        if (s_ui == ATOM_UI_SPEAK || s_ui == ATOM_UI_THINK) {
            was_capturing = false;
            capture_clear_ms = 0;
            vTaskDelay(pdMS_TO_TICKS(40));
            continue;
        }

        size_t got = 0;
        if (!atom_board_audio_ready()) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        if (atom_audio_read(frame, ATOM_LISTEN_FRAME_SAMPLES, &got, 100) != ESP_OK || got == 0) {
            vTaskDelay(1);
            continue;
        }

        (void)atom_listen_push_frame(&s_listen, frame, got);

        const bool capturing = s_listen.speech_active;
        const uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        if ((s_ui == ATOM_UI_LISTEN || s_ui == ATOM_UI_CAPTURE) && capturing && !was_capturing) {
            ATOM_LOG_STAGE(TAG, "capture", "speech detected — hearing");
            ui_set(ATOM_UI_CAPTURE, NULL);
            was_capturing = true;
            capture_clear_ms = 0;
        } else if ((s_ui == ATOM_UI_LISTEN || s_ui == ATOM_UI_CAPTURE) && !capturing && was_capturing) {
            if (capture_clear_ms == 0) {
                capture_clear_ms = now_ms;
            } else if (now_ms - capture_clear_ms >= 400) {
                ATOM_LOG_STAGE(TAG, "capture", "silence — back to listening");
                ui_set(ATOM_UI_LISTEN, NULL);
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

static void facultyatom_log_ready(void)
{
    wifi_ap_record_t ap = {};
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        ATOM_LOG_STAGE(TAG, "ready", "wifi ok rssi=%d ch=%u", (int)ap.rssi, (unsigned)ap.primary);
    } else {
        ATOM_LOG_STAGE(TAG, "ready", "wifi ok");
    }
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

    atom_faculty_request_bust(s_faculty_slug);
    ATOM_LOG_STAGE(TAG, "faculty", "bust preload %s", s_faculty_slug);

    ESP_ERROR_CHECK(atom_listen_init(&s_listen));
    atom_qa_bind(&(atom_qa_bind_t){
        .listen = &s_listen,
        .ui = &s_ui,
        .faculty_slug = s_faculty_slug,
        .faculty_name = s_faculty_name,
        .ui_detail = s_detail,
    });
    xTaskCreate(listen_task, "listen", 6144, NULL, 5, NULL);
    xTaskCreate(voice_worker_task, "voice", 12288, NULL, 4, NULL);
    ui_set(ATOM_UI_LISTEN, NULL);
    facultyatom_log_ready();

    uint32_t last_bust_retry_ms = 0;
    while (true) {
        if (atom_faculty_bust_status() == ATOM_FACULTY_BUST_ERROR) {
            const uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
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
