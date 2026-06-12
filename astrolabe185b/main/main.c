#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lwip/lwip_napt.h"
#include "nvs_flash.h"

#include "astrolabe_audio_pipeline.h"
#include "astrolabe_faculty175_face.h"
#include "astrolabe_time.h"
#include "faculty175_apocalypso.h"
#include "faculty175_board.h"
#include "faculty175_ble.h"
#include "faculty175_charts.h"
#include "faculty175_listen.h"
#include "faculty175_lvgl.h"
#include "faculty175_face_alethiometer.h"
#include "faculty175_face_babel.h"
#include "faculty175_face_dispatch.h"
#include "faculty175_face_incidents.h"
#include "faculty175_face_native.h"
#include "faculty175_face_runes.h"
#include "faculty175_face_wifilab.h"
#include "faculty175_face_tarot.h"
#include "faculty175_face_tarot_assets.h"
#include "faculty175_faculty.h"
#include "faculty175_faculty_roster.h"
#include "faculty175_faces.h"
#include "faculty175_face_profile.h"
#include "faculty175_gesture.h"
#include "faculty175_touch.h"
#include "faculty175_storage.h"
#include "faculty175_usb.h"
#include "faculty175_log.h"
#include "faculty175_device_auth.h"
#include "faculty175_qa.h"
#include "faculty175_ota.h"
#include "faculty175_pmu.h"
#include "faculty175_pocketwatch.h"
#include "faculty175_quotes.h"
#include "faculty175_rocket.h"
#include "faculty175_screen_http.h"
#include "faculty175_serial.h"
#include "faculty175_util.h"
#include "faculty175_voice.h"
#include "faculty175_wifi_settings.h"
#include "faculty175_wifi_monitor.h"
#include "faculty175_wifi_lab.h"

#if __has_include("secrets.local.h")
#include "secrets.local.h"
#else
#include "secrets.example.h"
#endif

static const char *TAG = "faculty175";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define VOICE_WORKER_STACK_BYTES 7168
#define FACE_SWIPE_SAVE_IDLE_MS 1500
#define FACE_REDRAW_MS 250
#define FACE_DEATHSTAR_REDRAW_MS 125
#define FACE_POCKETWATCH_REDRAW_MS 250
#define FACE_TRON_REDRAW_MS 50
#define FACE_CAROUSEL_FRAMES 1
#define FACE_CAROUSEL_FRAME_MS 120
#define NAV_TRANSITION_MS 72
#define FACULTY175_AUDIO_PIPELINE_AUTOSTART 0
#define LISTEN_CUE_RATE_HZ 16000
#define LISTEN_CUE_CHUNK_FRAMES 256
#define LISTEN_CUE_COOLDOWN_MS 1400
#define BATTERY_DIM_IDLE_MS 8000
#define BATTERY_SLEEP_IDLE_MS 30000
#define BATTERY_MONITOR_MS 5000
#define BATTERY_STT_ARM_MS 20000
#define BUTTON_RESET_HOLD_MS 4500
#define BUTTON_REBOOT_GRACE_MS 12000
#define WIFI_CONNECT_TIMEOUT_MS 20000
#define WIFI_CANDIDATE_MAX 3

static EventGroupHandle_t s_wifi_events;
static faculty175_ui_state_t s_ui = FACULTY175_UI_LISTEN;
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
static bool s_nav_mode = false;
static volatile bool s_low_power_dimmed = false;
static volatile bool s_low_power_asleep = false;
static bool s_wifi_low_power_paused = false;
static volatile bool s_wifi_settings_ap_requested = false;
static volatile bool s_wifi_start_complete = false;
static esp_netif_t *s_wifi_setup_ap_netif;
static esp_netif_t *s_wifi_sta_netif;
static bool s_wifi_driver_started;
static bool s_wifi_auto_apsta_enabled = false;
static bool s_power_on_battery = false;
static bool s_power_have_status = false;
static volatile bool s_battery_stt_armed = false;
static volatile bool s_battery_network_active = false;
static uint32_t s_battery_stt_armed_until_ms;
static faculty175_pmu_status_t s_power_status = {
    .battery_percent = -1,
};
static uint32_t s_listen_cue_last_ms;
static volatile bool s_faculty_ready;
static volatile bool s_face_tts_busy;
static volatile bool s_qa_stt_busy;
static char s_wifi_ssid[FACULTY175_WIFI_SSID_MAX + 1];

typedef struct {
    char ssid[FACULTY175_WIFI_SSID_MAX + 1];
    char pass[FACULTY175_WIFI_PASS_MAX + 1];
    const char *source;
} wifi_candidate_t;

static const char *ALETHIOMETER_SYSTEM_INSTRUCTION =
    "You are the aleithiometer face of a tiny round astrolabe. "
    "First use the user's audio transcript as the question. Then interpret that question symbolically and return only strict minified JSON. "
    "Use exactly three distinct questionSymbols and one distinct answerSymbol, all integer indices from this table: "
    "0 RIDER, 1 CLOVER, 2 SHIP, 3 HOUSE, 4 TREE, 5 CLOUDS, 6 SNAKE, 7 COFFIN, 8 BOUQUET, "
    "9 SCYTHE, 10 WHIP, 11 BIRDS, 12 CHILD, 13 FOX, 14 BEAR, 15 STARS, 16 STORK, 17 DOG, "
    "18 TOWER, 19 GARDEN, 20 MOUNTAIN, 21 ROADS, 22 MICE, 23 HEART, 24 RING, 25 BOOK, "
    "26 LETTER, 27 MAN, 28 WOMAN, 29 LILY, 30 SUN, 31 MOON, 32 KEY, 33 FISH, 34 ANCHOR, 35 CROSS. "
    "The JSON schema is {\"questionSymbols\":[number,number,number],\"answerSymbol\":number,"
    "\"spoken\":\"one or two concise spoken sentences interpreting the chosen symbols as an answer to the user's exact question\"}. "
    "Do not use markdown, prose outside JSON, or symbolic names in the numeric fields.";

static bool ui_state_modal(faculty175_ui_state_t state)
{
    return state == FACULTY175_UI_CAPTURE || state == FACULTY175_UI_THINK ||
           state == FACULTY175_UI_SPEAK || state == FACULTY175_UI_ERROR;
}

static bool draw_face_or_status(const faculty175_face_desc_t *face,
                                faculty175_ui_state_t state,
                                const char *detail,
                                uint32_t anim_ms,
                                const uint8_t *waveform,
                                const uint8_t *waveform_stream,
                                size_t waveform_len,
                                bool force_face)
{
    const bool babel_overlay = face != NULL && face->id == FACULTY175_FACE_BABEL && faculty175_face_babel_active();
    const bool selected_face = face != NULL && face->id != FACULTY175_FACE_FACULTY;
    const bool draw_face = face != NULL &&
                           (force_face || babel_overlay || (!ui_state_modal(state) && selected_face) ||
                            state == FACULTY175_UI_LISTEN);

    if (draw_face && faculty175_lvgl_face_supported(face->id) &&
        !ui_state_modal(state) && faculty175_lvgl_draw_face(face->id, anim_ms)) {
        return true;
    }

    if (draw_face && faculty175_face_dispatch_draw(face->id, anim_ms)) {
        return true;
    }

    faculty175_display_draw_status(state,
                                   s_faculty_name,
                                   detail,
                                   anim_ms,
                                   waveform,
                                   waveform_stream,
                                   waveform_len);
    return false;
}

static void faculty_log_ready(void);
static void make_supabase_ws_url(char *out, size_t out_len, const char *base_url, const char *path);
static bool start_face_tts_read(const faculty175_face_desc_t *face);
static esp_err_t face_tts_stream_post(const char *prompt,
                                      const char *system,
                                      const char *post_face,
                                      faculty175_voice_result_t *result);

static void save_faculty_to_nvs(void);
static void sync_voice_context(void *user);
static esp_err_t qa_trigger_stt(uint32_t capture_ms);

static void ui_set(faculty175_ui_state_t state, const char *detail);
static uint32_t draw_nav_preview(uint32_t anim_ms);
static uint16_t *alloc_carousel_frame(size_t pixel_count);
static bool animate_nav_preview_native(bool vertical, int delta, uint32_t duration_ms);
static void low_power_note_activity(uint32_t now_ms, const char *reason);
static void low_power_tick(uint32_t now_ms);
static void battery_arm_button_stt(uint32_t now_ms);

#define FACULTY175_USB_OTA_DEMO_BOOT ASTROLABE_USB_OTA_DEMO_BOOT

