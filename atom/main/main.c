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

#include "astrolabe_wand_face.h"
#include "atom_board.h"
#include "atom_listen.h"
#include "atom_voice.h"
#include "atom_faculty.h"
#include "atom_util.h"

#if __has_include("secrets.local.h")
#include "secrets.local.h"
#else
#include "secrets.example.h"
#endif

static const char *TAG = "atom_wand";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

static EventGroupHandle_t s_wifi_events;
static atom_ui_state_t s_ui = ATOM_UI_BOOT;
static char s_faculty_slug[64] = ASTROLABE_WAND_DEFAULT_FACULTY_SLUG;
static char s_faculty_name[96] = ASTROLABE_WAND_DEFAULT_FACULTY_NAME;
static char s_history[512];
static char s_detail[96];
static atom_listen_t s_listen;

static void save_faculty_to_nvs(void);
static QueueHandle_t s_ui_queue;

typedef struct {
    atom_ui_state_t state;
    char detail[96];
} atom_ui_msg_t;

static void bust_ui_refresh(void)
{
    ui_set(s_ui, s_detail[0] ? s_detail : NULL);
}

static void ui_set(atom_ui_state_t state, const char *detail)
{
    s_ui = state;
    if (detail != NULL) {
        atom_strlcpy(s_detail, detail, sizeof(s_detail));
    } else {
        s_detail[0] = '\0';
    }
    atom_ui_msg_t msg = {
        .state = state,
    };
    atom_strlcpy(msg.detail, s_detail, sizeof(msg.detail));
    (void)xQueueOverwrite(s_ui_queue, &msg);
}

