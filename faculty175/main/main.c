#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_attr.h"
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
#include "faculty175_listen.h"
#include "faculty175_voice.h"
#include "faculty175_faculty.h"
#include "faculty175_faculty_roster.h"
#include "faculty175_gesture.h"
#include "faculty175_touch.h"
#include "faculty175_log.h"
#include "faculty175_device_auth.h"
#include "faculty175_qa.h"
#include "faculty175_ota.h"
#include "faculty175_serial.h"
#include "faculty175_util.h"

#if __has_include("secrets.local.h")
#include "secrets.local.h"
#else
#include "secrets.example.h"
#endif

static const char *TAG = "faculty175";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define VOICE_FRAME_POOL_LEN 320
#define VOICE_QUEUE_LEN 320
#define VOICE_WORKER_STACK_BYTES 32768

static EventGroupHandle_t s_wifi_events;
static faculty175_ui_state_t s_ui = FACULTY175_UI_BOOT;
static char s_faculty_slug[64] = ASTROLABE_FACULTY_DEFAULT_FACULTY_SLUG;
static char s_faculty_name[96] = ASTROLABE_FACULTY_DEFAULT_FACULTY_NAME;
static char s_history[512];
static char s_detail[96];
static faculty175_listen_t s_listen;
static uint32_t s_voice_turn;
static QueueHandle_t s_voice_queue;
static volatile bool s_voice_capture_enabled;
static volatile uint32_t s_voice_capture_frames_queued;
static volatile uint32_t s_voice_capture_frames_dropped;
static volatile uint32_t s_voice_capture_samples_queued;
static volatile uint32_t s_voice_capture_samples_dropped;
EXT_RAM_BSS_ATTR static int16_t s_voice_frame_pool[VOICE_FRAME_POOL_LEN][FACULTY175_LISTEN_FRAME_SAMPLES];
static bool s_voice_frame_pool_used[VOICE_FRAME_POOL_LEN];
static portMUX_TYPE s_voice_frame_pool_mux = portMUX_INITIALIZER_UNLOCKED;

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

static int16_t *voice_frame_pool_acquire(void)
{
    int16_t *frame = NULL;
    portENTER_CRITICAL(&s_voice_frame_pool_mux);
    for (size_t i = 0; i < VOICE_FRAME_POOL_LEN; ++i) {
        if (!s_voice_frame_pool_used[i]) {
            s_voice_frame_pool_used[i] = true;
            frame = s_voice_frame_pool[i];
            break;
        }
    }
    portEXIT_CRITICAL(&s_voice_frame_pool_mux);
    return frame;
}

static void voice_frame_pool_release(int16_t *frame)
{
    if (frame == NULL) {
        return;
    }
    portENTER_CRITICAL(&s_voice_frame_pool_mux);
    for (size_t i = 0; i < VOICE_FRAME_POOL_LEN; ++i) {
        if (frame == s_voice_frame_pool[i]) {
            s_voice_frame_pool_used[i] = false;
            break;
        }
    }
    portEXIT_CRITICAL(&s_voice_frame_pool_mux);
}