static void faculty175_usb_ota_demo_boot(void)
{
    FACULTY175_LOG_STAGE(TAG, "demo", "USB OTA demo boot");
    faculty175_ota_init();
    ESP_ERROR_CHECK(faculty175_storage_init());
    ESP_ERROR_CHECK(faculty175_usb_init());
    faculty175_serial_init();
    FACULTY175_LOG_STAGE(TAG, "demo", "CDC + MSC ready; use `ota usb` after copying firmware");
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static int16_t clamp_i16(int32_t v)
{
    if (v > 32767) {
        return 32767;
    }
    if (v < -32768) {
        return -32768;
    }
    return (int16_t)v;
}

static uint32_t isqrt_u64_local(uint64_t value)
{
    uint64_t bit = 1ULL << 62;
    while (bit > value) {
        bit >>= 2;
    }
    uint64_t result = 0;
    while (bit != 0) {
        if (value >= result + bit) {
            value -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }
    return (uint32_t)result;
}

static esp_err_t play_listen_cue(void)
{
    if (!faculty175_board_audio_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    const uint32_t now_ms = faculty175_log_ms();
    if (s_listen_cue_last_ms != 0 && now_ms - s_listen_cue_last_ms < LISTEN_CUE_COOLDOWN_MS) {
        return ESP_OK;
    }
    s_listen_cue_last_ms = now_ms;

    esp_err_t err = faculty175_audio_set_sample_rate(LISTEN_CUE_RATE_HZ);
    if (err != ESP_OK) {
        return err;
    }
    faculty175_audio_set_speaker_mute(false);

    int16_t pcm[LISTEN_CUE_CHUNK_FRAMES * 2];
    uint32_t phase_a = 0;
    uint32_t phase_b = 0;
    uint32_t phase_c = 0;
    const uint32_t step_a = (523u * 65536u) / LISTEN_CUE_RATE_HZ;
    const uint32_t step_b = (659u * 65536u) / LISTEN_CUE_RATE_HZ;
    const uint32_t step_c = (784u * 65536u) / LISTEN_CUE_RATE_HZ;
    const int total_frames = LISTEN_CUE_RATE_HZ * 340 / 1000;

    for (int base = 0; base < total_frames; base += LISTEN_CUE_CHUNK_FRAMES) {
        const int frames =
            (total_frames - base) < LISTEN_CUE_CHUNK_FRAMES ? (total_frames - base) : LISTEN_CUE_CHUNK_FRAMES;
        for (int i = 0; i < frames; ++i) {
            const int t = base + i;
            int amp = 3600;
            if (t < 480) {
                amp = amp * t / 480;
            } else if (t > total_frames - 960) {
                amp = amp * (total_frames - t) / 960;
            }
            const int32_t s_a = (phase_a & 0x8000u) ? -amp : amp;
            const int32_t s_b = (phase_b & 0x8000u) ? -(amp / 2) : (amp / 2);
            const int32_t s_c = (phase_c & 0x8000u) ? -(amp / 3) : (amp / 3);
            const int32_t wobble = (t % 640) < 320 ? amp / 5 : -(amp / 5);
            const int16_t sample = clamp_i16(s_a + s_b + s_c + wobble);
            pcm[i * 2] = sample;
            pcm[i * 2 + 1] = sample;
            phase_a += step_a + (uint32_t)(t / 180);
            phase_b += step_b;
            phase_c += step_c - (uint32_t)(t / 220);
        }
        err = faculty175_audio_write_pcm(pcm, (size_t)frames * 2u, 250);
        if (err != ESP_OK) {
            break;
        }
        vTaskDelay(1);
    }

    ESP_ERROR_CHECK_WITHOUT_ABORT(faculty175_audio_set_sample_rate(FACULTY175_AUDIO_RATE));
    FACULTY175_LOG_STAGE(TAG, "cue", "listen cue %s", esp_err_to_name(err));
    return err;
}

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

static bool cycle_roster_by_delta(int delta)
{
    faculty175_faculty_roster_entry_t entry = {};
    if (faculty175_faculty_roster_cycle_delta(delta) >= 0 && faculty175_faculty_roster_active(&entry)) {
        activate_roster_entry(&entry);
        return true;
    }
    return false;
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

static void low_power_wifi_pause(void)
{
    if (s_wifi_low_power_paused) {
        return;
    }
    s_wifi_low_power_paused = true;
    faculty175_screen_http_stop();
    (void)esp_wifi_disconnect();
    (void)esp_wifi_stop();
    FACULTY175_LOG_STAGE(TAG, "power", "wifi paused for battery sleep");
}

static void low_power_wifi_resume(void)
{
    if (!s_wifi_low_power_paused) {
        return;
    }
    s_wifi_low_power_paused = false;
    esp_err_t err = esp_wifi_start();
    if (err == ESP_ERR_WIFI_NOT_INIT || err == ESP_ERR_WIFI_CONN) {
        FACULTY175_LOG_STAGE_W(TAG, "power", "wifi resume skipped: %s", esp_err_to_name(err));
    } else {
        FACULTY175_LOG_STAGE(TAG, "power", "wifi resume %s", esp_err_to_name(err));
    }
}

static bool low_power_wifi_allowed(void)
{
    return !s_power_on_battery || s_battery_network_active;
}

static void low_power_wifi_resume_for_voice(uint32_t wait_ms)
{
    s_battery_network_active = true;
    low_power_wifi_resume();
    if (s_wifi_events != NULL && wait_ms > 0) {
        const EventBits_t bits = xEventGroupWaitBits(s_wifi_events,
                                                     WIFI_CONNECTED_BIT,
                                                     pdFALSE,
                                                     pdFALSE,
                                                     pdMS_TO_TICKS(wait_ms));
        if ((bits & WIFI_CONNECTED_BIT) == 0) {
            FACULTY175_LOG_STAGE_W(TAG, "power", "wifi not connected after %u ms voice wait", (unsigned)wait_ms);
        }
    }
}

static bool low_power_on_battery(const faculty175_pmu_status_t *st)
{
    return st != NULL && st->present && st->battery_present && !st->vbus_in && !st->charging;
}

static void low_power_apply_awake(uint32_t now_ms, const char *reason)
{
    const bool was_low_power = s_low_power_asleep || s_low_power_dimmed;
    s_low_power_asleep = false;
    s_low_power_dimmed = false;
    faculty175_display_flush_suspended_set(false);
    faculty175_board_display_on(true);
    faculty175_board_set_backlight(100);
    faculty175_audio_set_speaker_mute(false);
    if (low_power_wifi_allowed()) {
        low_power_wifi_resume();
    }
    if (was_low_power) {
        FACULTY175_LOG_STAGE(TAG, "power", "wake %s", reason != NULL ? reason : "activity");
        ui_redraw();
    }
    (void)now_ms;
}

static uint32_t s_low_power_last_activity_ms;

static void low_power_note_activity(uint32_t now_ms, const char *reason)
{
    s_low_power_last_activity_ms = now_ms;
    if (s_low_power_asleep || s_low_power_dimmed) {
        low_power_apply_awake(now_ms, reason);
    }
}

static void low_power_tick(uint32_t now_ms)
{
    static uint32_t last_monitor_ms;
    static bool last_on_battery;
    static bool last_have_status;
    static int last_pct = -2;
    static uint16_t last_mv;

    if (last_monitor_ms == 0 || now_ms - last_monitor_ms >= BATTERY_MONITOR_MS) {
        last_monitor_ms = now_ms;
        faculty175_pmu_status_t st = {
            .battery_percent = -1,
        };
        s_power_have_status = faculty175_pmu_status(&st);
        s_power_status = st;
        s_power_on_battery = low_power_on_battery(&st);
        if (s_power_have_status &&
            (s_power_on_battery != last_on_battery || !last_have_status ||
             st.battery_percent != last_pct || st.battery_mv != last_mv)) {
            FACULTY175_LOG_STAGE(TAG,
                                 "power",
                                 "%s batt=%d%% %umV vbus=%d charging=%d discharge=%d",
                                 s_power_on_battery ? "battery" : "usb",
                                 st.battery_percent,
                                 (unsigned)st.battery_mv,
                                 st.vbus_in ? 1 : 0,
                                 st.charging ? 1 : 0,
                                 st.discharging ? 1 : 0);
            last_on_battery = s_power_on_battery;
            last_have_status = true;
            last_pct = st.battery_percent;
            last_mv = st.battery_mv;
        } else if (!s_power_have_status && last_have_status) {
            FACULTY175_LOG_STAGE_W(TAG, "power", "PMU unavailable");
            last_have_status = false;
        }
    }

    if (s_battery_stt_armed && (int32_t)(now_ms - s_battery_stt_armed_until_ms) >= 0) {
        s_battery_stt_armed = false;
        FACULTY175_LOG_STAGE(TAG, "power", "battery button STT arm expired");
    }

    if (!s_power_on_battery) {
        if (s_low_power_asleep || s_low_power_dimmed) {
            low_power_apply_awake(now_ms, "usb");
        } else {
            low_power_wifi_resume();
        }
        return;
    }
    if (!low_power_wifi_allowed()) {
        low_power_wifi_pause();
    }
    if (ui_state_modal(s_ui) || faculty175_ota_active() || faculty175_qa_audio_busy()) {
        return;
    }

    const uint32_t idle_ms = now_ms - s_low_power_last_activity_ms;
    if (!s_low_power_asleep && idle_ms >= BATTERY_SLEEP_IDLE_MS) {
        s_low_power_asleep = true;
        s_low_power_dimmed = true;
        faculty175_audio_set_speaker_mute(true);
        faculty175_board_set_backlight(0);
        faculty175_board_display_on(false);
        faculty175_display_flush_suspended_set(true);
        low_power_wifi_pause();
        FACULTY175_LOG_STAGE(TAG, "power", "battery sleep after %u ms idle", (unsigned)idle_ms);
        return;
    }
    if (!s_low_power_dimmed && idle_ms >= BATTERY_DIM_IDLE_MS) {
        s_low_power_dimmed = true;
        faculty175_board_set_backlight(10);
        FACULTY175_LOG_STAGE(TAG, "power", "battery dim after %u ms idle", (unsigned)idle_ms);
    }
}

static void battery_arm_button_stt(uint32_t now_ms)
{
    s_battery_stt_armed = true;
    s_battery_stt_armed_until_ms = now_ms + BATTERY_STT_ARM_MS;
    s_battery_network_active = false;
    FACULTY175_LOG_STAGE(TAG, "power", "battery STT armed by button");
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
        .state = FACULTY175_UI_LISTEN,
    };
    uint32_t last_face_draw_ms = 0;
    time_t last_pocketwatch_second = (time_t)-1;
    faculty175_face_id_t last_face_id = FACULTY175_FACE_COUNT;
    while (true) {
        bool force_draw = false;
        if (xQueueReceive(s_ui_queue, &msg, pdMS_TO_TICKS(100)) == pdTRUE) {
            s_ui = msg.state;
            faculty175_strlcpy(s_detail, msg.detail, sizeof(s_detail));
            force_draw = true;
        }
        const uint32_t anim_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        if (s_low_power_asleep) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        uint8_t waveform[ASTROLABE_AUDIO_PIPELINE_WAVEFORM_LEN];
        uint8_t waveform_stream[ASTROLABE_AUDIO_PIPELINE_WAVEFORM_LEN];
        if (s_pipeline != NULL) {
            astrolabe_audio_pipeline_waveform_copy(s_pipeline, waveform, sizeof(waveform));
            astrolabe_audio_pipeline_waveform_stream_copy(s_pipeline, waveform_stream, sizeof(waveform_stream));
        } else {
            memset(waveform, 0, sizeof(waveform));
            memset(waveform_stream, 0, sizeof(waveform_stream));
        }
        const bool show_waveform = s_ui == FACULTY175_UI_LISTEN || s_ui == FACULTY175_UI_CAPTURE;
        faculty175_display_waveform_update(waveform, waveform_stream, sizeof(waveform), show_waveform);
        const faculty175_face_desc_t *face = faculty175_faces_current();
        bool face_changed = false;
        if (face != NULL && face->id != last_face_id) {
            if (faculty175_wifi_lab_is_face(last_face_id)) {
                faculty175_face_wifilab_leave(last_face_id);
            }
            if (face != NULL && faculty175_wifi_lab_is_face(face->id)) {
                faculty175_face_wifilab_enter(face->id);
            }
            last_face_id = face->id;
            last_pocketwatch_second = (time_t)-1;
            force_draw = true;
            face_changed = true;
        }
        if (face != NULL && faculty175_wifi_lab_is_face(face->id)) {
            faculty175_face_wifilab_tick(face->id, anim_ms);
        }
        if (face != NULL && face->id == FACULTY175_FACE_POCKETWATCH && astrolabe_time_valid()) {
            const time_t pocketwatch_second = astrolabe_time_now();
            if (pocketwatch_second != last_pocketwatch_second) {
                last_pocketwatch_second = pocketwatch_second;
                force_draw = true;
            }
        }
        const faculty175_touch_state_t touch = faculty175_touch_state_get();
        if (touch.down && face != NULL && face->id == FACULTY175_FACE_POCKETWATCH && !face_changed) {
            vTaskDelay(pdMS_TO_TICKS(8));
            continue;
        }
        faculty175_display_lock();
        const uint32_t face_redraw_ms = face != NULL && face->id == FACULTY175_FACE_DEATHSTAR
                                            ? FACE_DEATHSTAR_REDRAW_MS
                                            : (face != NULL && face->id == FACULTY175_FACE_TRON
                                                   ? FACE_TRON_REDRAW_MS
                                                   : (face != NULL && face->id == FACULTY175_FACE_POCKETWATCH
                                                          ? FACE_POCKETWATCH_REDRAW_MS
                                                          : FACE_REDRAW_MS));
        if (s_nav_mode && force_draw) {
            draw_nav_preview(anim_ms);
            last_face_draw_ms = anim_ms;
        } else if (force_draw || anim_ms - last_face_draw_ms >= face_redraw_ms) {
            const size_t pixels = faculty175_display_frame_pixel_count();
            (void)pixels;
            (void)draw_face_or_status(face,
                                      s_ui,
                                      s_detail,
                                      anim_ms,
                                      waveform,
                                      waveform_stream,
                                      sizeof(waveform),
                                      false);
            last_face_draw_ms = anim_ms;
        }
        if (!s_nav_mode && !ui_state_modal(s_ui) && (face != NULL && faculty175_lvgl_face_supported(face->id))) {
            faculty175_lvgl_service(anim_ms);
        }
        faculty175_display_unlock();
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

static void prompt_append(char *out, size_t cap, size_t *off, const char *fmt, ...)
{
    if (out == NULL || cap == 0 || off == NULL || *off >= cap) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    const int wrote = vsnprintf(out + *off, cap - *off, fmt, ap);
    va_end(ap);
    if (wrote <= 0) {
        return;
    }
    const size_t add = (size_t)wrote;
    *off = add >= cap - *off ? cap - 1 : *off + add;
}

static const char *face_category_label(uint32_t categories)
{
    if ((categories & FACULTY175_FACE_CAT_HOME) != 0) {
        return "home";
    }
    if ((categories & FACULTY175_FACE_CAT_COMMONPLACE) != 0) {
        return "commonplace";
    }
    if ((categories & FACULTY175_FACE_CAT_ORACLE) != 0) {
        return "oracle";
    }
    if ((categories & FACULTY175_FACE_CAT_INSTRUMENT) != 0) {
        return "instrument";
    }
    if ((categories & FACULTY175_FACE_CAT_SYSTEM) != 0) {
        return "system";
    }
    return "uncategorized";
}

static void build_face_read_prompt(const faculty175_face_desc_t *face, char *out, size_t cap)
{
    if (out == NULL || cap == 0) {
        return;
    }
    out[0] = '\0';
    if (face == NULL) {
        faculty175_strlcpy(out, "Read the current astrolabe face aloud. No face descriptor is available.", cap);
        return;
    }

    size_t off = 0;
    char utc[32] = {};
    char local[32] = {};
    (void)astrolabe_time_format_utc(utc, sizeof(utc));
    (void)astrolabe_time_format_local(local, sizeof(local));
    prompt_append(out, cap, &off,
                  "Read the current Astrolabe watch face aloud without asking a follow-up question. "
                  "Current face: %s (%s). Category: %s. Ported to LVGL: %s. Navigation enabled: %s. ",
                  face->label != NULL ? face->label : face->slug,
                  face->slug != NULL ? face->slug : "-",
                  face_category_label(face->categories),
                  face->ported ? "yes" : "no",
                  faculty175_faces_navigation_enabled(face->id) ? "yes" : "no");
    prompt_append(out, cap, &off, "Device time is %s, UTC %s, timezone %s. ",
                  local[0] != '\0' ? local : "not synchronized",
                  utc[0] != '\0' ? utc : "not synchronized",
                  astrolabe_time_timezone());

    switch (face->id) {
        case FACULTY175_FACE_POCKETWATCH:
        case FACULTY175_FACE_CLASSIC:
        case FACULTY175_FACE_DIGITAL:
            prompt_append(out, cap, &off,
                          "The visible data is the current local time; read it plainly and mention the time-of-day tint. ");
            break;
        case FACULTY175_FACE_MOON:
            prompt_append(out, cap, &off,
                          "The visible data is a full-screen lunar texture with phase occlusion; describe the moon image and the current date context. ");
            break;
        case FACULTY175_FACE_SOLAR:
            prompt_append(out, cap, &off,
                          "The visible data is live solar activity imagery from NASA when cached; summarize the map state and say if live data appears unavailable. ");
            break;
        case FACULTY175_FACE_MAGNETOSPHERE:
            prompt_append(out, cap, &off,
                          "The visible data is a rotating Earth with magnetic field visualization based on live space-weather data when cached. ");
            break;
        case FACULTY175_FACE_TAROT: {
            const int idx = faculty175_face_tarot_current_card();
            const faculty175_tarot_card_t *card = faculty175_tarot_card_get(idx);
            if (card != NULL) {
                prompt_append(out, cap, &off, "Current tarot card: %s, keyword %s, roman %s. ",
                              card->title != NULL ? card->title : "unknown",
                              card->keyword != NULL ? card->keyword : "unknown",
                              card->roman != NULL ? card->roman : "-");
            }
            break;
        }
        case FACULTY175_FACE_TRON:
            prompt_append(out, cap, &off,
                          "The visible data is a TRON light-cycle arena controlled by watch tilt; describe the neon trails, current motion, and whether either cycle has crashed. ");
            break;
        case FACULTY175_FACE_RUNES: {
            int spread[3] = {};
            if (faculty175_face_runes_current(spread)) {
                prompt_append(out, cap, &off, "Current rune spread: ");
                for (int i = 0; i < 3; ++i) {
                    prompt_append(out, cap, &off, "%s %s (%s)%s",
                                  faculty175_face_rune_slot(i),
                                  faculty175_face_rune_name(spread[i]),
                                  faculty175_face_rune_keyword(spread[i]),
                                  i == 2 ? ". " : ", ");
                }
            }
            break;
        }
        case FACULTY175_FACE_CHAKRA:
        case FACULTY175_FACE_BOWL:
            prompt_append(out, cap, &off, "Current chakra selection: %s. ",
                          faculty175_face_native_chakra_name());
            break;
        case FACULTY175_FACE_OCARINA:
        case FACULTY175_FACE_BONGO:
        case FACULTY175_FACE_PIANO:
        case FACULTY175_FACE_KALIMBA:
        case FACULTY175_FACE_DRONE:
        case FACULTY175_FACE_CHORD:
        case FACULTY175_FACE_PANDRUM:
            prompt_append(out, cap, &off,
                          "This is a playable instrument face; mention what touch produces and keep the reading short. ");
            break;
        case FACULTY175_FACE_QUOTES: {
            faculty175_quote_t q = {};
            if (faculty175_quotes_current(&q) && q.ok) {
                prompt_append(out, cap, &off,
                              "Current quote of the day for %s: \"%s\" Author: %s. Source: %s%s%s. ",
                              q.date[0] != '\0' ? q.date : "today",
                              q.quote,
                              q.faculty_name[0] != '\0' ? q.faculty_name : q.faculty_slug,
                              q.book_title[0] != '\0' ? q.book_title : "unknown",
                              q.passage[0] != '\0' ? ", " : "",
                              q.passage[0] != '\0' ? q.passage : "");
            } else {
                prompt_append(out, cap, &off,
                              "The quote face is waiting for its flash-cached quote of the day. ");
            }
            break;
        }
        case FACULTY175_FACE_ROCKET: {
            faculty175_rocket_status_t st = {};
            if (faculty175_rocket_current(&st) && st.count > 0) {
                const faculty175_rocket_launch_t *lv = &st.launches[0];
                char countdown[32];
                char local[32];
                faculty175_rocket_format_countdown(lv->net_unix, countdown, sizeof(countdown));
                faculty175_rocket_format_local(lv->net_unix, local, sizeof(local));
                prompt_append(out, cap, &off,
                              "Read this as a launch clock. Next launch: %s %s by %s at %s from %s, %s. Countdown %s. ",
                              lv->vehicle,
                              lv->name,
                              lv->provider,
                              local,
                              lv->pad,
                              lv->location,
                              countdown);
            } else {
                prompt_append(out, cap, &off,
                              "The rocket face is waiting for upcoming launch data and a rocket or pad image. ");
            }
            break;
        }
        case FACULTY175_FACE_ALETHIOMETER:
        {
            int targets[4] = {};
            char question[192] = {};
            char spoken[384] = {};
            if (faculty175_face_alethiometer_context(targets, question, sizeof(question), spoken, sizeof(spoken))) {
                prompt_append(out, cap, &off,
                              "Read the aleithiometer's settled dial positions in relation to the user's question. "
                              "Question: %s. Question needles: %s, %s, %s. Answer needle: %s. "
                              "Prior interpretation: %s. Do not recast or choose new symbols. ",
                              question[0] != '\0' ? question : "(no spoken question has been recorded yet)",
                              faculty175_face_alethiometer_symbol_name(targets[0]),
                              faculty175_face_alethiometer_symbol_name(targets[1]),
                              faculty175_face_alethiometer_symbol_name(targets[2]),
                              faculty175_face_alethiometer_symbol_name(targets[3]),
                              spoken[0] != '\0' ? spoken : "(none yet)");
            } else {
                prompt_append(out, cap, &off,
                              "Read this as a symbolic alethiometer face. No valid dial state is available yet. ");
            }
            break;
        }
        default:
            prompt_append(out, cap, &off,
                          "Use only the supplied face name, category, and current time when live face-specific data is not available. ");
            break;
    }
    prompt_append(out, cap, &off, "Keep the spoken answer under forty words.");
}

static void face_tts_task(void *arg)
{
    const faculty175_face_id_t id = (faculty175_face_id_t)(uintptr_t)arg;
    const faculty175_face_desc_t *face = faculty175_faces_get(id);
    const char *slug = face != NULL && face->slug != NULL ? face->slug : "-";
    char *prompt = heap_caps_malloc(1536, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    faculty175_voice_result_t *result = heap_caps_calloc(1, sizeof(*result), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (prompt == NULL) {
        prompt = malloc(1536);
    }
    if (result == NULL) {
        result = calloc(1, sizeof(*result));
    }
    if (prompt == NULL || result == NULL) {
        FACULTY175_LOG_STAGE_E(TAG, "tts-face", "alloc failed %s", slug);
        printf("tts-face: done slug=%s err=ESP_ERR_NO_MEM\n", slug);
        fflush(stdout);
        free(prompt);
        free(result);
        ui_set(FACULTY175_UI_ERROR, "voice alloc");
        s_face_tts_busy = false;
        vTaskDeleteWithCaps(NULL);
        return;
    }
    build_face_read_prompt(face, prompt, 1536);

    FACULTY175_LOG_STAGE(TAG, "tts-face", "start %s", slug);
    printf("tts-face: start slug=%s\n", slug);
    fflush(stdout);

    ui_set(FACULTY175_UI_THINK, "reading face");
    const char *system =
        "You are the speaking voice of a tiny round astrolabe. Read the current face from the supplied data. "
        "Do not perform speech recognition, do not ask a question, and do not mention hidden implementation details.";
    const char *post_face = (face != NULL && face->id == FACULTY175_FACE_ALETHIOMETER)
                                ? ASTROLABE_FACULTY_FACE_NAME
                                : (face != NULL ? face->slug : ASTROLABE_FACULTY_FACE_NAME);
    ui_set(FACULTY175_UI_SPEAK, face != NULL ? face->label : "face");
    esp_err_t err = face_tts_stream_post(prompt, system, post_face, result);
    if (err == ESP_OK) {
        if (result->reply[0] != '\0') {
            FACULTY175_LOG_STAGE(TAG, "tts-face", "reply %.96s", result->reply);
            append_history(prompt, result->reply);
            save_faculty_to_nvs();
        }
    }

    FACULTY175_LOG_STAGE(TAG, "tts-face", "%s %s", slug, esp_err_to_name(err));
    printf("tts-face: done slug=%s err=%s\n", slug, esp_err_to_name(err));
    fflush(stdout);
    faculty175_voice_result_free(result);
    free(result);
    free(prompt);
    ui_set(FACULTY175_UI_LISTEN, NULL);
    s_face_tts_busy = false;
    vTaskDeleteWithCaps(NULL);
}

static esp_err_t face_tts_stream_post(const char *prompt,
                                      const char *system,
                                      const char *post_face,
                                      faculty175_voice_result_t *result)
{
    const faculty175_voice_tts_stream_t stream = {
        .spool_path = "/voice/voice-reply.mp3",
    };
    esp_err_t err = faculty175_voice_post_message_streaming(prompt,
                                                            system,
                                                            post_face,
                                                            s_faculty_slug,
                                                            s_faculty_name,
                                                            s_history,
                                                            &stream,
                                                            result);
    if (err == ESP_OK && result != NULL && result->mp3_path[0] != '\0') {
        err = faculty175_voice_play_mp3_file(result->mp3_path, result->mp3_len);
    }
    return err;
}

static bool start_face_tts_read(const faculty175_face_desc_t *face)
{
    if (face == NULL) {
        return false;
    }
    if (strlen(MYNAH_SUPABASE_URL) == 0 || strlen(MYNAH_SUPABASE_ANON_KEY) == 0) {
        FACULTY175_LOG_STAGE(TAG, "tts-face", "start %s", face->slug);
        printf("tts-face: start slug=%s\n", face->slug);
        FACULTY175_LOG_STAGE_W(TAG, "tts-face", "Supabase secrets missing");
        printf("tts-face: done slug=%s err=ESP_ERR_INVALID_STATE\n", face->slug);
        fflush(stdout);
        ui_set(FACULTY175_UI_ERROR, "voice config");
        return true;
    }
    if (s_face_tts_busy) {
        FACULTY175_LOG_STAGE_W(TAG, "tts-face", "busy; ignored %s", face->slug);
        printf("tts-face: busy slug=%s\n", face->slug);
        fflush(stdout);
        return true;
    }
    if (!faculty175_voice_heap_ready("face-tts-start")) {
        FACULTY175_LOG_STAGE_W(TAG, "tts-face", "low heap; ignored %s", face->slug);
        printf("tts-face: done slug=%s err=ESP_ERR_NO_MEM\n", face->slug);
        fflush(stdout);
        ui_set(FACULTY175_UI_ERROR, "voice heap");
        return true;
    }
    s_face_tts_busy = true;
    BaseType_t ok = xTaskCreateWithCaps(face_tts_task, "face_tts", VOICE_WORKER_STACK_BYTES,
                                        (void *)(uintptr_t)face->id, 4, NULL,
                                        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (ok != pdPASS) {
        s_face_tts_busy = false;
        FACULTY175_LOG_STAGE_E(TAG, "tts-face", "task create failed");
        printf("tts-face: done slug=%s err=ESP_ERR_NO_MEM\n", face->slug);
        fflush(stdout);
        ui_set(FACULTY175_UI_ERROR, "voice task");
        return true;
    }
    return true;
}

bool faculty175_request_current_face_tts(void)
{
    const faculty175_face_desc_t *face = faculty175_faces_current();
    const bool handled = start_face_tts_read(face);
    FACULTY175_LOG_STAGE(TAG,
                         "tts-face",
                         "serial read %s handled=%s",
                         face != NULL ? face->slug : "-",
                         handled ? "yes" : "no");
    return handled;
}

static esp_err_t pipeline_read(int16_t *samples, size_t sample_count, size_t *out_read, uint32_t timeout_ms, void *user)
{
    (void)user;
    if (faculty175_ota_active() || faculty175_qa_audio_busy() || faculty175_face_native_audio_busy() ||
        s_ui == FACULTY175_UI_THINK || s_ui == FACULTY175_UI_SPEAK || s_low_power_asleep ||
        (s_power_on_battery && !s_battery_stt_armed && !astrolabe_audio_pipeline_speech_active(s_pipeline))) {
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
        faculty175_face_alethiometer_apply_reply(transcript, reply)) {
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
            (void)play_listen_cue();
            FACULTY175_LOG_STAGE(TAG, "listen", "ready");
            break;
        case ASTROLABE_AUDIO_PIPELINE_EVENT_CAPTURE_START:
            if (s_power_on_battery) {
                s_battery_stt_armed = true;
            }
            ui_set(FACULTY175_UI_CAPTURE, NULL);
            FACULTY175_LOG_STAGE(TAG, "capture", "speech detected — streaming to flash");
            break;
        case ASTROLABE_AUDIO_PIPELINE_EVENT_CAPTURE_QUEUED:
            if (s_power_on_battery) {
                s_battery_stt_armed = false;
                low_power_wifi_resume_for_voice(0);
            }
            s_voice_turn++;
            FACULTY175_LOG_STAGE(TAG, "capture", "utterance queued from flash (turn #%u)", (unsigned)s_voice_turn);
            break;
        case ASTROLABE_AUDIO_PIPELINE_EVENT_THINKING:
            if (s_power_on_battery) {
                low_power_wifi_resume_for_voice(10000);
            }
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
            s_battery_stt_armed = false;
            s_battery_network_active = false;
            if (s_power_on_battery) {
                low_power_wifi_pause();
            }
            ui_set(FACULTY175_UI_LISTEN, NULL);
            FACULTY175_LOG_STAGE(TAG, "turn", "done #%u", (unsigned)s_voice_turn);
            break;
        case ASTROLABE_AUDIO_PIPELINE_EVENT_ERROR:
            s_battery_stt_armed = false;
            s_battery_network_active = false;
            if (s_power_on_battery) {
                low_power_wifi_pause();
            }
            FACULTY175_LOG_STAGE_E(TAG, "pipeline", "%s", detail != NULL ? detail : "error");
            ui_set(FACULTY175_UI_ERROR, "voice fail");
            break;
    }
}

typedef struct {
    uint32_t capture_ms;
} qa_stt_args_t;

static void qa_stt_frame_stats(const int16_t *samples, size_t count, int32_t *out_peak, uint32_t *out_rms)
{
    int32_t peak = 0;
    uint64_t sum_sq = 0;
    for (size_t i = 0; i < count; ++i) {
        int32_t v = samples[i];
        if (v < 0) {
            v = -v;
        }
        if (v > peak) {
            peak = v;
        }
        sum_sq += (uint64_t)((int32_t)samples[i] * (int32_t)samples[i]);
    }
    if (out_peak != NULL) {
        *out_peak = peak;
    }
    if (out_rms != NULL) {
        *out_rms = count > 0 ? isqrt_u64_local(sum_sq / count) : 0;
    }
}

static void qa_stt_task(void *arg)
{
    qa_stt_args_t *args = (qa_stt_args_t *)arg;
    uint32_t capture_ms = args != NULL ? args->capture_ms : 0;
    free(args);
    if (capture_ms == 0) {
        capture_ms = 5000;
    }
    if (capture_ms < 1000) {
        capture_ms = 1000;
    } else if (capture_ms > 15000) {
        capture_ms = 15000;
    }

    int16_t *frame = heap_caps_malloc(FACULTY175_LISTEN_FRAME_SAMPLES * sizeof(int16_t),
                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    faculty175_voice_result_t *result = heap_caps_calloc(1, sizeof(*result), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (frame == NULL) {
        frame = malloc(FACULTY175_LISTEN_FRAME_SAMPLES * sizeof(int16_t));
    }
    if (result == NULL) {
        result = calloc(1, sizeof(*result));
    }
    if (frame == NULL || result == NULL) {
        FACULTY175_LOG_STAGE_E(TAG, "stt", "qa alloc failed");
        printf("qa: stt done err=ESP_ERR_NO_MEM\n");
        fflush(stdout);
        free(frame);
        free(result);
        s_qa_stt_busy = false;
        vTaskDeleteWithCaps(NULL);
        return;
    }

    sync_voice_context(NULL);
    ui_set(FACULTY175_UI_CAPTURE, "ask");
    faculty175_audio_set_speaker_mute(true);
    const uint32_t start_ms = faculty175_log_ms();
    FACULTY175_LOG_STAGE(TAG, "stt", "qa capture begin ms=%u", (unsigned)capture_ms);
    printf("qa: stt capture begin ms=%u\n", (unsigned)capture_ms);
    fflush(stdout);

    esp_err_t err = faculty175_voice_stream_begin(s_faculty_slug, s_faculty_name, s_history);
    size_t frames = 0;
    int32_t peak_max = 0;
    uint32_t rms_max = 0;
    if (err == ESP_OK) {
        while (faculty175_log_ms() - start_ms < capture_ms) {
            size_t got = 0;
            err = faculty175_audio_read(frame, FACULTY175_LISTEN_FRAME_SAMPLES, &got, 200);
            if (err != ESP_OK || got == 0) {
                break;
            }
            int32_t peak = 0;
            uint32_t rms = 0;
            qa_stt_frame_stats(frame, got, &peak, &rms);
            if (peak > peak_max) {
                peak_max = peak;
            }
            if (rms > rms_max) {
                rms_max = rms;
            }
            err = faculty175_voice_stream_write(frame, got);
            if (err != ESP_OK) {
                break;
            }
            frames++;
            vTaskDelay(1);
        }
    }

    FACULTY175_LOG_STAGE(TAG, "stt", "qa capture frames=%u peak=%ld rms=%lu err=%s",
                         (unsigned)frames,
                         (long)peak_max,
                         (unsigned long)rms_max,
                         esp_err_to_name(err));
    printf("qa: stt capture frames=%u peak=%ld rms=%lu err=%s\n",
           (unsigned)frames,
           (long)peak_max,
           (unsigned long)rms_max,
           esp_err_to_name(err));
    fflush(stdout);

    if (err == ESP_OK) {
        ui_set(FACULTY175_UI_THINK, "Castalia...");
        const faculty175_voice_tts_stream_t tts_stream = {
            .spool_path = "/voice/voice-reply.mp3",
        };
        faculty175_audio_set_speaker_mute(false);
        err = faculty175_voice_stt_stream_commit_streaming(&tts_stream, result);
        if (err == ESP_OK && result->mp3_path[0] != '\0') {
            err = faculty175_voice_play_mp3_file(result->mp3_path, result->mp3_len);
        }
    } else {
        faculty175_voice_stream_cancel();
    }

    if (err == ESP_OK) {
        if (result->transcript[0] != '\0') {
            FACULTY175_LOG_STAGE(TAG, "stt", "qa transcript %.160s", result->transcript);
            printf("qa: stt transcript=\"%s\"\n", result->transcript);
        }
        if (result->reply[0] != '\0') {
            FACULTY175_LOG_STAGE(TAG, "reply", "qa %.160s", result->reply);
            printf("qa: stt reply=\"%s\"\n", result->reply);
            append_history(result->transcript, result->reply);
            save_faculty_to_nvs();
        }
        fflush(stdout);
        ui_set(FACULTY175_UI_SPEAK, "qa stt");
    }

    printf("qa: stt done err=%s\n", esp_err_to_name(err));
    fflush(stdout);
    FACULTY175_LOG_STAGE(TAG, "stt", "qa done %s", esp_err_to_name(err));
    faculty175_voice_result_free(result);
    free(result);
    free(frame);
    ui_set(FACULTY175_UI_LISTEN, NULL);
    s_qa_stt_busy = false;
    vTaskDeleteWithCaps(NULL);
}

static esp_err_t qa_trigger_stt(uint32_t capture_ms)
{
    if (!faculty175_board_audio_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_qa_stt_busy) {
        return ESP_ERR_INVALID_STATE;
    }
    low_power_note_activity((uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS), "qa-stt");
    qa_stt_args_t *args = calloc(1, sizeof(*args));
    if (args == NULL) {
        return ESP_ERR_NO_MEM;
    }
    args->capture_ms = capture_ms;
    s_qa_stt_busy = true;
    const BaseType_t ok = xTaskCreateWithCaps(qa_stt_task, "qa_stt", VOICE_WORKER_STACK_BYTES,
                                              args, 4, NULL, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (ok != pdPASS) {
        free(args);
        s_qa_stt_busy = false;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t faculty175_request_qa_stt(uint32_t capture_ms)
{
    return qa_trigger_stt(capture_ms);
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
        const esp_err_t ble_err = faculty175_ble_init();
        if (ble_err != ESP_OK) {
            FACULTY175_LOG_STAGE_E(TAG, "ble", "start failed: %s", esp_err_to_name(ble_err));
        }
    }
    vTaskDelete(NULL);
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)data;
        faculty175_wifi_settings_set_ap_client_count(1);
        faculty175_wifi_monitor_record_ap_client(true, event != NULL ? (int)event->aid : -1);
        FACULTY175_LOG_STAGE(TAG,
                             "wifi",
                             "setup AP client joined aid=%d",
                             event != NULL ? (int)event->aid : -1);
        return;
    }
    if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)data;
        faculty175_wifi_settings_set_ap_client_count(0);
        faculty175_wifi_monitor_record_ap_client(false, event != NULL ? (int)event->aid : -1);
        FACULTY175_LOG_STAGE(TAG,
                             "wifi",
                             "setup AP client left aid=%d",
                             event != NULL ? (int)event->aid : -1);
        return;
    }
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (faculty175_wifi_settings_scan_suppressed()) {
            FACULTY175_LOG_STAGE(TAG, "wifi", "STA start held for serial scan");
            return;
        }
        if (!low_power_wifi_allowed()) {
            FACULTY175_LOG_STAGE(TAG, "wifi", "STA start held for battery idle");
            return;
        }
        FACULTY175_LOG_STAGE(TAG, "wifi", "STA start — connecting to %s", s_wifi_ssid);
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_wifi_low_power_paused) {
            FACULTY175_LOG_STAGE(TAG, "wifi", "disconnected for battery sleep");
            xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT);
            faculty175_screen_http_stop();
            return;
        }
        if (faculty175_wifi_settings_scan_suppressed()) {
            FACULTY175_LOG_STAGE(TAG, "wifi", "disconnect retry held for serial scan");
            xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT);
            faculty175_screen_http_stop();
            return;
        }
        const wifi_event_sta_disconnected_t *disc = (const wifi_event_sta_disconnected_t *)data;
        const int reason = disc != NULL ? (int)disc->reason : -1;
        char disc_ssid[FACULTY175_WIFI_SSID_MAX + 1] = {};
        if (disc != NULL && disc->ssid_len > 0) {
            const size_t ssid_len = disc->ssid_len < FACULTY175_WIFI_SSID_MAX ? disc->ssid_len : FACULTY175_WIFI_SSID_MAX;
            memcpy(disc_ssid, disc->ssid, ssid_len);
            disc_ssid[ssid_len] = '\0';
        }
        faculty175_wifi_monitor_record_disconnected(disc_ssid[0] != '\0' ? disc_ssid : s_wifi_ssid,
                                                    disc != NULL ? disc->bssid : NULL,
                                                    reason,
                                                    disc != NULL ? (int)disc->rssi : 0);
        FACULTY175_LOG_STAGE_W(TAG, "wifi", "disconnected reason=%d — retrying", reason);
        xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT);
        faculty175_screen_http_stop();
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)data;
        if (event != NULL) {
            FACULTY175_LOG_STAGE(TAG, "wifi", "connected ip=" IPSTR " gw=" IPSTR, IP2STR(&event->ip_info.ip),
                           IP2STR(&event->ip_info.gw));
            wifi_ap_record_t ap = {};
            if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
                faculty175_wifi_monitor_record_connected(&ap);
            } else {
                faculty175_wifi_monitor_record_connected(NULL);
            }
            if (faculty175_wifi_settings_ap_active()) {
                esp_netif_ip_info_t ap_ip = {};
                if (s_wifi_setup_ap_netif != NULL && esp_netif_get_ip_info(s_wifi_setup_ap_netif, &ap_ip) == ESP_OK) {
                    ip_napt_enable(ap_ip.ip.addr, 1);
                    faculty175_wifi_monitor_record_note("router-napt", "enabled");
                    FACULTY175_LOG_STAGE(TAG, "wifi", "APSTA NAPT enabled ap=" IPSTR, IP2STR(&ap_ip.ip));
                }
                faculty175_wifi_settings_set_router_upstream(s_wifi_ssid, &event->ip_info.ip);
            } else {
                faculty175_wifi_settings_set_sta(s_wifi_ssid, &event->ip_info.ip);
            }
            (void)faculty175_screen_http_start(&event->ip_info.ip);
        } else {
            FACULTY175_LOG_STAGE(TAG, "wifi", "connected (got IP)");
            if (faculty175_wifi_settings_ap_active()) {
                faculty175_wifi_settings_set_router_upstream(s_wifi_ssid, NULL);
            } else {
                faculty175_wifi_settings_set_sta(s_wifi_ssid, NULL);
            }
            (void)faculty175_screen_http_start(NULL);
        }
        (void)astrolabe_time_start(NULL);
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
    }
}

static esp_err_t wifi_start_setup_ap(const char *reason)
{
    if (s_wifi_setup_ap_netif == NULL) {
        s_wifi_setup_ap_netif = esp_netif_create_default_wifi_ap();
    }
    uint8_t mac[6] = {};
    (void)esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    char ap_ssid[FACULTY175_WIFI_SSID_MAX + 1];
    snprintf(ap_ssid, sizeof(ap_ssid), "Astrolabe-%02X%02X", mac[4], mac[5]);
    const char *ap_pass = "astrolabe";

    wifi_config_t ap = {};
    faculty175_strlcpy((char *)ap.ap.ssid, ap_ssid, sizeof(ap.ap.ssid));
    faculty175_strlcpy((char *)ap.ap.password, ap_pass, sizeof(ap.ap.password));
    ap.ap.ssid_len = strlen(ap_ssid);
    ap.ap.channel = 6;
    ap.ap.max_connection = 4;
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap.ap.beacon_interval = 100;
    ap.ap.pmf_cfg.capable = false;
    ap.ap.pmf_cfg.required = false;

    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err == ESP_OK) {
        err = esp_wifi_set_config(WIFI_IF_AP, &ap);
    }
    if (err == ESP_OK) {
        err = esp_wifi_start();
    }
    if (err == ESP_OK) {
        esp_err_t bw_err = esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT20);
        if (bw_err != ESP_OK) {
            FACULTY175_LOG_STAGE_W(TAG, "wifi", "setup AP HT20 failed: %s", esp_err_to_name(bw_err));
        }
    }
    if (err != ESP_OK) {
        faculty175_wifi_settings_clear_runtime();
        FACULTY175_LOG_STAGE_E(TAG, "wifi", "setup AP failed: %s", esp_err_to_name(err));
        return err;
    }
    s_wifi_driver_started = true;

    esp_netif_ip_info_t ip_info = {};
    if (s_wifi_setup_ap_netif != NULL) {
        (void)esp_netif_get_ip_info(s_wifi_setup_ap_netif, &ip_info);
    }
    faculty175_wifi_settings_set_ap(ap_ssid, ap_pass, &ip_info.ip);
    ui_set(FACULTY175_UI_WIFI, ap_ssid);
    FACULTY175_LOG_STAGE_W(TAG,
                           "wifi",
                           "%s; setup AP %s pass=%s url=%s",
                           reason != NULL ? reason : "setup AP",
                           ap_ssid,
                           ap_pass,
                           faculty175_wifi_settings_url());
    return ESP_OK;
}

