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
#include "astrolabe_faculty175_face.h"
#include "astrolabe_time.h"
#include "faculty175_board.h"
#include "faculty175_almanac.h"
#include "faculty175_charts.h"
#include "faculty175_listen.h"
#include "faculty175_face_alethiometer.h"
#include "faculty175_face_babel.h"
#include "faculty175_face_dispatch.h"
#include "faculty175_faculty.h"
#include "faculty175_faculty_roster.h"
#include "faculty175_faces.h"
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
#define VOICE_WORKER_STACK_BYTES 32768
#define FACE_SWIPE_SAVE_IDLE_MS 1500
#define FACE_REDRAW_MS 250

static EventGroupHandle_t s_wifi_events;
static faculty175_ui_state_t s_ui = FACULTY175_UI_BOOT;
static char s_faculty_slug[64] = ASTROLABE_FACULTY_DEFAULT_FACULTY_SLUG;
static char s_faculty_name[96] = ASTROLABE_FACULTY_DEFAULT_FACULTY_NAME;
static char s_history[512];
static char s_detail[96];
static faculty175_listen_t s_listen;
static astrolabe_audio_pipeline_t *s_pipeline;
static uint32_t s_voice_turn;
static char s_voice_pipeline_url[256];
static char s_voice_stream_url[256];
static char s_voice_face[32] = ASTROLABE_FACULTY_FACE_NAME;
static char s_voice_interaction_mode[16] = "conversation";
static char s_voice_commonplace_mode[16] = "conversation";
static char s_voice_response_format[8] = "mp3";
static char s_voice_system_instruction[2048] = ASTROLABE_FACULTY_SYSTEM_INSTRUCTION;
static bool s_voice_skip_llm = false;
static bool s_voice_log_to_commonplace = true;

static const char *ALETHIOMETER_SYSTEM_INSTRUCTION =
    "You are the aleithiometer face of a tiny round astrolabe. "
    "Interpret the user's spoken question symbolically and return only strict minified JSON. "
    "Use exactly three distinct questionSymbols and one distinct answerSymbol, all integer indices from this table: "
    "0 ALPHA, 1 BEE, 2 SUN, 3 MOON, 4 HOUR, 5 KEY, 6 ANCHOR, 7 HEART, 8 CROWN, 9 SWORD, "
    "10 TREE, 11 SERPENT, 12 BRIDGE, 13 LANTERN, 14 BOOK, 15 MASK, 16 SHIP, 17 THUNDER, "
    "18 EYE, 19 CLOUD, 20 MOUNT, 21 ROAD, 22 CUP, 23 BUTTERFLY, 24 WELL, 25 MIRROR, "
    "26 SCALES, 27 FIRE, 28 FEATHER, 29 GATE, 30 STAR, 31 WHEEL, 32 HAND, 33 LYRE, "
    "34 ARROW, 35 OMEGA. "
    "The JSON schema is {\"questionSymbols\":[number,number,number],\"answerSymbol\":number,"
    "\"spoken\":\"one or two concise spoken sentences folding the symbols into the answer\"}. "
    "Do not use markdown, prose outside JSON, or symbolic names in the numeric fields.";

static void faculty_log_ready(void);
static void make_supabase_ws_url(char *out, size_t out_len, const char *base_url, const char *path);