static void listen_stream_start(void *ctx)
{
    (void)ctx;
    s_voice_capture_enabled = faculty175_voice_heap_ready("capture-start");
    s_voice_capture_frames_queued = 0;
    s_voice_capture_frames_dropped = 0;
    s_voice_capture_samples_queued = 0;
    s_voice_capture_samples_dropped = 0;
    if (!s_voice_capture_enabled) {
        FACULTY175_LOG_STAGE_W(TAG, "capture", "voice capture skipped: low heap");
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
    if (frame_samples > FACULTY175_LISTEN_FRAME_SAMPLES) {
        return;
    }
    int16_t *copy = voice_frame_pool_acquire();
    if (copy == NULL) {
        s_voice_capture_frames_dropped++;
        s_voice_capture_samples_dropped += (uint32_t)frame_samples;
        return;
    }
    const size_t bytes = frame_samples * sizeof(int16_t);
    memcpy(copy, frame, bytes);
    const voice_event_t ev = {
        .type = VOICE_EVT_PCM,
        .sample_count = frame_samples,
        .samples = copy,
    };
    if (xQueueSend(s_voice_queue, &ev, pdMS_TO_TICKS(80)) != pdTRUE) {
        s_voice_capture_frames_dropped++;
        s_voice_capture_samples_dropped += (uint32_t)frame_samples;
        voice_frame_pool_release(copy);
    } else {
        s_voice_capture_frames_queued++;
        s_voice_capture_samples_queued += (uint32_t)frame_samples;
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
    if (xQueueSend(s_voice_queue, &ev, pdMS_TO_TICKS(200)) != pdTRUE) {
        FACULTY175_LOG_STAGE_W(TAG, "capture", "end event dropped queued=%lu dropped=%lu",
                               (unsigned long)s_voice_capture_samples_queued,
                               (unsigned long)s_voice_capture_samples_dropped);
    }
    FACULTY175_LOG_STAGE(TAG, "capture", "queue samples=%lu/%u dropped=%lu frames=%lu dropped_frames=%lu",
                         (unsigned long)s_voice_capture_samples_queued,
                         (unsigned)sample_count,
                         (unsigned long)s_voice_capture_samples_dropped,
                         (unsigned long)s_voice_capture_frames_queued,
                         (unsigned long)s_voice_capture_frames_dropped);
}

static void faculty_log_ready(void);

static void save_faculty_to_nvs(void);

static void ui_set(faculty175_ui_state_t state, const char *detail);

static void activate_roster_entry(const faculty175_faculty_roster_entry_t *entry)
{
    if (entry == NULL) {
        return;
    }
    faculty175_strlcpy(s_faculty_slug, entry->slug, sizeof(s_faculty_slug));
    faculty175_strlcpy(s_faculty_name, entry->name, sizeof(s_faculty_name));
    s_history[0] = '\0';
    save_faculty_to_nvs();
    faculty175_faculty_request_bust(s_faculty_slug);
    ui_set(FACULTY175_UI_LISTEN, NULL);
    FACULTY175_LOG_STAGE(TAG, "faculty", "roster -> %s (%s)", s_faculty_name, s_faculty_slug);
}

static void cycle_roster_by_delta(int delta)
{
    faculty175_faculty_roster_entry_t entry = {};
    if (faculty175_faculty_roster_cycle_delta(delta) >= 0 && faculty175_faculty_roster_active(&entry)) {
        activate_roster_entry(&entry);
    }
}

static bool qa_set_faculty_active(const char *slug, const char *name)
{
    if (slug == NULL || slug[0] == '\0') {
        return false;
    }
    faculty175_strlcpy(s_faculty_slug, slug, sizeof(s_faculty_slug));
    faculty175_strlcpy(s_faculty_name, (name != NULL && name[0] != '\0') ? name : slug, sizeof(s_faculty_name));
    s_history[0] = '\0';
    save_faculty_to_nvs();
    faculty175_faculty_request_bust(s_faculty_slug);
    ui_set(FACULTY175_UI_LISTEN, NULL);
    FACULTY175_LOG_STAGE(TAG, "faculty", "qa -> %s (%s)", s_faculty_name, s_faculty_slug);
    return true;
}

static bool qa_fetch_faculty_active(const char *slug, const char *name)
{
    if (slug == NULL || slug[0] == '\0') {
        return false;
    }
    faculty175_strlcpy(s_faculty_slug, slug, sizeof(s_faculty_slug));
    faculty175_strlcpy(s_faculty_name, (name != NULL && name[0] != '\0') ? name : slug, sizeof(s_faculty_name));
    s_history[0] = '\0';
    save_faculty_to_nvs();
    faculty175_faculty_request_bust_download(s_faculty_slug);
    ui_set(FACULTY175_UI_LISTEN, NULL);
    FACULTY175_LOG_STAGE(TAG, "faculty", "qa fetch -> %s (%s)", s_faculty_name, s_faculty_slug);
    return true;
}
static QueueHandle_t s_ui_queue;

typedef struct {
    faculty175_ui_state_t state;
    char detail[96];
} faculty175_ui_msg_t;

static void ui_set(faculty175_ui_state_t state, const char *detail)
{
    s_ui = state;
    if (detail != NULL) {
        faculty175_strlcpy(s_detail, detail, sizeof(s_detail));
    } else {
        s_detail[0] = '\0';
    }
    if (s_ui_queue == NULL) {
        return;
    }
    faculty175_ui_msg_t msg = {
        .state = state,
    };
    faculty175_strlcpy(msg.detail, s_detail, sizeof(msg.detail));
    (void)xQueueOverwrite(s_ui_queue, &msg);
}

static void ui_redraw(void)
{
    if (s_ui_queue == NULL) {
        return;
    }
    faculty175_ui_msg_t msg = {
        .state = s_ui,
    };
    faculty175_strlcpy(msg.detail, s_detail, sizeof(msg.detail));
    (void)xQueueOverwrite(s_ui_queue, &msg);
}

static void bust_ui_refresh(void)
{
    ui_redraw();
}


static void ui_task(void *arg)
{
    (void)arg;
    faculty175_ui_msg_t msg = {
        .state = FACULTY175_UI_BOOT,
    };
    while (true) {
        if (xQueueReceive(s_ui_queue, &msg, pdMS_TO_TICKS(50)) == pdTRUE) {
            s_ui = msg.state;
            faculty175_strlcpy(s_detail, msg.detail, sizeof(s_detail));
        }
        const uint32_t anim_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        uint8_t waveform[FACULTY175_LISTEN_WAVEFORM_LEN];
        uint8_t waveform_stream[FACULTY175_LISTEN_WAVEFORM_LEN];
        faculty175_listen_waveform_copy(&s_listen, waveform, sizeof(waveform));
        faculty175_listen_waveform_stream_copy(&s_listen, waveform_stream, sizeof(waveform_stream));
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
        faculty175_strlcpy(s_history + strlen(s_history), " || ", sizeof(s_history) - strlen(s_history));
    }
    faculty175_strlcpy(s_history + strlen(s_history), chunk, sizeof(s_history) - strlen(s_history));
    if (strlen(s_history) > sizeof(s_history) / 2) {
        memmove(s_history, s_history + strlen(s_history) / 2, strlen(s_history) / 2 + 1);
    }
}

static void handle_voice_result(uint32_t turn, uint32_t t0, faculty175_voice_result_t *result)
{
    bool faculty_changed = false;
    if (result->faculty_slug[0] != '\0' && strcmp(s_faculty_slug, result->faculty_slug) != 0) {
        FACULTY175_LOG_STAGE(TAG, "faculty", "active %s -> %s", s_faculty_slug, result->faculty_slug);
        faculty175_strlcpy(s_faculty_slug, result->faculty_slug, sizeof(s_faculty_slug));
        faculty_changed = true;
    }
    if (result->faculty_name[0] != '\0') {
        faculty175_strlcpy(s_faculty_name, result->faculty_name, sizeof(s_faculty_name));
    }
    append_history(result->transcript, result->reply);
    if (faculty_changed) {
        faculty175_faculty_request_bust(s_faculty_slug);
        save_faculty_to_nvs();
    } else if (result->faculty_slug[0] != '\0' || result->faculty_name[0] != '\0') {
        save_faculty_to_nvs();
    }
    if (faculty175_faculty_bust_status() != FACULTY175_FACULTY_BUST_READY ||
        strcmp(faculty175_faculty_loaded_slug(), s_faculty_slug) != 0) {
        faculty175_faculty_request_bust(s_faculty_slug);
    }

    if ((result->mp3 != NULL && result->mp3_len > 0) || (result->mp3_path[0] != '\0' && result->mp3_len > 0)) {
        ui_set(FACULTY175_UI_SPEAK, result->faculty_name[0] ? result->faculty_name : s_faculty_name);
        const esp_err_t play_err = result->mp3_path[0] != '\0'
            ? faculty175_voice_play_mp3_file(result->mp3_path, result->mp3_len)
            : faculty175_voice_play_mp3(result->mp3, result->mp3_len);
        if (play_err != ESP_OK) {
            FACULTY175_LOG_STAGE_E(TAG, "tts", "playback failed: %s", esp_err_to_name(play_err));
        }
    }
    faculty175_voice_result_free(result);
    ui_set(FACULTY175_UI_LISTEN, NULL);
    FACULTY175_LOG_STAGE(TAG, "listen", "ready (turn #%u total %ums)", (unsigned)turn,
                   (unsigned)(faculty175_log_ms() - t0));
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
                const esp_err_t err = faculty175_voice_stream_begin(s_faculty_slug, s_faculty_name, s_history);
                if (err != ESP_OK) {
                    FACULTY175_LOG_STAGE_E(TAG, "stream", "begin failed: %s", esp_err_to_name(err));
                    faculty175_voice_stream_cancel();
                }
                break;
            }
            case VOICE_EVT_PCM:
                if (ev.samples != NULL) {
                    if (faculty175_voice_stream_write(ev.samples, ev.sample_count) != ESP_OK) {
                        FACULTY175_LOG_STAGE_W(TAG, "stream", "pcm write failed — cancelled");
                    }
                    voice_frame_pool_release(ev.samples);
                }
                break;
            case VOICE_EVT_END: {
                const uint32_t turn = ++s_voice_turn;
                const float dur_s = (float)ev.sample_count / (float)FACULTY175_AUDIO_RATE;
                FACULTY175_LOG_STAGE(TAG, "turn", "#%u streamed %.2fs (%u samples)", (unsigned)turn, dur_s,
                               (unsigned)ev.sample_count);
                ui_set(FACULTY175_UI_THINK, "Castalia…");
                FACULTY175_LOG_STAGE(TAG, "pipeline", "STT->LLM->TTS via voice-pipeline (face=%s)",
                               ASTROLABE_FACULTY_FACE_NAME);

                faculty175_voice_result_t result = {};
                const uint32_t t0 = faculty175_log_ms();
                const esp_err_t err =
                    faculty175_voice_stream_finish(s_faculty_slug, s_faculty_name, &result);
                if (err != ESP_OK) {
                    FACULTY175_LOG_STAGE_E(TAG, "pipeline", "turn #%u failed after %ums", (unsigned)turn,
                                     (unsigned)(faculty175_log_ms() - t0));
                    ui_set(FACULTY175_UI_ERROR, "voice fail");
                    vTaskDelay(pdMS_TO_TICKS(1200));
                    ui_set(FACULTY175_UI_LISTEN, NULL);
                    FACULTY175_LOG_STAGE(TAG, "listen", "ready");
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
    int16_t frame[FACULTY175_LISTEN_FRAME_SAMPLES];
    bool was_capturing = false;
    uint32_t capture_clear_ms = 0;
    while (true) {
        if (faculty175_ota_active() || faculty175_qa_audio_busy() ||
            s_ui == FACULTY175_UI_SPEAK || s_ui == FACULTY175_UI_THINK) {
            faculty175_listen_reset(&s_listen);
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
        if (faculty175_audio_read(frame, FACULTY175_LISTEN_FRAME_SAMPLES, &got, 100) != ESP_OK || got == 0) {
            vTaskDelay(1);
            continue;
        }

        (void)faculty175_listen_push_frame(&s_listen, frame, got);

        const bool capturing = s_listen.speech_active;
        const uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        if ((s_ui == FACULTY175_UI_LISTEN || s_ui == FACULTY175_UI_CAPTURE) && capturing && !was_capturing) {
            FACULTY175_LOG_STAGE(TAG, "capture", "speech detected — hearing");
            ui_set(FACULTY175_UI_CAPTURE, NULL);
            was_capturing = true;
            capture_clear_ms = 0;
        } else if ((s_ui == FACULTY175_UI_LISTEN || s_ui == FACULTY175_UI_CAPTURE) && !capturing && was_capturing) {
            if (capture_clear_ms == 0) {
                capture_clear_ms = now_ms;
            } else if (now_ms - capture_clear_ms >= 400) {
                FACULTY175_LOG_STAGE(TAG, "capture", "silence — back to listening");
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
        FACULTY175_LOG_STAGE(TAG, "wifi", "STA start — connecting to %s", MYNAH_WIFI_SSID);
        esp_wifi_connect();
    } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *disc = (const wifi_event_sta_disconnected_t *)data;
        const int reason = disc != NULL ? (int)disc->reason : -1;
        FACULTY175_LOG_STAGE_W(TAG, "wifi", "disconnected reason=%d — retrying", reason);
        xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT);
        esp_wifi_connect();
    } else if (id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)data;
        if (event != NULL) {
            FACULTY175_LOG_STAGE(TAG, "wifi", "connected ip=" IPSTR " gw=" IPSTR, IP2STR(&event->ip_info.ip),
                           IP2STR(&event->ip_info.gw));
        } else {
            FACULTY175_LOG_STAGE(TAG, "wifi", "connected (got IP)");
        }
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
    }
}

static void faculty_log_ready(void)
{
    wifi_ap_record_t ap = {};
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        FACULTY175_LOG_STAGE(TAG, "ready", "wifi ok rssi=%d ch=%u", (int)ap.rssi, (unsigned)ap.primary);
    } else {
        FACULTY175_LOG_STAGE(TAG, "ready", "wifi ok");
    }
    FACULTY175_LOG_STAGE(TAG, "ready", "faculty %s (%s)", s_faculty_name, s_faculty_slug);
    FACULTY175_LOG_STAGE(TAG, "ready", "pipeline %s/functions/v1/voice-pipeline face=%s", MYNAH_SUPABASE_URL,
                   ASTROLABE_FACULTY_FACE_NAME);
    FACULTY175_LOG_STAGE(TAG, "ready", "always-on VAD → voice-stream STT (face=%s)", ASTROLABE_FACULTY_FACE_NAME);
    FACULTY175_LOG_STAGE(TAG, "ready", "serial: help | qa audio | ota status | screen.bmp");
    FACULTY175_LOG_STAGE(TAG, "ready", "monitor: ./scripts/faculty175_build.sh -p PORT monitor");
}

static esp_err_t wifi_start(void)
{
    if (strlen(MYNAH_WIFI_SSID) == 0) {
        FACULTY175_LOG_STAGE_W(TAG, "wifi", "MYNAH_WIFI_SSID empty — set include/secrets.local.h");
        return ESP_ERR_INVALID_STATE;
    }
    if (strlen(MYNAH_SUPABASE_URL) == 0 || strlen(MYNAH_SUPABASE_ANON_KEY) == 0) {
        FACULTY175_LOG_STAGE_W(TAG, "wifi", "Supabase secrets missing — voice pipeline will fail");
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
    faculty175_strlcpy((char *)wifi.sta.ssid, MYNAH_WIFI_SSID, sizeof(wifi.sta.ssid));
    faculty175_strlcpy((char *)wifi.sta.password, MYNAH_WIFI_PASSWORD, sizeof(wifi.sta.password));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi));
    ESP_ERROR_CHECK(esp_wifi_start());

    ui_set(FACULTY175_UI_WIFI, MYNAH_WIFI_SSID);
    FACULTY175_LOG_STAGE(TAG, "wifi", "waiting for IP (20s)…");
    const EventBits_t bits = xEventGroupWaitBits(s_wifi_events, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE,
                                                 pdMS_TO_TICKS(20000));
    if (bits & WIFI_CONNECTED_BIT) {
        return ESP_OK;
    }
    FACULTY175_LOG_STAGE_E(TAG, "wifi", "connect timeout");
    return ESP_FAIL;
}

static void load_faculty_from_nvs(void)
{
    faculty175_faculty_roster_ensure_default();

    nvs_handle_t nvs;
    bool has_slug = false;
    if (nvs_open("faculty", NVS_READONLY, &nvs) == ESP_OK) {
        size_t len = sizeof(s_faculty_slug);
        if (nvs_get_str(nvs, "faculty_slug", s_faculty_slug, &len) == ESP_OK && s_faculty_slug[0] != '\0') {
            has_slug = true;
        }
        len = sizeof(s_faculty_name);
        if (nvs_get_str(nvs, "faculty_name", s_faculty_name, &len) != ESP_OK || s_faculty_name[0] == '\0') {
            faculty175_strlcpy(s_faculty_name, ASTROLABE_FACULTY_DEFAULT_FACULTY_NAME, sizeof(s_faculty_name));
        }
        len = sizeof(s_history);
        nvs_get_str(nvs, "history", s_history, &len);
        nvs_close(nvs);
    }

    if (!has_slug || strcmp(s_faculty_slug, "a.einstein") == 0) {
        faculty175_faculty_roster_entry_t active = {};
        if (faculty175_faculty_roster_active(&active)) {
            faculty175_strlcpy(s_faculty_slug, active.slug, sizeof(s_faculty_slug));
            faculty175_strlcpy(s_faculty_name, active.name, sizeof(s_faculty_name));
        } else {
            faculty175_strlcpy(s_faculty_slug, ASTROLABE_FACULTY_DEFAULT_FACULTY_SLUG, sizeof(s_faculty_slug));
            faculty175_strlcpy(s_faculty_name, ASTROLABE_FACULTY_DEFAULT_FACULTY_NAME, sizeof(s_faculty_name));
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
    FACULTY175_LOG_STAGE(TAG, "boot", "Astrolabe Faculty — Waveshare ESP32-S3 Touch AMOLED 1.75C");

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(faculty175_device_auth_init());
    faculty175_ota_init();
    load_faculty_from_nvs();
    FACULTY175_LOG_STAGE(TAG, "boot", "faculty %s (%s)", s_faculty_name, s_faculty_slug);

    ESP_ERROR_CHECK(faculty175_board_init());
    (void)faculty175_touch_init();
    faculty175_gesture_start_task();
    faculty175_serial_init();
    FACULTY175_LOG_STAGE(TAG, "boot", "board audio=%s", faculty175_board_audio_ready() ? "ok" : "off");
    ESP_ERROR_CHECK(faculty175_faculty_init());
    faculty175_faculty_set_ui_notify(bust_ui_refresh);
    s_ui_queue = xQueueCreate(1, sizeof(faculty175_ui_msg_t));
    xTaskCreate(ui_task, "ui", 8192, NULL, 4, NULL);
    ui_set(FACULTY175_UI_BOOT, ASTROLABE_FACULTY_OTA_CHANNEL);
    faculty175_faculty_request_bust(s_faculty_slug);

    if (wifi_start() != ESP_OK) {
        ui_set(FACULTY175_UI_ERROR, "wifi");
        return;
    }
    faculty175_ota_maybe_start_recovery_request();
    faculty175_ota_start_auto_update_task();

    faculty175_faculty_request_bust(s_faculty_slug);
    faculty175_faculty_prefetch_roster();
    FACULTY175_LOG_STAGE(TAG, "faculty", "bust preload %s", s_faculty_slug);

    ESP_ERROR_CHECK(faculty175_listen_init(&s_listen));
    s_voice_queue = xQueueCreate(VOICE_QUEUE_LEN, sizeof(voice_event_t));
    faculty175_listen_set_stream_cb(&s_listen, &(faculty175_listen_stream_cb_t){
        .on_speech_start = listen_stream_start,
        .on_frame = listen_stream_frame,
        .on_speech_end = listen_stream_end,
        .ctx = NULL,
    });
    faculty175_qa_bind(&(faculty175_qa_bind_t){
        .listen = &s_listen,
        .ui = &s_ui,
        .faculty_slug = s_faculty_slug,
        .faculty_name = s_faculty_name,
        .ui_detail = s_detail,
        .set_faculty = qa_set_faculty_active,
        .fetch_faculty = qa_fetch_faculty_active,
    });
    xTaskCreate(listen_task, "listen", 4096, NULL, 5, NULL);
    xTaskCreate(voice_worker_task, "voice", VOICE_WORKER_STACK_BYTES, NULL, 4, NULL);
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

        if (faculty175_faculty_bust_status() == FACULTY175_FACULTY_BUST_ERROR) {
            if (now_ms - last_bust_retry_ms >= 15000) {
                last_bust_retry_ms = now_ms;
                faculty175_faculty_request_bust(s_faculty_slug);
                FACULTY175_LOG_STAGE(TAG, "faculty", "bust retry %s", s_faculty_slug);
            }
        }
        if (faculty175_button_just_pressed()) {
            faculty175_faculty_roster_entry_t entry = {};
            if (faculty175_faculty_roster_cycle_next() >= 0 && faculty175_faculty_roster_active(&entry)) {
                activate_roster_entry(&entry);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