static esp_err_t wifi_configure_setup_ap_for_apsta(const char *reason)
{
    if (s_wifi_setup_ap_netif == NULL) {
        s_wifi_setup_ap_netif = esp_netif_create_default_wifi_ap();
    }
    uint8_t mac[6] = {};
    (void)esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    char ap_ssid[FACULTY175_WIFI_SSID_MAX + 1];
    snprintf(ap_ssid, sizeof(ap_ssid), "Astrolabe-%02X%02X", mac[4], mac[5]);
    const char *ap_pass = "astrolabe";

    wifi_config_t ap = {};
    faculty175_strlcpy((char *)ap.ap.ssid, ap_ssid, sizeof(ap.ap.ssid));
    faculty175_strlcpy((char *)ap.ap.password, ap_pass, sizeof(ap.ap.password));
    ap.ap.ssid_len = strlen(ap_ssid);
    ap.ap.channel = 6;
    ap.ap.max_connection = 4;
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap.ap.beacon_interval = 100;
    ap.ap.pmf_cfg.capable = false;
    ap.ap.pmf_cfg.required = false;

    esp_err_t err = esp_wifi_set_config(WIFI_IF_AP, &ap);
    if (err != ESP_OK) {
        faculty175_wifi_settings_clear_runtime();
        FACULTY175_LOG_STAGE_E(TAG, "wifi", "setup AP config failed: %s", esp_err_to_name(err));
        return err;
    }

    esp_netif_ip_info_t ip_info = {};
    if (s_wifi_setup_ap_netif != NULL) {
        (void)esp_netif_get_ip_info(s_wifi_setup_ap_netif, &ip_info);
    }
    faculty175_wifi_settings_set_ap(ap_ssid, ap_pass, &ip_info.ip);
    FACULTY175_LOG_STAGE_W(TAG,
                           "wifi",
                           "%s; setup AP configured for APSTA %s pass=%s url=%s",
                           reason != NULL ? reason : "setup AP",
                           ap_ssid,
                           ap_pass,
                           faculty175_wifi_settings_url());
    return ESP_OK;
}