static void ui_task(void *arg)
{
    (void)arg;
    atom_ui_msg_t msg = {
        .state = ATOM_UI_BOOT,
    };
    while (true) {
        if (xQueueReceive(s_ui_queue, &msg, pdMS_TO_TICKS(200)) == pdTRUE) {
            atom_display_draw_status(msg.state, s_faculty_name, msg.detail);
        } else {
            atom_display_draw_status(s_ui, s_faculty_name, s_detail);
        }
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
        ui_set(ATOM_UI_THINK, "Castalia…");

        atom_voice_result_t result = {};
        const esp_err_t err = atom_voice_post_pcm((const uint8_t *)utterance.samples,
                                                  utterance.sample_count * sizeof(int16_t),
                                                  s_faculty_slug, s_faculty_name, s_history, &result);
        free(utterance.samples);

        if (err != ESP_OK) {
            ui_set(ATOM_UI_ERROR, "voice fail");
            vTaskDelay(pdMS_TO_TICKS(1200));
            ui_set(ATOM_UI_LISTEN, "ready");
            continue;
        }

        bool faculty_changed = false;
        if (result.faculty_slug[0] != '\0' && strcmp(s_faculty_slug, result.faculty_slug) != 0) {
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
        ESP_LOGI(TAG, "transcript: %s", result.transcript);
        ESP_LOGI(TAG, "reply: %s", result.reply);

        if (result.mp3 != NULL && result.mp3_len > 0) {
            ui_set(ATOM_UI_SPEAK, result.faculty_name[0] ? result.faculty_name : s_faculty_name);
            (void)atom_voice_play_mp3(result.mp3, result.mp3_len);
        }
        atom_voice_result_free(&result);
        ui_set(ATOM_UI_LISTEN, "ready");
    }
}

static void listen_task(void *arg)
{
    (void)arg;
    int16_t frame[ATOM_LISTEN_FRAME_SAMPLES];
    while (true) {
        if (s_ui == ATOM_UI_SPEAK || s_ui == ATOM_UI_THINK) {
            vTaskDelay(pdMS_TO_TICKS(40));
            continue;
        }

        size_t got = 0;
        if (atom_audio_read(frame, ATOM_LISTEN_FRAME_SAMPLES, &got, 100) != ESP_OK || got == 0) {
            vTaskDelay(1);
            continue;
        }

        if (s_ui == ATOM_UI_LISTEN || s_ui == ATOM_UI_CAPTURE) {
            const bool capturing = s_listen.speech_active;
            ui_set(capturing ? ATOM_UI_CAPTURE : ATOM_UI_LISTEN, capturing ? "…" : "ready");
        }
        (void)atom_listen_push_frame(&s_listen, frame, got);
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    if (id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT);
        esp_wifi_connect();
    } else if (id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
    }
    (void)data;
}

static esp_err_t wifi_start(void)
{
    if (strlen(MYNAH_WIFI_SSID) == 0) {
        ESP_LOGW(TAG, "MYNAH_WIFI_SSID empty — set include/secrets.local.h");
        return ESP_ERR_INVALID_STATE;
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
    const EventBits_t bits = xEventGroupWaitBits(s_wifi_events, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE,
                                                 pdMS_TO_TICKS(20000));
    return (bits & WIFI_CONNECTED_BIT) ? ESP_OK : ESP_FAIL;
}

static void load_faculty_from_nvs(void)
{
    nvs_handle_t nvs;
    if (nvs_open("wand", NVS_READONLY, &nvs) != ESP_OK) {
        return;
    }
    size_t len = sizeof(s_faculty_slug);
    if (nvs_get_str(nvs, "faculty_slug", s_faculty_slug, &len) != ESP_OK) {
        atom_strlcpy(s_faculty_slug, ASTROLABE_WAND_DEFAULT_FACULTY_SLUG, sizeof(s_faculty_slug));
    }
    len = sizeof(s_faculty_name);
    if (nvs_get_str(nvs, "faculty_name", s_faculty_name, &len) != ESP_OK) {
        atom_strlcpy(s_faculty_name, ASTROLABE_WAND_DEFAULT_FACULTY_NAME, sizeof(s_faculty_name));
    }
    len = sizeof(s_history);
    nvs_get_str(nvs, "history", s_history, &len);
    nvs_close(nvs);
}

static void save_faculty_to_nvs(void)
{
    nvs_handle_t nvs;
    if (nvs_open("wand", NVS_READWRITE, &nvs) != ESP_OK) {
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
    ESP_LOGI(TAG, "Astrolabe Wand — AtomS3R + Atomic Voice Base");
    ui_set(ATOM_UI_BOOT, ASTROLABE_WAND_OTA_CHANNEL);

    ESP_ERROR_CHECK(nvs_flash_init());
    load_faculty_from_nvs();

    ESP_ERROR_CHECK(atom_board_init());
    s_ui_queue = xQueueCreate(1, sizeof(atom_ui_msg_t));
    xTaskCreate(ui_task, "ui", 4096, NULL, 4, NULL);

    if (wifi_start() != ESP_OK) {
        ui_set(ATOM_UI_ERROR, "wifi");
        return;
    }

    ESP_ERROR_CHECK(atom_faculty_init());
    atom_faculty_set_ui_notify(bust_ui_refresh);
    atom_faculty_request_bust(s_faculty_slug);

    ESP_ERROR_CHECK(atom_listen_init(&s_listen));
    xTaskCreate(listen_task, "listen", 6144, NULL, 5, NULL);
    xTaskCreate(voice_worker_task, "voice", 12288, NULL, 4, NULL);
    ui_set(ATOM_UI_LISTEN, "ready");
    ESP_LOGI(TAG, "Wand ready — full-time STT with faculty %s", s_faculty_name);

    while (true) {
        if (atom_button_just_pressed()) {
            atom_strlcpy(s_faculty_slug, ASTROLABE_WAND_DEFAULT_FACULTY_SLUG, sizeof(s_faculty_slug));
            atom_strlcpy(s_faculty_name, ASTROLABE_WAND_DEFAULT_FACULTY_NAME, sizeof(s_faculty_name));
            s_history[0] = '\0';
            save_faculty_to_nvs();
            atom_faculty_request_bust(s_faculty_slug);
            ui_set(ATOM_UI_LISTEN, "reset");
            ESP_LOGI(TAG, "faculty reset to default");
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