static void save_faculty_to_nvs(void);
static void sync_voice_context(void *user);

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
    faculty175_ui_msg_t msg = {
        .state = FACULTY175_UI_BOOT,
    };
    uint32_t last_face_draw_ms = 0;
    faculty175_face_id_t last_face_id = FACULTY175_FACE_COUNT;
    while (true) {
        bool force_draw = false;
        if (xQueueReceive(s_ui_queue, &msg, pdMS_TO_TICKS(100)) == pdTRUE) {
            s_ui = msg.state;
            faculty175_strlcpy(s_detail, msg.detail, sizeof(s_detail));
            force_draw = true;
        }
        const uint32_t anim_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        uint8_t waveform[ASTROLABE_AUDIO_PIPELINE_WAVEFORM_LEN];
        uint8_t waveform_stream[ASTROLABE_AUDIO_PIPELINE_WAVEFORM_LEN];
        astrolabe_audio_pipeline_waveform_copy(s_pipeline, waveform, sizeof(waveform));
        astrolabe_audio_pipeline_waveform_stream_copy(s_pipeline, waveform_stream, sizeof(waveform_stream));
        const bool show_waveform = s_ui == FACULTY175_UI_LISTEN || s_ui == FACULTY175_UI_CAPTURE;
        faculty175_display_waveform_update(waveform, waveform_stream, sizeof(waveform), show_waveform);
        const faculty175_face_desc_t *face = faculty175_faces_current();
        if (face != NULL && face->id != last_face_id) {
            last_face_id = face->id;
            force_draw = true;
        }
        const bool babel_overlay =
            face != NULL && face->id == FACULTY175_FACE_BABEL && faculty175_face_babel_active();
        if ((s_ui == FACULTY175_UI_LISTEN || babel_overlay) && face != NULL) {
            faculty175_display_lock();
            if (force_draw || anim_ms - last_face_draw_ms >= FACE_REDRAW_MS) {
                if (faculty175_face_dispatch_draw(face->id, anim_ms)) {
                    last_face_draw_ms = anim_ms;
                    /* Face module rendered and flushed the frame. */
                } else {
                    faculty175_display_draw_status(s_ui,
                                                   s_faculty_name,
                                                   s_detail,
                                                   anim_ms,
                                                   waveform,
                                                   waveform_stream,
                                                   sizeof(waveform));
                    last_face_draw_ms = anim_ms;
                }
            }
            faculty175_display_unlock();
        } else {
            faculty175_display_lock();
            faculty175_display_draw_status(s_ui,
                                           s_faculty_name,
                                           s_detail,
                                           anim_ms,
                                           waveform,
                                           waveform_stream,
                                           sizeof(waveform));
            faculty175_display_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(20));
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

static esp_err_t pipeline_read(int16_t *samples, size_t sample_count, size_t *out_read, uint32_t timeout_ms, void *user)
{
    (void)user;
    if (faculty175_ota_active() || faculty175_qa_audio_busy() || s_ui == FACULTY175_UI_THINK ||
        s_ui == FACULTY175_UI_SPEAK) {
        if (out_read != NULL) {
            *out_read = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(40));
        return ESP_ERR_TIMEOUT;
    }
    if (!faculty175_board_audio_ready()) {
        if (out_read != NULL) {
            *out_read = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
        return ESP_ERR_INVALID_STATE;
    }
    return faculty175_audio_read(samples, sample_count, out_read, timeout_ms);
}

static esp_err_t pipeline_write(const int16_t *samples, size_t sample_count, uint32_t timeout_ms, void *user)
{
    (void)user;
    return faculty175_audio_write_pcm(samples, sample_count, timeout_ms);
}

static esp_err_t pipeline_set_rate(uint32_t sample_rate_hz, void *user)
{
    (void)user;
    return faculty175_audio_set_sample_rate(sample_rate_hz);
}

static void pipeline_mute(bool mute, void *user)
{
    (void)user;
    faculty175_audio_set_speaker_mute(mute);
}

static void pipeline_result(const char *transcript,
                            const char *reply,
                            const char *faculty_slug,
                            const char *faculty_name,
                            void *user)
{
    (void)user;
    bool faculty_changed = false;
    const faculty175_face_desc_t *face = faculty175_faces_current();
    if (face != NULL && face->id == FACULTY175_FACE_ALETHIOMETER &&
        faculty175_face_alethiometer_apply_reply(reply)) {
        FACULTY175_LOG_STAGE(TAG, "alethiometer", "dial reply applied");
        ui_redraw();
    }
    if (faculty175_face_babel_update_from_transcript(transcript)) {
        faculty175_face_babel_set_reply(reply);
        if (faculty175_faces_set_runtime(FACULTY175_FACE_BABEL) == ESP_OK) {
            ui_redraw();
        }
    }
    if (faculty_slug != NULL && faculty_slug[0] != '\0' && strcmp(s_faculty_slug, faculty_slug) != 0) {
        FACULTY175_LOG_STAGE(TAG, "faculty", "active %s -> %s", s_faculty_slug, faculty_slug);
        faculty175_strlcpy(s_faculty_slug, faculty_slug, sizeof(s_faculty_slug));
        faculty_changed = true;
    }
    if (faculty_name != NULL && faculty_name[0] != '\0') {
        faculty175_strlcpy(s_faculty_name, faculty_name, sizeof(s_faculty_name));
    }
    append_history(transcript, reply);
    if (faculty_changed || (faculty_slug != NULL && faculty_slug[0] != '\0') ||
        (faculty_name != NULL && faculty_name[0] != '\0')) {
        save_faculty_to_nvs();
    }
    if (faculty_changed || faculty175_faculty_bust_status() != FACULTY175_FACULTY_BUST_READY ||
        strcmp(faculty175_faculty_loaded_slug(), s_faculty_slug) != 0) {
        faculty175_faculty_request_bust(s_faculty_slug);
    }
}

static void pipeline_event(astrolabe_audio_pipeline_event_t event, const char *detail, void *user)
{
    (void)user;
    switch (event) {
        case ASTROLABE_AUDIO_PIPELINE_EVENT_LISTENING:
            ui_set(FACULTY175_UI_LISTEN, NULL);
            FACULTY175_LOG_STAGE(TAG, "listen", "ready");
            break;
        case ASTROLABE_AUDIO_PIPELINE_EVENT_CAPTURE_START:
            ui_set(FACULTY175_UI_CAPTURE, NULL);
            FACULTY175_LOG_STAGE(TAG, "capture", "speech detected — streaming to flash");
            break;
        case ASTROLABE_AUDIO_PIPELINE_EVENT_CAPTURE_QUEUED:
            s_voice_turn++;
            FACULTY175_LOG_STAGE(TAG, "capture", "utterance queued from flash (turn #%u)", (unsigned)s_voice_turn);
            break;
        case ASTROLABE_AUDIO_PIPELINE_EVENT_THINKING:
            ui_set(FACULTY175_UI_THINK, "Castalia...");
            FACULTY175_LOG_STAGE(TAG, "pipeline", "streaming flash capture to voice-stream (face=%s)",
                                  s_voice_face);
            break;
        case ASTROLABE_AUDIO_PIPELINE_EVENT_TRANSCRIPT:
            if (detail != NULL && detail[0] != '\0') {
                FACULTY175_LOG_STAGE(TAG, "stt", "%.80s", detail);
                if (faculty175_face_babel_update_from_transcript(detail)) {
                    if (faculty175_faces_set_runtime(FACULTY175_FACE_BABEL) == ESP_OK) {
                        ui_redraw();
                    }
                    FACULTY175_LOG_STAGE(TAG, "babel", "translate intent %.80s", detail);
                }
            }
            break;
        case ASTROLABE_AUDIO_PIPELINE_EVENT_REPLY:
            if (detail != NULL && detail[0] != '\0') {
                FACULTY175_LOG_STAGE(TAG, "reply", "%.80s", detail);
                faculty175_face_babel_set_reply(detail);
                if (faculty175_face_babel_active()) {
                    ui_redraw();
                }
            }
            break;
        case ASTROLABE_AUDIO_PIPELINE_EVENT_SPEAKING:
            ui_set(FACULTY175_UI_SPEAK, detail != NULL && detail[0] != '\0' ? detail : s_faculty_name);
            FACULTY175_LOG_STAGE(TAG, "speak", "%s", detail != NULL && detail[0] != '\0' ? detail : s_faculty_name);
            break;
        case ASTROLABE_AUDIO_PIPELINE_EVENT_TURN_DONE:
            ui_set(FACULTY175_UI_LISTEN, NULL);
            FACULTY175_LOG_STAGE(TAG, "turn", "done #%u", (unsigned)s_voice_turn);
            break;
        case ASTROLABE_AUDIO_PIPELINE_EVENT_ERROR:
            FACULTY175_LOG_STAGE_E(TAG, "pipeline", "%s", detail != NULL ? detail : "error");
            ui_set(FACULTY175_UI_ERROR, "voice fail");
            break;
    }
}

static void pipeline_start_task(void *arg)
{
    (void)arg;
    const esp_err_t err = astrolabe_audio_pipeline_start(s_pipeline);
    if (err != ESP_OK) {
        FACULTY175_LOG_STAGE_E(TAG, "pipeline", "start failed: %s", esp_err_to_name(err));
        ui_set(FACULTY175_UI_ERROR, "voice fail");
    } else {
        ui_set(FACULTY175_UI_LISTEN, NULL);
        faculty_log_ready();
    }
    vTaskDelete(NULL);
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
        (void)astrolabe_time_start(NULL);
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
    char utc[32] = {};
    char local[32] = {};
    (void)astrolabe_time_format_utc(utc, sizeof(utc));
    (void)astrolabe_time_format_local(local, sizeof(local));
    FACULTY175_LOG_STAGE(TAG, "ready", "time %s utc=%s local=%s tz=%s",
                         astrolabe_time_valid() ? "ok" : "stale",
                         utc[0] != '\0' ? utc : "-",
                         local[0] != '\0' ? local : "-",
                         astrolabe_time_timezone());
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

static void sync_voice_context(void *user)
{
    (void)user;
    const faculty175_face_desc_t *face = faculty175_faces_current();
    const bool notes_mode = face != NULL && face->id == FACULTY175_FACE_NOTES;
    const bool alethiometer_mode = face != NULL && face->id == FACULTY175_FACE_ALETHIOMETER;
    faculty175_strlcpy(s_voice_face,
                       notes_mode ? "notes" : (alethiometer_mode ? "alethiometer" : ASTROLABE_FACULTY_FACE_NAME),
                       sizeof(s_voice_face));
    faculty175_strlcpy(s_voice_interaction_mode, notes_mode ? "journal" : "conversation",
                       sizeof(s_voice_interaction_mode));
    faculty175_strlcpy(s_voice_commonplace_mode, notes_mode ? "journal" : "conversation",
                       sizeof(s_voice_commonplace_mode));
    faculty175_strlcpy(s_voice_response_format, (notes_mode || alethiometer_mode) ? "json" : "mp3",
                       sizeof(s_voice_response_format));
    faculty175_strlcpy(s_voice_system_instruction,
                       alethiometer_mode ? ALETHIOMETER_SYSTEM_INSTRUCTION : ASTROLABE_FACULTY_SYSTEM_INSTRUCTION,
                       sizeof(s_voice_system_instruction));
    s_voice_skip_llm = notes_mode;
    s_voice_log_to_commonplace = true;
}

static uint32_t primary_face_category(uint32_t categories)
{
    static const uint32_t order[] = {
        FACULTY175_FACE_CAT_HOME,
        FACULTY175_FACE_CAT_COMMONPLACE,
        FACULTY175_FACE_CAT_ORACLE,
        FACULTY175_FACE_CAT_INSTRUMENT,
        FACULTY175_FACE_CAT_SYSTEM,
    };
    for (size_t i = 0; i < sizeof(order) / sizeof(order[0]); ++i) {
        if ((categories & order[i]) != 0) {
            return order[i];
        }
    }
    return 0;
}

static const faculty175_face_desc_t *nav_category_delta(int delta)
{
    size_t index = 0;
    size_t count = 0;
    if (!faculty175_faces_nav_position(&index, &count) || count <= 1) {
        return NULL;
    }
    const faculty175_face_desc_t *current = faculty175_faces_current();
    const uint32_t current_cat = current != NULL ? primary_face_category(current->categories) : 0;
    for (size_t step = 1; step <= count; ++step) {
        const size_t next_index = delta >= 0 ? (index + step) % count : (index + count - (step % count)) % count;
        const faculty175_face_desc_t *candidate = faculty175_faces_nav_at(next_index);
        if (candidate != NULL && primary_face_category(candidate->categories) != current_cat) {
            return candidate;
        }
    }
    return NULL;
}

static void nav_select_face(const faculty175_face_desc_t *face,
                            uint32_t now_ms,
                            uint32_t *last_nav_ms,
                            bool *save_pending)
{
    if (face == NULL || faculty175_faces_set_runtime(face->id) != ESP_OK) {
        return;
    }
    if (last_nav_ms != NULL) {
        *last_nav_ms = now_ms;
    }
    if (save_pending != NULL) {
        *save_pending = true;
    }
    if (!face->ported) {
        ui_set(FACULTY175_UI_ERROR, face->slug);
        vTaskDelay(pdMS_TO_TICKS(500));
        ui_set(FACULTY175_UI_LISTEN, NULL);
    } else {
        ui_redraw();
    }
}

void app_main(void)
{
    FACULTY175_LOG_STAGE(TAG, "boot", "Astrolabe Faculty — Waveshare ESP32-S3 Touch AMOLED 1.75C");

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(faculty175_device_auth_init());
    faculty175_ota_init();
    ESP_ERROR_CHECK(faculty175_almanac_init());
    faculty175_ota_maybe_boot_product();
    ESP_ERROR_CHECK(faculty175_faces_init());
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
    faculty175_almanac_start_auto_fetch_task();

    faculty175_faculty_request_bust(s_faculty_slug);
    faculty175_faculty_prefetch_roster();
    FACULTY175_LOG_STAGE(TAG, "faculty", "bust preload %s", s_faculty_slug);

    ESP_ERROR_CHECK(faculty175_listen_init(&s_listen));
    snprintf(s_voice_pipeline_url, sizeof(s_voice_pipeline_url), "%s/functions/v1/voice-pipeline", MYNAH_SUPABASE_URL);
    make_supabase_ws_url(s_voice_stream_url, sizeof(s_voice_stream_url), MYNAH_SUPABASE_URL,
                         "/functions/v1/voice-stream");
    sync_voice_context(NULL);
    astrolabe_audio_pipeline_config_t pipeline_cfg = {
        .io = {
            .read = pipeline_read,
            .write = pipeline_write,
            .set_rate = pipeline_set_rate,
            .mute = pipeline_mute,
        },
        .on_event = pipeline_event,
        .on_result = pipeline_result,
        .prepare_context = sync_voice_context,
        .endpoint_url = s_voice_pipeline_url,
        .stream_url = s_voice_stream_url,
        .transport = ASTROLABE_AUDIO_PIPELINE_TRANSPORT_FLASH_POST,
        .api_key = MYNAH_SUPABASE_ANON_KEY,
        .face = s_voice_face,
        .faculty_slug = s_faculty_slug,
        .faculty_name = s_faculty_name,
        .system_instruction = s_voice_system_instruction,
        .history = s_history,
        .interaction_mode = s_voice_interaction_mode,
        .commonplace_mode = s_voice_commonplace_mode,
        .response_format = s_voice_response_format,
        .skip_llm = s_voice_skip_llm,
        .log_to_commonplace = s_voice_log_to_commonplace,
        .capture_mount_path = "/voice",
        .capture_partition_label = "voice_spool",
        .capture_file_path = "/voice/faculty175_utterance.pcm",
        .sample_rate_hz = FACULTY175_AUDIO_RATE,
        .stt_sample_rate_hz = 8000,
        .frame_samples = 160,
        .rms_start = 750,
        .rms_end = 280,
        .start_frames = 3,
        .silence_frames = 50,
        .max_seconds = 15,
        .min_ms = 400,
        .capture_cooldown_ms = 2500,
        .capture_ring_slots = 8,
        .capture_segment_ms = 0,
        .listen_priority = 5,
        .voice_priority = 4,
        .listen_stack = 6144,
        .voice_stack = VOICE_WORKER_STACK_BYTES,
    };
    ESP_ERROR_CHECK(astrolabe_audio_pipeline_create(&pipeline_cfg, &s_pipeline));
    faculty175_qa_bind(&(faculty175_qa_bind_t){
        .listen = &s_listen,
        .ui = &s_ui,
        .faculty_slug = s_faculty_slug,
        .faculty_name = s_faculty_name,
        .ui_detail = s_detail,
        .set_faculty = qa_set_faculty_active,
        .fetch_faculty = qa_fetch_faculty_active,
    });
    xTaskCreate(pipeline_start_task, "audio_pipe", 8192, NULL, 4, NULL);

    uint32_t last_bust_retry_ms = 0;
    uint32_t last_face_swipe_ms = 0;
    uint32_t last_time_retry_ms = 0;
    bool face_save_pending = false;
    bool nav_mode = false;
    faculty175_display_nav_mode_set(false);
    while (true) {
        const uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

        if (!astrolabe_time_valid() && (last_time_retry_ms == 0 || now_ms - last_time_retry_ms >= 60000)) {
            last_time_retry_ms = now_ms;
            (void)astrolabe_time_retry_if_stale();
        }

        faculty175_gesture_t gesture = {};
        if (faculty175_gesture_consume(&gesture)) {
            const faculty175_face_desc_t *active_face = faculty175_faces_current();
            if (gesture.kind == FACULTY175_GESTURE_LONG_TAP) {
                nav_mode = true;
                faculty175_display_nav_mode_set(true);
                FACULTY175_LOG_STAGE(TAG, "faces", "navigation mode on");
                ui_redraw();
            } else if (nav_mode && (gesture.kind == FACULTY175_GESTURE_TAP ||
                                    gesture.kind == FACULTY175_GESTURE_BEZEL_TAP)) {
                nav_mode = false;
                faculty175_display_nav_mode_set(false);
                if (face_save_pending) {
                    const esp_err_t err = faculty175_faces_save_current();
                    if (err != ESP_OK) {
                        FACULTY175_LOG_STAGE(TAG, "faces", "save current failed %s", esp_err_to_name(err));
                    }
                    face_save_pending = false;
                }
                FACULTY175_LOG_STAGE(TAG, "faces", "navigation mode off");
                ui_redraw();
            } else if (nav_mode && faculty175_faces_enabled_count() > 1) {
                if (gesture.kind == FACULTY175_GESTURE_BEZEL_ROTATE_CW ||
                           gesture.kind == FACULTY175_GESTURE_BEZEL_ROTATE_CCW ||
                           gesture.kind == FACULTY175_GESTURE_SWIPE_LEFT ||
                           gesture.kind == FACULTY175_GESTURE_SWIPE_RIGHT) {
                    const int delta = (gesture.kind == FACULTY175_GESTURE_BEZEL_ROTATE_CW ||
                                       gesture.kind == FACULTY175_GESTURE_SWIPE_LEFT) ? 1 : -1;
                    const faculty175_face_desc_t *face = faculty175_faces_cycle_runtime(delta);
                    FACULTY175_LOG_STAGE(TAG, "faces", "nav %s -> %s", delta > 0 ? "next" : "prev",
                                         face != NULL ? face->slug : "-");
                    nav_select_face(face, now_ms, &last_face_swipe_ms, &face_save_pending);
                } else if (gesture.kind == FACULTY175_GESTURE_SWIPE_UP ||
                           gesture.kind == FACULTY175_GESTURE_SWIPE_DOWN) {
                    const int delta = gesture.kind == FACULTY175_GESTURE_SWIPE_DOWN ? 1 : -1;
                    const faculty175_face_desc_t *face = nav_category_delta(delta);
                    FACULTY175_LOG_STAGE(TAG, "faces", "nav category %s -> %s", delta > 0 ? "next" : "prev",
                                         face != NULL ? face->slug : "-");
                    nav_select_face(face, now_ms, &last_face_swipe_ms, &face_save_pending);
                }
            } else if (!nav_mode && active_face != NULL && active_face->id == FACULTY175_FACE_FACULTY &&
                       (gesture.kind == FACULTY175_GESTURE_SWIPE_UP ||
                        gesture.kind == FACULTY175_GESTURE_SWIPE_DOWN)) {
                FACULTY175_LOG_STAGE(TAG, "faculty", "swipe %s roster",
                                     gesture.kind == FACULTY175_GESTURE_SWIPE_UP ? "up" : "down");
                cycle_roster_by_delta(gesture.kind == FACULTY175_GESTURE_SWIPE_UP ? 1 : -1);
            } else if (!nav_mode && active_face != NULL && active_face->id == FACULTY175_FACE_SYNASTRY &&
                       (gesture.kind == FACULTY175_GESTURE_SWIPE_UP ||
                        gesture.kind == FACULTY175_GESTURE_SWIPE_DOWN)) {
                int slot = -1;
                faculty175_birth_chart_t profile = {};
                if (faculty175_charts_cycle_active(gesture.kind == FACULTY175_GESTURE_SWIPE_UP ? 1 : -1, &slot,
                                                   &profile)) {
                    FACULTY175_LOG_STAGE(TAG, "charts", "synastry swipe %s -> %s (%d)",
                                         gesture.kind == FACULTY175_GESTURE_SWIPE_UP ? "up" : "down",
                                         profile.name, slot);
                    ui_redraw();
                }
            } else if (!nav_mode && gesture.kind == FACULTY175_GESTURE_TAP) {
                const faculty175_face_desc_t *face = faculty175_faces_current();
                if (face != NULL && faculty175_face_dispatch_action(face->id, now_ms)) {
                    ui_redraw();
                } else {
                    FACULTY175_LOG_STAGE(TAG, "faces", "tap no action");
                }
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
            const faculty175_face_desc_t *face = faculty175_faces_current();
            if (!faculty175_touch_ready() && faculty175_faces_enabled_count() > 1) {
                face = faculty175_faces_cycle(1);
                FACULTY175_LOG_STAGE(TAG, "faces", "button fallback -> %s", face != NULL ? face->slug : "-");
                ui_redraw();
            } else if (face != NULL && face->id == FACULTY175_FACE_ALETHIOMETER && s_pipeline != NULL) {
                sync_voice_context(NULL);
                const esp_err_t err = astrolabe_audio_pipeline_trigger_capture(s_pipeline);
                FACULTY175_LOG_STAGE(TAG, "alethiometer", "button STT %s", esp_err_to_name(err));
                if (err == ESP_OK) {
                    ui_set(FACULTY175_UI_CAPTURE, "ask");
                }
            } else if (face != NULL && faculty175_face_dispatch_action(face->id, now_ms)) {
                ui_redraw();
            } else {
                faculty175_faculty_roster_entry_t entry = {};
                if (faculty175_faculty_roster_cycle_next() >= 0 && faculty175_faculty_roster_active(&entry)) {
                    activate_roster_entry(&entry);
                }
            }
        }

        if (face_save_pending && (now_ms - last_face_swipe_ms) >= FACE_SWIPE_SAVE_IDLE_MS) {
            const esp_err_t err = faculty175_faces_save_current();
            if (err != ESP_OK) {
                FACULTY175_LOG_STAGE(TAG, "faces", "save current failed %s", esp_err_to_name(err));
            }
            face_save_pending = false;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