static bool wifi_candidate_exists(const wifi_candidate_t *candidates, size_t count, const char *ssid)
{
    if (ssid == NULL || ssid[0] == '\0') {
        return true;
    }
    for (size_t i = 0; i < count; ++i) {
        if (strcmp(candidates[i].ssid, ssid) == 0) {
            return true;
        }
    }
    return false;
}

static void wifi_candidate_add(wifi_candidate_t *candidates,
                               size_t *count,
                               const char *ssid,
                               const char *pass,
                               const char *source)
{
    if (candidates == NULL || count == NULL || *count >= WIFI_CANDIDATE_MAX ||
        wifi_candidate_exists(candidates, *count, ssid)) {
        return;
    }
    faculty175_strlcpy(candidates[*count].ssid, ssid, sizeof(candidates[*count].ssid));
    faculty175_strlcpy(candidates[*count].pass, pass != NULL ? pass : "", sizeof(candidates[*count].pass));
    candidates[*count].source = source;
    ++(*count);
}

static esp_err_t wifi_connect_candidate(const wifi_candidate_t *candidate)
{
    if (candidate == NULL || candidate->ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    faculty175_strlcpy(s_wifi_ssid, candidate->ssid, sizeof(s_wifi_ssid));

    wifi_config_t wifi = {};
    faculty175_strlcpy((char *)wifi.sta.ssid, candidate->ssid, sizeof(wifi.sta.ssid));
    faculty175_strlcpy((char *)wifi.sta.password, candidate->pass, sizeof(wifi.sta.password));
    const bool router_mode = s_wifi_auto_apsta_enabled;
    if (faculty175_wifi_settings_travel_router_enabled() && !s_wifi_auto_apsta_enabled) {
        faculty175_wifi_monitor_record_note("router-deferred", "auto APSTA disabled");
    }
    esp_err_t err = esp_wifi_set_mode(router_mode ? WIFI_MODE_APSTA : WIFI_MODE_STA);
    if (err != ESP_OK) {
        FACULTY175_LOG_STAGE_E(TAG, "wifi", "set mode failed ssid=%s err=%s",
                               candidate->ssid, esp_err_to_name(err));
        return err;
    }
    if (router_mode && !faculty175_wifi_settings_ap_active()) {
        err = wifi_configure_setup_ap_for_apsta(faculty175_wifi_settings_travel_router_enabled()
                                                    ? "travel router enabled"
                                                    : "APSTA control enabled");
        if (err != ESP_OK) {
            return err;
        }
    }
    if (router_mode && s_wifi_driver_started) {
        (void)esp_wifi_disconnect();
        vTaskDelay(pdMS_TO_TICKS(150));
    }
    err = esp_wifi_set_config(WIFI_IF_STA, &wifi);
    if (err == ESP_ERR_WIFI_STATE) {
        FACULTY175_LOG_STAGE_W(TAG, "wifi", "STA busy while applying config; disconnect/retry ssid=%s",
                               candidate->ssid);
        (void)esp_wifi_disconnect();
        vTaskDelay(pdMS_TO_TICKS(300));
        err = esp_wifi_set_config(WIFI_IF_STA, &wifi);
    }
    if (err != ESP_OK) {
        FACULTY175_LOG_STAGE_E(TAG, "wifi", "set STA config failed ssid=%s err=%s",
                               candidate->ssid, esp_err_to_name(err));
        return err;
    }
    if (!s_wifi_driver_started) {
        err = esp_wifi_start();
        if (err != ESP_OK) {
            FACULTY175_LOG_STAGE_E(TAG, "wifi", "start failed ssid=%s err=%s",
                                   candidate->ssid, esp_err_to_name(err));
            return err;
        }
        s_wifi_driver_started = true;
        if (router_mode && faculty175_wifi_settings_ap_active()) {
            esp_err_t bw_err = esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT20);
            if (bw_err != ESP_OK) {
                FACULTY175_LOG_STAGE_W(TAG, "wifi", "APSTA HT20 failed: %s", esp_err_to_name(bw_err));
            }
        }
    } else {
        if (router_mode && faculty175_wifi_settings_ap_active()) {
            esp_err_t bw_err = esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT20);
            if (bw_err != ESP_OK) {
                FACULTY175_LOG_STAGE_W(TAG, "wifi", "APSTA HT20 failed: %s", esp_err_to_name(bw_err));
            }
        }
        err = esp_wifi_connect();
        if (err != ESP_OK && err != ESP_ERR_WIFI_CONN) {
            FACULTY175_LOG_STAGE_E(TAG, "wifi", "connect failed ssid=%s err=%s",
                                   candidate->ssid, esp_err_to_name(err));
            return err;
        }
    }

    ui_set(FACULTY175_UI_WIFI, candidate->ssid);
    FACULTY175_LOG_STAGE(TAG,
                         "wifi",
                         "waiting for IP (20s) ssid=%s source=%s",
                         candidate->ssid,
                         candidate->source != NULL ? candidate->source : "-");
    xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    EventBits_t bits = 0;
    TickType_t waited = 0;
    const TickType_t poll_ticks = pdMS_TO_TICKS(250);
    const TickType_t timeout_ticks = pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS);
    while (waited < timeout_ticks) {
        if (s_wifi_settings_ap_requested) {
            FACULTY175_LOG_STAGE_W(TAG, "wifi", "settings requested setup AP; abort STA wait");
            bits |= WIFI_FAIL_BIT;
            break;
        }
        bits = xEventGroupWaitBits(s_wifi_events,
                                   WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                   pdFALSE,
                                   pdFALSE,
                                   poll_ticks);
        if ((bits & (WIFI_CONNECTED_BIT | WIFI_FAIL_BIT)) != 0) {
            break;
        }
        waited += poll_ticks;
    }
    if (bits & WIFI_CONNECTED_BIT) {
        return ESP_OK;
    }
    FACULTY175_LOG_STAGE_E(TAG, "wifi", "connect timeout ssid=%s", candidate->ssid);
    (void)esp_wifi_disconnect();
    if (!router_mode) {
        faculty175_screen_http_stop();
        (void)esp_wifi_stop();
        s_wifi_driver_started = false;
    }
    xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    return ESP_ERR_TIMEOUT;
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
    FACULTY175_LOG_STAGE(TAG, "ready", "USB VAD always-on; battery STT on button press only");
    FACULTY175_LOG_STAGE(TAG, "ready", "serial: help | qa audio | ota status | screen.bmp");
    FACULTY175_LOG_STAGE(TAG, "ready", "monitor: ./scripts/astrolabe175c_build.sh -p PORT monitor");
}

static esp_err_t wifi_start(void)
{
    if (strlen(MYNAH_SUPABASE_URL) == 0 || strlen(MYNAH_SUPABASE_ANON_KEY) == 0) {
        FACULTY175_LOG_STAGE_W(TAG, "wifi", "Supabase secrets missing — voice pipeline will fail");
    }

    wifi_candidate_t candidates[WIFI_CANDIDATE_MAX] = {};
    size_t candidate_count = 0;
    wifi_candidate_add(candidates, &candidate_count, MYNAH_WIFI_SSID, MYNAH_WIFI_PASSWORD, "default");
    faculty175_wifi_known_t known[FACULTY175_WIFI_KNOWN_MAX] = {};
    const size_t known_count = faculty175_wifi_settings_load_known(known, FACULTY175_WIFI_KNOWN_MAX);
    for (size_t i = 0; i < known_count; ++i) {
        if (strcmp(known[i].ssid, "Hilton Honors") == 0) {
            continue;
        }
        wifi_candidate_add(candidates, &candidate_count, known[i].ssid, known[i].pass, "nvs");
    }
    wifi_candidate_add(candidates, &candidate_count, "Syzygyx", "12345678", "built-in");
    wifi_candidate_add(candidates, &candidate_count, "AstrolabeRouter", "astrolabe", "built-in");

    s_wifi_events = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        FACULTY175_LOG_STAGE_E(TAG, "wifi", "init failed: %s", esp_err_to_name(err));
        faculty175_wifi_settings_clear_runtime();
        return err;
    }
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) {
        FACULTY175_LOG_STAGE_W(TAG, "wifi", "RAM storage failed: %s", esp_err_to_name(err));
    }
    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, NULL);
    if (err != ESP_OK) {
        FACULTY175_LOG_STAGE_E(TAG, "wifi", "event handler failed: %s", esp_err_to_name(err));
        return err;
    }
    err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL, NULL);
    if (err != ESP_OK) {
        FACULTY175_LOG_STAGE_E(TAG, "wifi", "ip handler failed: %s", esp_err_to_name(err));
        return err;
    }

    if (candidate_count == 0) {
        const esp_err_t ap_err = wifi_start_setup_ap("no saved SSID");
        s_wifi_start_complete = true;
        return ap_err == ESP_OK ? ESP_ERR_INVALID_STATE : ap_err;
    }

    s_wifi_sta_netif = esp_netif_create_default_wifi_sta();
    const bool router_mode = s_wifi_auto_apsta_enabled;
    if (faculty175_wifi_settings_travel_router_enabled() && !s_wifi_auto_apsta_enabled) {
        FACULTY175_LOG_STAGE_W(TAG, "wifi", "travel router saved in NVS; auto APSTA deferred to avoid SoftAP boot crash");
    }
    for (size_t i = 0; i < candidate_count; ++i) {
        const esp_err_t connect_err = wifi_connect_candidate(&candidates[i]);
        if (connect_err == ESP_OK) {
            s_wifi_start_complete = true;
            return ESP_OK;
        }
        if (s_wifi_settings_ap_requested) {
            s_wifi_settings_ap_requested = false;
            const esp_err_t ap_err = wifi_start_setup_ap("settings requested");
            s_wifi_start_complete = true;
            return ap_err == ESP_OK ? ESP_ERR_INVALID_STATE : ap_err;
        }
    }
    if (router_mode && faculty175_wifi_settings_ap_active()) {
        s_wifi_start_complete = true;
        FACULTY175_LOG_STAGE_W(TAG, "wifi", "STA candidates exhausted; keeping setup AP alive for captive login");
        return ESP_ERR_INVALID_STATE;
    }
    if (known_count > 0) {
        faculty175_wifi_settings_clear_runtime();
        s_wifi_start_complete = true;
        FACULTY175_LOG_STAGE_W(TAG, "wifi", "STA candidates exhausted; setup AP deferred until settings request");
        return ESP_ERR_TIMEOUT;
    }
    const esp_err_t ap_err = wifi_start_setup_ap("STA candidates exhausted");
    s_wifi_start_complete = true;
    return ap_err == ESP_OK ? ESP_ERR_INVALID_STATE : ap_err;
}

static void wifi_start_task(void *arg)
{
    (void)arg;
    s_wifi_auto_apsta_enabled = false;
    const esp_err_t wifi_err = wifi_start();
    if (wifi_err != ESP_OK) {
        if (s_faculty_ready) {
            faculty175_faculty_set_network_fetch_enabled(false);
        }
        FACULTY175_LOG_STAGE_W(TAG, "wifi", "continuing offline: %s", esp_err_to_name(wifi_err));
    } else {
        if (s_faculty_ready) {
            faculty175_faculty_set_network_fetch_enabled(true);
            faculty175_faculty_request_bust(s_faculty_slug);
            faculty175_faculty_prefetch_roster();
        }
        faculty175_ota_maybe_start_recovery_request();
        FACULTY175_LOG_STAGE(TAG, "network", "faculty bust cache enabled");
    }
    vTaskDelete(NULL);
}

static bool wifi_is_connected(void)
{
    return s_wifi_events != NULL &&
           (xEventGroupGetBits(s_wifi_events) & WIFI_CONNECTED_BIT) != 0;
}

static void ensure_settings_wifi_access(void)
{
    if (faculty175_wifi_settings_ap_active() || wifi_is_connected()) {
        return;
    }
    s_wifi_settings_ap_requested = true;
    if (s_wifi_events != NULL) {
        xEventGroupSetBits(s_wifi_events, WIFI_FAIL_BIT);
    }
    if (s_wifi_start_complete) {
        s_wifi_settings_ap_requested = false;
        (void)wifi_start_setup_ap("settings requested");
    }
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

    if (!has_slug || strcmp(s_faculty_slug, "nabokov") == 0) {
        faculty175_faculty_roster_entry_t active = {};
        if (faculty175_faculty_roster_active(&active)) {
            faculty175_strlcpy(s_faculty_slug, active.slug, sizeof(s_faculty_slug));
            faculty175_strlcpy(s_faculty_name, active.name, sizeof(s_faculty_name));
        } else {
            faculty175_strlcpy(s_faculty_slug, ASTROLABE_FACULTY_DEFAULT_FACULTY_SLUG, sizeof(s_faculty_slug));
            faculty175_strlcpy(s_faculty_name, ASTROLABE_FACULTY_DEFAULT_FACULTY_NAME, sizeof(s_faculty_name));
        }
    }
    if (strcmp(s_faculty_slug, ASTROLABE_FACULTY_DEFAULT_FACULTY_SLUG) != 0) {
        faculty175_strlcpy(s_faculty_slug, ASTROLABE_FACULTY_DEFAULT_FACULTY_SLUG, sizeof(s_faculty_slug));
        faculty175_strlcpy(s_faculty_name, ASTROLABE_FACULTY_DEFAULT_FACULTY_NAME, sizeof(s_faculty_name));
        (void)faculty175_faculty_roster_set_active_index(0);
        save_faculty_to_nvs();
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

static const faculty175_face_desc_t *nav_relative_delta(int delta)
{
    size_t index = 0;
    size_t count = 0;
    if (!faculty175_faces_nav_position(&index, &count) || count <= 1 || delta == 0) {
        return NULL;
    }
    const size_t step = (size_t)(delta > 0 ? delta : -delta) % count;
    const size_t next_index = delta >= 0 ? (index + step) % count : (index + count - step) % count;
    return faculty175_faces_nav_at(next_index);
}

static uint16_t nav_rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
}

static void draw_nav_hint(const faculty175_face_desc_t *face, int x, int y, const char *mark, uint16_t color)
{
    if (face == NULL) {
        return;
    }
    faculty175_display_draw_text(mark, x, y, color);
}

enum {
    NAV_PREVIEW_X = 205,
    NAV_PREVIEW_Y = 205,
    NAV_PREVIEW_W = 56,
    NAV_PREVIEW_H = 56,
    NAV_HINT_SIZE = 28,
};

static void draw_nav_preview_surface(void)
{
    const faculty175_face_desc_t *center = faculty175_faces_current();
    const faculty175_face_desc_t *left = nav_relative_delta(-1);
    const faculty175_face_desc_t *right = nav_relative_delta(1);
    const faculty175_face_desc_t *up = NULL;
    const faculty175_face_desc_t *down = NULL;
    const uint16_t primary = nav_rgb565(255, 242, 196);
    const uint16_t cyan = nav_rgb565(127, 205, 224);
    const uint16_t amber = nav_rgb565(255, 207, 102);

    faculty175_display_draw_circle(FACULTY175_LCD_W / 2, FACULTY175_LCD_H / 2, 12, primary);
    faculty175_display_draw_circle(FACULTY175_LCD_W / 2, FACULTY175_LCD_H / 2, 20, cyan);

    draw_nav_hint(left, 26, FACULTY175_LCD_H / 2 - 4, "<", cyan);
    draw_nav_hint(right, FACULTY175_LCD_W - 36, FACULTY175_LCD_H / 2 - 4, ">", cyan);
    draw_nav_hint(up, FACULTY175_LCD_W / 2 - 4, 24, "^", amber);
    draw_nav_hint(down, FACULTY175_LCD_W / 2 - 4, FACULTY175_LCD_H - 34, "v", amber);
    (void)center;
}

static void flush_nav_preview_surface(void)
{
    faculty175_display_flush_rect(NAV_PREVIEW_X, NAV_PREVIEW_Y, NAV_PREVIEW_W, NAV_PREVIEW_H);
    faculty175_display_flush_rect(18, FACULTY175_LCD_H / 2 - 16, NAV_HINT_SIZE, NAV_HINT_SIZE);
    faculty175_display_flush_rect(FACULTY175_LCD_W - 48, FACULTY175_LCD_H / 2 - 16, NAV_HINT_SIZE, NAV_HINT_SIZE);
    faculty175_display_flush_rect(FACULTY175_LCD_W / 2 - 14, 18, NAV_HINT_SIZE, NAV_HINT_SIZE);
    faculty175_display_flush_rect(FACULTY175_LCD_W / 2 - 14, FACULTY175_LCD_H - 48, NAV_HINT_SIZE, NAV_HINT_SIZE);
}

static uint32_t draw_nav_preview(uint32_t anim_ms)
{
    const uint32_t start_ms = faculty175_log_ms();
    const faculty175_face_desc_t *center = faculty175_faces_current();
    if (faculty175_lvgl_draw_nav(center,
                                 nav_relative_delta(-1),
                                 nav_relative_delta(1),
                                 NULL,
                                 NULL,
                                 anim_ms)) {
        return faculty175_log_ms() - start_ms;
    }
    draw_nav_preview_surface();
    flush_nav_preview_surface();
    return faculty175_log_ms() - start_ms;
}

static bool animate_nav_preview_native(bool vertical, int delta, uint32_t duration_ms)
{
    const uint32_t duration = duration_ms > 0 ? duration_ms : NAV_TRANSITION_MS;
    const faculty175_face_desc_t *center = faculty175_faces_current();
    if (faculty175_lvgl_transition_nav(center, vertical, delta, duration)) {
        return true;
    }
    draw_nav_preview_surface();
    const uint32_t start_ms = faculty175_log_ms();
    flush_nav_preview_surface();
    const uint32_t actual_ms = faculty175_log_ms() - start_ms;
    ESP_LOGI(TAG,
             "nav-anim-metrics kind=native-nav-preview axis=%s delta=%d expected_ms=%u actual_ms=%u frames=%u avg_gap_ms=%u max_gap_ms=%u overrun_ms=%d",
             vertical ? "vertical" : "horizontal",
             delta,
             (unsigned)duration,
             (unsigned)actual_ms,
             1u,
             (unsigned)actual_ms,
             (unsigned)actual_ms,
             (int)((int32_t)actual_ms - (int32_t)duration));
    return true;
}

static uint16_t *alloc_carousel_frame(size_t pixel_count)
{
    const size_t bytes = pixel_count * sizeof(uint16_t);
    uint16_t *frame = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (frame == NULL) {
        frame = malloc(bytes);
    }
    return frame;
}

static bool render_face_snapshot(const faculty175_face_desc_t *face, uint32_t anim_ms, uint16_t *out, size_t pixel_count)
{
    uint8_t waveform[ASTROLABE_AUDIO_PIPELINE_WAVEFORM_LEN] = {};
    uint8_t waveform_stream[ASTROLABE_AUDIO_PIPELINE_WAVEFORM_LEN] = {};
    faculty175_display_flush_suspended_set(true);
    (void)draw_face_or_status(face,
                              s_ui,
                              s_detail,
                              anim_ms,
                              waveform,
                              waveform_stream,
                              sizeof(waveform),
                              true);
    const bool copied = faculty175_display_frame_copy(out, pixel_count);
    faculty175_display_flush_suspended_set(false);
    return copied;
}

static bool capture_face_frame(const faculty175_face_desc_t *face, uint32_t anim_ms, uint16_t *out, size_t pixel_count)
{
    faculty175_display_lock();
    const bool ok = render_face_snapshot(face, anim_ms, out, pixel_count);
    faculty175_display_unlock();
    return ok;
}

static bool capture_current_frame(uint16_t *out, size_t pixel_count)
{
    faculty175_display_lock();
    const bool ok = faculty175_display_frame_copy(out, pixel_count);
    faculty175_display_unlock();
    return ok;
}

static void animate_face_slide(const faculty175_face_desc_t *from_face,
                               const faculty175_face_desc_t *to_face,
                               bool vertical,
                               int delta,
                               uint32_t now_ms)
{
    if (from_face == NULL || to_face == NULL || from_face->id == to_face->id) {
        ui_redraw();
        return;
    }

    const size_t pixels = faculty175_display_frame_pixel_count();
    uint16_t *from = alloc_carousel_frame(pixels);
    uint16_t *to = alloc_carousel_frame(pixels);
    if (from == NULL || to == NULL) {
        free(from);
        free(to);
        ui_redraw();
        return;
    }

    const bool have_from = capture_current_frame(from, pixels) ||
                           capture_face_frame(from_face, now_ms, from, pixels);
    const bool have_to = capture_face_frame(to_face, now_ms + FACE_CAROUSEL_FRAME_MS, to, pixels);
    if (have_from && have_to) {
        const uint32_t anim_start_ms = faculty175_log_ms();
        faculty175_display_lock();
        faculty175_display_draw_rgb565(from, 0, 0, FACULTY175_LCD_W, FACULTY175_LCD_H);
        const int dir = delta >= 0 ? 1 : -1;
        uint32_t frames = 0;
        uint32_t max_gap_ms = 0;
        uint32_t last_frame_ms = faculty175_log_ms();
        for (int frame = 1; frame <= FACE_CAROUSEL_FRAMES; ++frame) {
            if (vertical) {
                const int shift = (FACULTY175_LCD_H * frame * dir) / FACE_CAROUSEL_FRAMES;
                faculty175_display_frame_compose_vertical(from, to, shift);
            } else {
                const int shift = (FACULTY175_LCD_W * frame * dir) / FACE_CAROUSEL_FRAMES;
                faculty175_display_frame_compose_carousel(from, to, shift);
            }
            faculty175_display_flush_rect(0, 0, FACULTY175_LCD_W, FACULTY175_LCD_H);
            const uint32_t now_frame_ms = faculty175_log_ms();
            const uint32_t gap_ms = now_frame_ms - last_frame_ms;
            if (gap_ms > max_gap_ms) {
                max_gap_ms = gap_ms;
            }
            last_frame_ms = now_frame_ms;
            ++frames;
        }
        faculty175_display_unlock();
        const uint32_t actual_ms = faculty175_log_ms() - anim_start_ms;
        ESP_LOGI(TAG,
                 "nav-anim-metrics kind=native-snapshot-slide axis=%s delta=%d expected_ms=%u actual_ms=%u frames=%u avg_gap_ms=%u max_gap_ms=%u overrun_ms=%d",
                 vertical ? "vertical" : "horizontal",
                 delta,
                 (unsigned)(FACE_CAROUSEL_FRAMES * FACE_CAROUSEL_FRAME_MS),
                 (unsigned)actual_ms,
                 (unsigned)frames,
                 (unsigned)(frames > 0 ? actual_ms / frames : actual_ms),
                 (unsigned)max_gap_ms,
                 (int)((int32_t)actual_ms - (int32_t)(FACE_CAROUSEL_FRAMES * FACE_CAROUSEL_FRAME_MS)));
        FACULTY175_LOG_STAGE(TAG,
                             "nav-metrics",
                             "slide from=%s to=%s axis=%s delta=%d expected_ms=%u actual_ms=%u",
                             from_face->slug,
                             to_face->slug,
                             vertical ? "vertical" : "horizontal",
                             delta,
                             (unsigned)(FACE_CAROUSEL_FRAMES * FACE_CAROUSEL_FRAME_MS),
                             (unsigned)actual_ms);
    } else {
        ui_redraw();
    }
    free(from);
    free(to);
}

static void animate_face_carousel(const faculty175_face_desc_t *from_face,
                                  const faculty175_face_desc_t *to_face,
                                  int delta,
                                  uint32_t now_ms)
{
    animate_face_slide(from_face, to_face, false, delta, now_ms);
}

typedef bool (*face_state_change_fn_t)(int delta, uint32_t now_ms, void *ctx);

static void animate_face_vertical_change(const faculty175_face_desc_t *face,
                                         int delta,
                                         uint32_t now_ms,
                                         face_state_change_fn_t change,
                                         void *ctx)
{
    if (face == NULL || delta == 0 || change == NULL) {
        return;
    }
    const size_t pixels = faculty175_display_frame_pixel_count();
    uint16_t *from = alloc_carousel_frame(pixels);
    uint16_t *to = alloc_carousel_frame(pixels);
    if (from == NULL || to == NULL) {
        free(from);
        free(to);
        if (change(delta, now_ms, ctx)) {
            ui_redraw();
        }
        return;
    }

    const bool have_from = capture_face_frame(face, now_ms, from, pixels);
    const bool changed = change(delta, now_ms, ctx);
    const faculty175_face_desc_t *to_face = faculty175_faces_current();
    const bool have_to = changed && capture_face_frame(to_face != NULL ? to_face : face,
                                                       now_ms + FACE_CAROUSEL_FRAME_MS,
                                                       to,
                                                       pixels);
    if (changed && have_from && have_to) {
        const uint32_t anim_start_ms = faculty175_log_ms();
        faculty175_display_lock();
        const int max_r = FACULTY175_LCD_W > FACULTY175_LCD_H ? FACULTY175_LCD_W : FACULTY175_LCD_H;
        for (int frame = 0; frame <= FACE_CAROUSEL_FRAMES; ++frame) {
            const int r = (max_r * frame) / FACE_CAROUSEL_FRAMES;
            faculty175_display_frame_compose_radial(from, to, r);
            faculty175_display_flush();
            vTaskDelay(pdMS_TO_TICKS(FACE_CAROUSEL_FRAME_MS));
        }
        faculty175_display_unlock();
        FACULTY175_LOG_STAGE(TAG,
                             "nav-metrics",
                             "vertical-state face=%s delta=%d expected_ms=%u actual_ms=%u",
                             face->slug,
                             delta,
                             (unsigned)(FACE_CAROUSEL_FRAMES * FACE_CAROUSEL_FRAME_MS),
                             (unsigned)(faculty175_log_ms() - anim_start_ms));
    } else if (changed) {
        ui_redraw();
    }
    free(from);
    free(to);
}

static bool change_faculty_roster_for_vertical(int delta, uint32_t now_ms, void *ctx)
{
    (void)now_ms;
    (void)ctx;
    return cycle_roster_by_delta(delta);
}

static bool change_synastry_profile_for_vertical(int delta, uint32_t now_ms, void *ctx)
{
    (void)now_ms;
    (void)ctx;
    int slot = -1;
    faculty175_birth_chart_t profile = {};
    if (!faculty175_charts_cycle_active(delta, &slot, &profile)) {
        return false;
    }
    FACULTY175_LOG_STAGE(TAG, "charts", "synastry vertical %s -> %s (%d)",
                         delta > 0 ? "next" : "prev", profile.name, slot);
    return true;
}

static bool change_chakra_for_vertical(int delta, uint32_t now_ms, void *ctx)
{
    (void)now_ms;
    (void)ctx;
    const faculty175_face_desc_t *face = faculty175_faces_current();
    if (face == NULL || !faculty175_face_native_chakra_delta(face->id, delta)) {
        return false;
    }
    FACULTY175_LOG_STAGE(TAG, "faces", "chakra %s -> %s", delta > 0 ? "up" : "down",
                         faculty175_face_native_chakra_name());
    return true;
}

static uint32_t draw_current_face_now(uint32_t now_ms)
{
    uint8_t waveform[ASTROLABE_AUDIO_PIPELINE_WAVEFORM_LEN] = {};
    uint8_t waveform_stream[ASTROLABE_AUDIO_PIPELINE_WAVEFORM_LEN] = {};
    const faculty175_face_desc_t *face = faculty175_faces_current();
    const uint32_t draw_start_ms = faculty175_log_ms();
    faculty175_display_lock();
    (void)draw_face_or_status(face,
                              s_ui,
                              s_detail,
                              now_ms,
                              waveform,
                              waveform_stream,
                              sizeof(waveform),
                              true);
    faculty175_display_unlock();
    return faculty175_log_ms() - draw_start_ms;
}

static uint32_t transition_current_face_now(faculty175_face_id_t from_id,
                                            uint32_t now_ms,
                                            bool vertical,
                                            int delta,
                                            const char **anim_out)
{
    const faculty175_face_desc_t *face = faculty175_faces_current();
    const faculty175_face_desc_t *from_face = faculty175_faces_get(from_id);
    const uint32_t draw_start_ms = faculty175_log_ms();

    if (face != NULL && from_face != NULL &&
        faculty175_lvgl_face_supported(from_face->id) &&
        faculty175_lvgl_face_supported(face->id)) {
        bool animated = false;
        faculty175_display_lock();
        const bool ok = faculty175_lvgl_transition_face(from_id,
                                                        face->id,
                                                        now_ms,
                                                        vertical,
                                                        delta,
                                                        NAV_TRANSITION_MS,
                                                        &animated);
        faculty175_display_unlock();
        if (ok) {
            FACULTY175_LOG_STAGE(TAG,
                                 "nav-metrics",
                                 "screen-slide-complete from=%s to=%s animated=%d",
                                 from_face->slug,
                                 face->slug,
                                 animated ? 1 : 0);
            if (anim_out != NULL) {
                *anim_out = animated ? "lvgl-screen-slide" : "direct-lvgl-swap";
            }
            return faculty175_log_ms() - draw_start_ms;
        }
    }

    if (face != NULL && from_face != NULL) {
        animate_face_slide(from_face, face, vertical, delta, now_ms);
        const uint32_t settle_start_ms = faculty175_log_ms();
        (void)draw_current_face_now(now_ms);
        FACULTY175_LOG_STAGE(TAG,
                             "nav-metrics",
                             "snapshot-slide-settle from=%s to=%s draw_ms=%u",
                             from_face->slug,
                             face->slug,
                             (unsigned)(faculty175_log_ms() - settle_start_ms));
        if (anim_out != NULL) {
            *anim_out = "native-snapshot-slide";
        }
        return faculty175_log_ms() - draw_start_ms;
    }

    const uint32_t fallback_ms = draw_current_face_now(now_ms);
    if (anim_out != NULL) {
        *anim_out = "direct-lvgl-swap";
    }
    return fallback_ms;
}

static void button_reboot_task(void *arg)
{
    (void)arg;
    bool button_was_down = false;
    bool reset_armed = false;
    uint32_t down_since_ms = 0;
    const uint32_t boot_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

    FACULTY175_LOG_STAGE(TAG,
                         "button",
                         "long-press reboot watchdog ready hold_ms=%u",
                         (unsigned)BUTTON_RESET_HOLD_MS);
    while (true) {
        const uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        const bool button_down = faculty175_button_pressed();
        const bool grace_elapsed = now_ms - boot_ms >= BUTTON_REBOOT_GRACE_MS;
        const bool pmu_long_press = faculty175_pmu_pekey_long_press();

        if (button_down) {
            if (!button_was_down) {
                down_since_ms = now_ms;
                reset_armed = true;
                button_was_down = true;
                FACULTY175_LOG_STAGE(TAG, "button", "long-press reboot armed");
            }

            if (reset_armed && grace_elapsed && now_ms - down_since_ms >= BUTTON_RESET_HOLD_MS) {
                FACULTY175_LOG_STAGE(TAG,
                                     "button",
                                     "long-press reboot firing held_ms=%u",
                                     (unsigned)(now_ms - down_since_ms));
                vTaskDelay(pdMS_TO_TICKS(80));
                esp_restart();
            }
        } else {
            if (button_was_down) {
                FACULTY175_LOG_STAGE(TAG,
                                     "button",
                                     "long-press reboot disarmed held_ms=%u",
                                     (unsigned)(now_ms - down_since_ms));
            }
            button_was_down = false;
            reset_armed = true;
            down_since_ms = 0;
        }

        if (pmu_long_press && grace_elapsed) {
            FACULTY175_LOG_STAGE(TAG, "button", "PMU long-press reboot firing");
            vTaskDelay(pdMS_TO_TICKS(80));
            esp_restart();
        } else if (pmu_long_press) {
            FACULTY175_LOG_STAGE(TAG, "button", "PMU long-press ignored during boot grace");
        }

        vTaskDelay(pdMS_TO_TICKS(25));
    }
}

static void input_task(void *arg)
{
    (void)arg;
    uint32_t last_bust_retry_ms = 0;
    uint32_t last_face_swipe_ms = 0;
    uint32_t last_time_retry_ms = 0;
    bool button_was_down = false;
    bool face_save_pending = false;
    s_nav_mode = false;
    s_low_power_last_activity_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    faculty175_display_nav_mode_set(false);
    FACULTY175_LOG_STAGE(TAG, "input", "gesture/button task ready");
    draw_current_face_now(s_low_power_last_activity_ms);

    while (true) {
        const uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        low_power_tick(now_ms);

        const bool button_down = faculty175_button_pressed();
        bool button_short_press = false;
        if (button_down) {
            button_was_down = true;
        } else {
            if (button_was_down) {
                button_short_press = true;
            }
            button_was_down = false;
        }

        if (!astrolabe_time_valid() && (last_time_retry_ms == 0 || now_ms - last_time_retry_ms >= 60000)) {
            last_time_retry_ms = now_ms;
            (void)astrolabe_time_retry_if_stale();
        }

        faculty175_gesture_t gesture = {};
        if (faculty175_gesture_consume(&gesture)) {
            const uint32_t gesture_ms = now_ms;
            const uint32_t queue_age_ms = gesture.queued_ms != 0 && now_ms >= gesture.queued_ms ? now_ms - gesture.queued_ms : 0;
            const bool woke_from_low_power = s_low_power_asleep || s_low_power_dimmed;
            low_power_note_activity(now_ms, "gesture");
            const faculty175_face_desc_t *active_face = faculty175_faces_current();
            if (gesture.kind == FACULTY175_GESTURE_LONG_TAP) {
                FACULTY175_LOG_STAGE(TAG,
                                     "faces",
                                     "ignored touch long press x=%d y=%d",
                                     (int)gesture.x,
                                     (int)gesture.y);
            } else if (woke_from_low_power) {
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            } else if (s_nav_mode && (gesture.kind == FACULTY175_GESTURE_TAP ||
                                      gesture.kind == FACULTY175_GESTURE_BEZEL_TAP)) {
                const char *exit_slug = active_face != NULL ? active_face->slug : "-";
                const uint32_t exit_start_ms = faculty175_log_ms();
                s_nav_mode = false;
                faculty175_display_nav_mode_set(false);
                uint32_t save_ms = 0;
                if (face_save_pending) {
                    const uint32_t save_start_ms = faculty175_log_ms();
                    const esp_err_t err = faculty175_faces_save_current();
                    save_ms = faculty175_log_ms() - save_start_ms;
                    if (err != ESP_OK) {
                        FACULTY175_LOG_STAGE(TAG, "faces", "save current failed %s", esp_err_to_name(err));
                    }
                    face_save_pending = false;
                }
                FACULTY175_LOG_STAGE(TAG, "faces", "navigation mode off");
                const uint32_t draw_start_ms = faculty175_log_ms();
                draw_current_face_now(now_ms);
                const uint32_t draw_ms = faculty175_log_ms() - draw_start_ms;
                FACULTY175_LOG_STAGE(TAG,
                                     "nav-metrics",
                                     "exit face=%s save_ms=%u full_draw_ms=%u total_ms=%u",
                                     exit_slug,
                                     (unsigned)save_ms,
                                     (unsigned)draw_ms,
                                     (unsigned)(faculty175_log_ms() - exit_start_ms));
            } else if (s_nav_mode && faculty175_faces_enabled_count() > 1) {
                if (gesture.kind == FACULTY175_GESTURE_BEZEL_ROTATE_CW ||
                    gesture.kind == FACULTY175_GESTURE_BEZEL_ROTATE_CCW ||
                    gesture.kind == FACULTY175_GESTURE_SWIPE_LEFT ||
                    gesture.kind == FACULTY175_GESTURE_SWIPE_RIGHT) {
                    const int delta = (gesture.kind == FACULTY175_GESTURE_BEZEL_ROTATE_CW ||
                                       gesture.kind == FACULTY175_GESTURE_SWIPE_LEFT) ? 1 : -1;
                    const char *from_slug = active_face != NULL ? active_face->slug : "-";
                    const uint32_t select_start_ms = faculty175_log_ms();
                    const faculty175_face_desc_t *face = faculty175_faces_cycle_runtime(delta);
                    const uint32_t select_ms = faculty175_log_ms() - select_start_ms;
                    FACULTY175_LOG_STAGE(TAG, "faces", "nav %s -> %s", delta > 0 ? "next" : "prev",
                                         face != NULL ? face->slug : "-");
                    if (face != NULL) {
                        last_face_swipe_ms = now_ms;
                        face_save_pending = true;
                        faculty175_display_lock();
                        const uint32_t preview_start_ms = faculty175_log_ms();
                        if (!animate_nav_preview_native(false, delta, NAV_TRANSITION_MS)) {
                            (void)draw_nav_preview(now_ms);
                        }
                        const uint32_t preview_ms = faculty175_log_ms() - preview_start_ms;
                        faculty175_display_unlock();
                        FACULTY175_LOG_STAGE(TAG,
                                             "nav-metrics",
                                             "swipe axis=horizontal from=%s to=%s delta=%d queue_age_ms=%u select_ms=%u preview_ms=%u total_ms=%u anim=native-nav-slide",
                                             from_slug,
                                             face->slug,
                                             delta,
                                             (unsigned)queue_age_ms,
                                             (unsigned)select_ms,
                                             (unsigned)preview_ms,
                                             (unsigned)(faculty175_log_ms() - gesture_ms));
                        faculty175_gesture_flush();
                    }
                } else if (gesture.kind == FACULTY175_GESTURE_SWIPE_UP ||
                           gesture.kind == FACULTY175_GESTURE_SWIPE_DOWN) {
                    FACULTY175_LOG_STAGE(TAG, "faces", "ignored vertical nav-mode swipe");
                    faculty175_gesture_flush();
                }
            } else if (!s_nav_mode && active_face != NULL && active_face->id == FACULTY175_FACE_FACULTY &&
                       (gesture.kind == FACULTY175_GESTURE_SWIPE_UP ||
                        gesture.kind == FACULTY175_GESTURE_SWIPE_DOWN)) {
                const int delta = gesture.kind == FACULTY175_GESTURE_SWIPE_UP ? 1 : -1;
                FACULTY175_LOG_STAGE(TAG, "faculty", "swipe %s roster", delta > 0 ? "up" : "down");
                animate_face_vertical_change(active_face, delta, now_ms, change_faculty_roster_for_vertical, NULL);
            } else if (!s_nav_mode && active_face != NULL && active_face->id == FACULTY175_FACE_SYNASTRY &&
                       (gesture.kind == FACULTY175_GESTURE_SWIPE_UP ||
                        gesture.kind == FACULTY175_GESTURE_SWIPE_DOWN)) {
                const int delta = gesture.kind == FACULTY175_GESTURE_SWIPE_UP ? 1 : -1;
                animate_face_vertical_change(active_face, delta, now_ms, change_synastry_profile_for_vertical, NULL);
            } else if (!s_nav_mode && active_face != NULL &&
                       (active_face->id == FACULTY175_FACE_CHAKRA || active_face->id == FACULTY175_FACE_BOWL) &&
                       (gesture.kind == FACULTY175_GESTURE_SWIPE_UP ||
                        gesture.kind == FACULTY175_GESTURE_SWIPE_DOWN)) {
                const int delta = gesture.kind == FACULTY175_GESTURE_SWIPE_UP ? 1 : -1;
                animate_face_vertical_change(active_face, delta, now_ms, change_chakra_for_vertical, NULL);
            } else if (!s_nav_mode && active_face != NULL &&
                       (active_face->id == FACULTY175_FACE_CHAKRA || active_face->id == FACULTY175_FACE_BOWL) &&
                       (gesture.kind == FACULTY175_GESTURE_SWIPE_LEFT ||
                        gesture.kind == FACULTY175_GESTURE_SWIPE_RIGHT)) {
                const int delta = gesture.kind == FACULTY175_GESTURE_SWIPE_LEFT ? 1 : -1;
                const uint32_t select_start_ms = faculty175_log_ms();
                const faculty175_face_desc_t *face = faculty175_faces_cycle_runtime(delta);
                const uint32_t select_ms = faculty175_log_ms() - select_start_ms;
                FACULTY175_LOG_STAGE(TAG, "faces", "chakra nav %s -> %s", delta > 0 ? "next" : "prev",
                                     face != NULL ? face->slug : "-");
                if (face != NULL) {
                    last_face_swipe_ms = now_ms;
                    face_save_pending = true;
                    const char *anim = "direct-lvgl-swap";
                    const uint32_t draw_ms = transition_current_face_now(active_face->id, now_ms, false, delta, &anim);
                    FACULTY175_LOG_STAGE(TAG,
                                         "nav-metrics",
                                         "direct-swipe from=%s to=%s delta=%d queue_age_ms=%u select_ms=%u draw_ms=%u total_ms=%u anim=%s",
                                         active_face->slug,
                                         face->slug,
                                         delta,
                                         (unsigned)queue_age_ms,
                                         (unsigned)select_ms,
                                         (unsigned)draw_ms,
                                         (unsigned)(faculty175_log_ms() - gesture_ms),
                                         anim);
                    faculty175_gesture_flush();
                }
            } else if (!s_nav_mode && active_face != NULL &&
                       ((active_face->id == FACULTY175_FACE_POCKETWATCH &&
                         gesture.kind == FACULTY175_GESTURE_SWIPE_DOWN) ||
                        (active_face->id == FACULTY175_FACE_SETTINGS &&
                         gesture.kind == FACULTY175_GESTURE_SWIPE_UP))) {
                const faculty175_face_id_t target_id =
                    active_face->id == FACULTY175_FACE_POCKETWATCH ? FACULTY175_FACE_SETTINGS : FACULTY175_FACE_POCKETWATCH;
                const int delta = active_face->id == FACULTY175_FACE_POCKETWATCH ? -1 : 1;
                const uint32_t select_start_ms = faculty175_log_ms();
                if (target_id == FACULTY175_FACE_SETTINGS) {
                    ensure_settings_wifi_access();
                }
                const esp_err_t set_err = faculty175_faces_set_runtime(target_id);
                const uint32_t select_ms = faculty175_log_ms() - select_start_ms;
                const faculty175_face_desc_t *face = set_err == ESP_OK ? faculty175_faces_current() : NULL;
                FACULTY175_LOG_STAGE(TAG,
                                     "faces",
                                     "watch settings shortcut %s -> %s",
                                     active_face->slug,
                                     face != NULL ? face->slug : "-");
                if (face != NULL) {
                    last_face_swipe_ms = now_ms;
                    face_save_pending = true;
                    const char *anim = "direct-lvgl-swap";
                    const uint32_t draw_ms = transition_current_face_now(active_face->id, now_ms, true, delta, &anim);
                    FACULTY175_LOG_STAGE(TAG,
                                         "nav-metrics",
                                         "settings-direct-swipe from=%s to=%s delta=%d queue_age_ms=%u select_ms=%u draw_ms=%u total_ms=%u anim=%s",
                                         active_face->slug,
                                         face->slug,
                                         delta,
                                         (unsigned)queue_age_ms,
                                         (unsigned)select_ms,
                                         (unsigned)draw_ms,
                                         (unsigned)(faculty175_log_ms() - gesture_ms),
                                         anim);
                    faculty175_gesture_flush();
                }
            } else if (!s_nav_mode && active_face != NULL &&
                       faculty175_wifi_lab_is_face(active_face->id) &&
                       (gesture.kind == FACULTY175_GESTURE_SWIPE_UP ||
                        gesture.kind == FACULTY175_GESTURE_SWIPE_DOWN)) {
                const int delta = gesture.kind == FACULTY175_GESTURE_SWIPE_UP ? 1 : -1;
                if (faculty175_wifi_lab_cycle_target(active_face->id, delta)) {
                    ui_redraw();
                }
                faculty175_gesture_flush();
            } else if (!s_nav_mode && active_face != NULL && active_face->id == FACULTY175_FACE_INCIDENTS &&
                       (gesture.kind == FACULTY175_GESTURE_SWIPE_UP ||
                        gesture.kind == FACULTY175_GESTURE_SWIPE_DOWN)) {
                const int delta = gesture.kind == FACULTY175_GESTURE_SWIPE_UP ? 1 : -1;
                if (faculty175_face_incidents_scroll(delta)) {
                    ui_redraw();
                }
                faculty175_gesture_flush();
            } else if (!s_nav_mode && active_face != NULL && false && faculty175_faces_vertical_group(active_face->id) &&
                       (gesture.kind == FACULTY175_GESTURE_SWIPE_UP ||
                        gesture.kind == FACULTY175_GESTURE_SWIPE_DOWN)) {
                const int delta = gesture.kind == FACULTY175_GESTURE_SWIPE_UP ? 1 : -1;
                const uint32_t select_start_ms = faculty175_log_ms();
                const faculty175_face_desc_t *face = faculty175_faces_cycle_vertical_runtime(delta);
                const uint32_t select_ms = faculty175_log_ms() - select_start_ms;
                FACULTY175_LOG_STAGE(TAG, "faces", "vertical %s -> %s", delta > 0 ? "next" : "prev",
                                     face != NULL ? face->slug : "-");
                if (face != NULL) {
                    last_face_swipe_ms = now_ms;
                    face_save_pending = true;
                    const char *anim = "direct-lvgl-swap";
                    const uint32_t draw_ms = transition_current_face_now(active_face->id, now_ms, true, delta, &anim);
                    FACULTY175_LOG_STAGE(TAG,
                                         "nav-metrics",
                                         "vertical-direct-swipe from=%s to=%s delta=%d queue_age_ms=%u select_ms=%u draw_ms=%u total_ms=%u anim=%s",
                                         active_face->slug,
                                         face->slug,
                                         delta,
                                         (unsigned)queue_age_ms,
                                         (unsigned)select_ms,
                                         (unsigned)draw_ms,
                                         (unsigned)(faculty175_log_ms() - gesture_ms),
                                         anim);
                    faculty175_gesture_flush();
                }
            } else if (!s_nav_mode && active_face != NULL && faculty175_faces_enabled_count() > 1 &&
                       (gesture.kind == FACULTY175_GESTURE_SWIPE_LEFT ||
                        gesture.kind == FACULTY175_GESTURE_SWIPE_RIGHT)) {
                const int delta = gesture.kind == FACULTY175_GESTURE_SWIPE_LEFT ? 1 : -1;
                const uint32_t select_start_ms = faculty175_log_ms();
                const faculty175_face_desc_t *face = faculty175_faces_cycle_runtime(delta);
                const uint32_t select_ms = faculty175_log_ms() - select_start_ms;
                FACULTY175_LOG_STAGE(TAG, "faces", "direct swipe %s -> %s", delta > 0 ? "next" : "prev",
                                     face != NULL ? face->slug : "-");
                if (face != NULL) {
                    last_face_swipe_ms = now_ms;
                    face_save_pending = true;
                    const char *anim = "direct-lvgl-swap";
                    const uint32_t draw_ms = transition_current_face_now(active_face->id, now_ms, false, delta, &anim);
                    FACULTY175_LOG_STAGE(TAG,
                                         "nav-metrics",
                                         "direct-swipe from=%s to=%s delta=%d queue_age_ms=%u select_ms=%u draw_ms=%u total_ms=%u anim=%s",
                                         active_face->slug,
                                         face->slug,
                                         delta,
                                         (unsigned)queue_age_ms,
                                         (unsigned)select_ms,
                                         (unsigned)draw_ms,
                                         (unsigned)(faculty175_log_ms() - gesture_ms),
                                         anim);
                    faculty175_gesture_flush();
                }
            } else if (!s_nav_mode && gesture.kind == FACULTY175_GESTURE_TAP) {
                const faculty175_face_desc_t *face = faculty175_faces_current();
                if (face != NULL && face->id == FACULTY175_FACE_POCKETWATCH) {
                    char profile_slug[24];
                    if (faculty175_pocketwatch_profile_tap(gesture.x, gesture.y, now_ms, profile_slug,
                                                           sizeof(profile_slug))) {
                        faculty175_face_profile_t profile;
                        if (faculty175_face_profile_from_slug(profile_slug, &profile)) {
                            const esp_err_t err = faculty175_face_profile_apply(profile, true);
                            FACULTY175_LOG_STAGE(TAG,
                                                 "profile",
                                                 "dial -> %s (%s)",
                                                 profile_slug,
                                                 esp_err_to_name(err));
                            ui_set(FACULTY175_UI_LISTEN, faculty175_face_profile_label(profile));
                            ui_redraw();
                        }
                        faculty175_gesture_flush();
                    } else if (face != NULL && faculty175_face_dispatch_action(face->id, now_ms)) {
                        ui_redraw();
                    } else {
                        FACULTY175_LOG_STAGE(TAG, "faces", "tap no action");
                    }
                } else if (face != NULL && faculty175_face_dispatch_action(face->id, now_ms)) {
                    ui_redraw();
                } else {
                    FACULTY175_LOG_STAGE(TAG, "faces", "tap no action");
                }
            }
        }

        if (s_faculty_ready && faculty175_faculty_bust_status() == FACULTY175_FACULTY_BUST_ERROR) {
            if (now_ms - last_bust_retry_ms >= 15000) {
                last_bust_retry_ms = now_ms;
                faculty175_faculty_request_bust(s_faculty_slug);
                FACULTY175_LOG_STAGE(TAG, "faculty", "bust retry %s", s_faculty_slug);
            }
        }
        const bool injected_button_press = !button_down && faculty175_button_just_pressed();
        if (button_short_press || injected_button_press) {
            const bool woke_from_low_power = s_low_power_asleep || s_low_power_dimmed;
            low_power_note_activity(now_ms, "button");
            if (woke_from_low_power && !s_power_on_battery) {
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
            const faculty175_face_desc_t *face = faculty175_faces_current();
            if (face != NULL && start_face_tts_read(face)) {
                FACULTY175_LOG_STAGE(TAG, "tts-face", "button read %s", face->slug);
            } else if (s_power_on_battery && s_pipeline != NULL) {
                battery_arm_button_stt(now_ms);
                sync_voice_context(NULL);
                const esp_err_t err = astrolabe_audio_pipeline_trigger_capture(s_pipeline);
                FACULTY175_LOG_STAGE(TAG, "listen", "battery button STT %s", esp_err_to_name(err));
                if (err == ESP_OK) {
                    ui_set(FACULTY175_UI_CAPTURE, "ask");
                }
            } else if (!faculty175_touch_ready() && faculty175_faces_enabled_count() > 1) {
                const faculty175_face_desc_t *from_face = face;
                face = faculty175_faces_cycle(1);
                FACULTY175_LOG_STAGE(TAG, "faces", "button fallback -> %s", face != NULL ? face->slug : "-");
                animate_face_carousel(from_face, face, 1, now_ms);
            } else if (s_faculty_ready) {
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

void app_main(void)
{
    FACULTY175_LOG_STAGE(TAG, "boot", "Astrolabe Faculty — Waveshare ESP32-S3 Touch AMOLED 1.75C");

    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        FACULTY175_LOG_STAGE_W(TAG, "boot", "NVS init failed %s; erasing NVS", esp_err_to_name(nvs_err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_err);
#if FACULTY175_USB_OTA_DEMO_BOOT
    faculty175_usb_ota_demo_boot();
    return;
#endif
    ESP_ERROR_CHECK(faculty175_storage_init());
    ESP_ERROR_CHECK(faculty175_usb_init());
    ESP_ERROR_CHECK(faculty175_device_auth_init());
    faculty175_ota_init();
    ESP_ERROR_CHECK(faculty175_apocalypso_init());
    ESP_ERROR_CHECK(faculty175_quotes_init());
    ESP_ERROR_CHECK(faculty175_rocket_init());
    faculty175_ota_maybe_boot_product();
    ESP_ERROR_CHECK(faculty175_faces_init());
    load_faculty_from_nvs();
    FACULTY175_LOG_STAGE(TAG, "boot", "faculty %s (%s)", s_faculty_name, s_faculty_slug);

    const esp_err_t faculty_init_err = faculty175_faculty_init();
    if (faculty_init_err != ESP_OK) {
        FACULTY175_LOG_STAGE_E(TAG, "faculty", "init failed: %s", esp_err_to_name(faculty_init_err));
    }

    ESP_ERROR_CHECK(faculty175_board_init());
    (void)faculty175_touch_init();
    faculty175_gesture_start_task();
    faculty175_serial_init();
    s_ui_queue = xQueueCreate(1, sizeof(faculty175_ui_msg_t));
    BaseType_t wifi_task_ok = xTaskCreateWithCaps(wifi_start_task,
                                                  "wifi_start",
                                                  8192,
                                                  NULL,
                                                  6,
                                                  NULL,
                                                  MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (wifi_task_ok != pdPASS) {
        wifi_task_ok = xTaskCreate(wifi_start_task, "wifi_start", 4096, NULL, 6, NULL);
    }
    if (wifi_task_ok != pdPASS) {
        FACULTY175_LOG_STAGE_W(TAG, "wifi", "background start task failed; staying offline");
    }
    xTaskCreate(ui_task, "ui", 12288, NULL, 4, NULL);
    xTaskCreate(input_task, "input", 12288, NULL, 5, NULL);
    xTaskCreate(button_reboot_task, "button_reboot", 3072, NULL, 7, NULL);
    ui_set(FACULTY175_UI_LISTEN, NULL);
    FACULTY175_LOG_STAGE(TAG, "boot", "board audio=%s", faculty175_board_audio_ready() ? "ok" : "off");

    if (faculty_init_err == ESP_OK) {
        s_faculty_ready = true;
        faculty175_faculty_set_ui_notify(bust_ui_refresh);
        faculty175_faculty_set_network_fetch_enabled(false);
        faculty175_faculty_request_bust(s_faculty_slug);
    }

    FACULTY175_LOG_STAGE_W(TAG, "ble", "startup disabled to preserve internal RAM");

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
        .voice_stack = 12288,
    };
    if (FACULTY175_AUDIO_PIPELINE_AUTOSTART) {
        ESP_ERROR_CHECK(astrolabe_audio_pipeline_create(&pipeline_cfg, &s_pipeline));
    } else {
        s_pipeline = NULL;
    }
    faculty175_qa_bind(&(faculty175_qa_bind_t){
        .listen = &s_listen,
        .ui = &s_ui,
        .faculty_slug = s_faculty_slug,
        .faculty_name = s_faculty_name,
        .ui_detail = s_detail,
        .set_faculty = qa_set_faculty_active,
        .fetch_faculty = qa_fetch_faculty_active,
        .trigger_stt = qa_trigger_stt,
    });
    if (FACULTY175_AUDIO_PIPELINE_AUTOSTART && s_pipeline != NULL) {
        xTaskCreate(pipeline_start_task, "audio_pipe", 4096, NULL, 4, NULL);
    } else {
        FACULTY175_LOG_STAGE_W(TAG, "pipeline", "audio capture autostart disabled for responsive instrument QA");
    }
    ui_set(FACULTY175_UI_LISTEN, NULL);
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
