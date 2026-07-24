#include "faculty175_research.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

#include "faculty175_device_auth.h"
#include "faculty175_log.h"
#include "faculty175_wifi_settings.h"

#if __has_include("secrets.local.h")
#include "secrets.local.h"
#else
#include "secrets.example.h"
#endif

#ifndef MYNAH_SUPABASE_URL
#define MYNAH_SUPABASE_URL ""
#endif

#ifndef MYNAH_SUPABASE_ANON_KEY
#define MYNAH_SUPABASE_ANON_KEY ""
#endif

#define RESEARCH_NVS_NS "luna_research"
#define RESEARCH_NVS_CONSENT "consent"
#define RESEARCH_NVS_VERSION "version"
#define RESEARCH_NVS_PENDING "pending"
#define RESEARCH_NVS_KIND "kind"
#define RESEARCH_NVS_MOOD "mood"
#define RESEARCH_NVS_AROUSAL "arousal"
#define RESEARCH_NVS_VALENCE "valence"
#define RESEARCH_NVS_EPOCH "epoch"
#define RESEARCH_NVS_FACE "face"
#define RESEARCH_NVS_RATING "rating"
#define RESEARCH_NVS_DATE "read_date"
#define RESEARCH_CONSENT_VERSION "research-v2"
#define RESEARCH_RETRY_MS (5U * 60U * 1000U)
#define RESEARCH_PAUSED_RETRY_MS (6U * 60U * 60U * 1000U)
#define RESEARCH_TASK_STACK 7168

static const char *TAG = "faculty175_research";

typedef enum {
    RESEARCH_EVENT_MOOD = 1,
    RESEARCH_EVENT_FEEDBACK = 2,
} research_event_kind_t;

typedef struct {
    bool loaded;
    bool consent;
    bool pending;
    bool task_running;
    uint8_t arousal;
    uint8_t valence;
    research_event_kind_t kind;
    int64_t epoch;
    uint32_t last_attempt_ms;
    uint32_t generation;
    faculty175_research_state_t state;
    char consent_version[24];
    char mood[16];
    char feedback_face[16];
    char feedback_rating[12];
    char reading_date[11];
} research_context_t;

typedef struct {
    char body[192];
    size_t len;
} research_http_response_t;

static research_context_t s_research;
static esp_err_t save_state(void);

static void load_state(void)
{
    if (s_research.loaded) {
        return;
    }
    s_research.loaded = true;
    strlcpy(s_research.consent_version, RESEARCH_CONSENT_VERSION,
            sizeof(s_research.consent_version));
    nvs_handle_t nvs;
    if (nvs_open(RESEARCH_NVS_NS, NVS_READONLY, &nvs) == ESP_OK) {
        uint8_t value = 0;
        if (nvs_get_u8(nvs, RESEARCH_NVS_CONSENT, &value) == ESP_OK) {
            s_research.consent = value != 0;
        }
        value = 0;
        if (nvs_get_u8(nvs, RESEARCH_NVS_PENDING, &value) == ESP_OK) {
            s_research.pending = value != 0;
        }
        value = RESEARCH_EVENT_MOOD;
        (void)nvs_get_u8(nvs, RESEARCH_NVS_KIND, &value);
        s_research.kind = (research_event_kind_t)value;
        size_t len = sizeof(s_research.consent_version);
        (void)nvs_get_str(nvs, RESEARCH_NVS_VERSION,
                          s_research.consent_version, &len);
        len = sizeof(s_research.mood);
        (void)nvs_get_str(nvs, RESEARCH_NVS_MOOD, s_research.mood, &len);
        (void)nvs_get_u8(nvs, RESEARCH_NVS_AROUSAL, &s_research.arousal);
        (void)nvs_get_u8(nvs, RESEARCH_NVS_VALENCE, &s_research.valence);
        (void)nvs_get_i64(nvs, RESEARCH_NVS_EPOCH, &s_research.epoch);
        len = sizeof(s_research.feedback_face);
        (void)nvs_get_str(nvs, RESEARCH_NVS_FACE,
                          s_research.feedback_face, &len);
        len = sizeof(s_research.feedback_rating);
        (void)nvs_get_str(nvs, RESEARCH_NVS_RATING,
                          s_research.feedback_rating, &len);
        len = sizeof(s_research.reading_date);
        (void)nvs_get_str(nvs, RESEARCH_NVS_DATE,
                          s_research.reading_date, &len);
        nvs_close(nvs);
    }
    if (strcmp(s_research.consent_version, RESEARCH_CONSENT_VERSION) != 0) {
        s_research.consent = false;
        s_research.pending = false;
        strlcpy(s_research.consent_version, RESEARCH_CONSENT_VERSION,
                sizeof(s_research.consent_version));
        (void)save_state();
    }
    if (!s_research.consent) {
        s_research.pending = false;
        s_research.state = FACULTY175_RESEARCH_OFF;
    } else {
        s_research.state = s_research.pending
            ? FACULTY175_RESEARCH_PENDING
            : FACULTY175_RESEARCH_READY;
    }
}

static esp_err_t save_state(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(RESEARCH_NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_u8(nvs, RESEARCH_NVS_CONSENT, s_research.consent ? 1 : 0);
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, RESEARCH_NVS_VERSION,
                          s_research.consent_version);
    }
    if (err == ESP_OK) {
        err = nvs_set_u8(nvs, RESEARCH_NVS_PENDING,
                         s_research.pending ? 1 : 0);
    }
    if (err == ESP_OK && s_research.pending) {
        err = nvs_set_u8(nvs, RESEARCH_NVS_KIND, (uint8_t)s_research.kind);
    }
    if (err == ESP_OK && s_research.pending &&
        s_research.kind == RESEARCH_EVENT_MOOD) {
        err = nvs_set_str(nvs, RESEARCH_NVS_MOOD, s_research.mood);
    }
    if (err == ESP_OK && s_research.pending &&
        s_research.kind == RESEARCH_EVENT_MOOD) {
        err = nvs_set_u8(nvs, RESEARCH_NVS_AROUSAL, s_research.arousal);
    }
    if (err == ESP_OK && s_research.pending &&
        s_research.kind == RESEARCH_EVENT_MOOD) {
        err = nvs_set_u8(nvs, RESEARCH_NVS_VALENCE, s_research.valence);
    }
    if (err == ESP_OK && s_research.pending) {
        err = nvs_set_i64(nvs, RESEARCH_NVS_EPOCH, s_research.epoch);
    }
    if (err == ESP_OK && s_research.pending &&
        s_research.kind == RESEARCH_EVENT_FEEDBACK) {
        err = nvs_set_str(nvs, RESEARCH_NVS_FACE, s_research.feedback_face);
    }
    if (err == ESP_OK && s_research.pending &&
        s_research.kind == RESEARCH_EVENT_FEEDBACK) {
        err = nvs_set_str(nvs, RESEARCH_NVS_RATING,
                          s_research.feedback_rating);
    }
    if (err == ESP_OK && s_research.pending &&
        s_research.kind == RESEARCH_EVENT_FEEDBACK) {
        err = nvs_set_str(nvs, RESEARCH_NVS_DATE, s_research.reading_date);
    }
    if (err == ESP_OK && !s_research.pending) {
        (void)nvs_erase_key(nvs, RESEARCH_NVS_KIND);
        (void)nvs_erase_key(nvs, RESEARCH_NVS_MOOD);
        (void)nvs_erase_key(nvs, RESEARCH_NVS_AROUSAL);
        (void)nvs_erase_key(nvs, RESEARCH_NVS_VALENCE);
        (void)nvs_erase_key(nvs, RESEARCH_NVS_EPOCH);
        (void)nvs_erase_key(nvs, RESEARCH_NVS_FACE);
        (void)nvs_erase_key(nvs, RESEARCH_NVS_RATING);
        (void)nvs_erase_key(nvs, RESEARCH_NVS_DATE);
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

static bool valid_mood(const char *mood)
{
    static const char *const moods[] = {
        "calm", "bright", "tender", "low", "tense", "energized",
    };
    if (mood == NULL) {
        return false;
    }
    for (size_t i = 0; i < sizeof(moods) / sizeof(moods[0]); ++i) {
        if (strcasecmp(mood, moods[i]) == 0) {
            return true;
        }
    }
    return false;
}

static bool value_in(const char *value,
                     const char *const *allowed,
                     size_t allowed_count)
{
    if (value == NULL) {
        return false;
    }
    for (size_t i = 0; i < allowed_count; ++i) {
        if (strcasecmp(value, allowed[i]) == 0) {
            return true;
        }
    }
    return false;
}

static bool valid_feedback(const char *face,
                           const char *rating,
                           const char *reading_date)
{
    static const char *const faces[] = {
        "moon", "astrology", "transits", "synastry", "tarot", "sky",
    };
    static const char *const ratings[] = {"helpful", "mixed", "missed"};
    if (!value_in(face, faces, sizeof(faces) / sizeof(faces[0])) ||
        !value_in(rating, ratings, sizeof(ratings) / sizeof(ratings[0])) ||
        reading_date == NULL || strlen(reading_date) != 10) {
        return false;
    }
    return reading_date[4] == '-' && reading_date[7] == '-' &&
           isdigit((unsigned char)reading_date[0]) &&
           isdigit((unsigned char)reading_date[1]) &&
           isdigit((unsigned char)reading_date[2]) &&
           isdigit((unsigned char)reading_date[3]) &&
           isdigit((unsigned char)reading_date[5]) &&
           isdigit((unsigned char)reading_date[6]) &&
           isdigit((unsigned char)reading_date[8]) &&
           isdigit((unsigned char)reading_date[9]);
}

static esp_err_t research_http_event(esp_http_client_event_t *event)
{
    research_http_response_t *response =
        event != NULL ? (research_http_response_t *)event->user_data : NULL;
    if (event == NULL || response == NULL ||
        event->event_id != HTTP_EVENT_ON_DATA || event->data_len <= 0) {
        return ESP_OK;
    }
    const size_t available = sizeof(response->body) - response->len - 1;
    const size_t copy = (size_t)event->data_len < available
        ? (size_t)event->data_len
        : available;
    if (copy > 0) {
        memcpy(response->body + response->len, event->data, copy);
        response->len += copy;
        response->body[response->len] = '\0';
    }
    return ESP_OK;
}

static bool make_endpoint(char *out, size_t cap)
{
    const char *base = MYNAH_SUPABASE_URL;
    if (out == NULL || cap == 0 || base[0] == '\0') {
        return false;
    }
    size_t len = strlen(base);
    while (len > 0 && base[len - 1] == '/') {
        --len;
    }
    const int written = snprintf(out, cap, "%.*s/functions/v1/lunasay-event",
                                 (int)len, base);
    return written > 0 && (size_t)written < cap;
}

static void research_export_task(void *arg)
{
    (void)arg;
    const uint32_t generation = s_research.generation;
    const uint8_t arousal = s_research.arousal;
    const uint8_t valence = s_research.valence;
    const research_event_kind_t kind = s_research.kind;
    const int64_t epoch = s_research.epoch;
    char consent_version[sizeof(s_research.consent_version)];
    char mood[sizeof(s_research.mood)];
    char feedback_face[sizeof(s_research.feedback_face)];
    char feedback_rating[sizeof(s_research.feedback_rating)];
    char reading_date[sizeof(s_research.reading_date)];
    strlcpy(consent_version, s_research.consent_version,
            sizeof(consent_version));
    strlcpy(mood, s_research.mood, sizeof(mood));
    strlcpy(feedback_face, s_research.feedback_face, sizeof(feedback_face));
    strlcpy(feedback_rating, s_research.feedback_rating,
            sizeof(feedback_rating));
    strlcpy(reading_date, s_research.reading_date, sizeof(reading_date));
    char url[256];
    char body[512];
    research_http_response_t response = {};
    esp_err_t err = ESP_FAIL;
    int status = 0;
    bool logged = false;

    if (!make_endpoint(url, sizeof(url)) ||
        MYNAH_SUPABASE_ANON_KEY[0] == '\0') {
        s_research.state = FACULTY175_RESEARCH_ERROR;
        goto done;
    }

    struct tm utc = {};
    time_t occurred = (time_t)epoch;
    char occurred_at[32] = "";
    if (occurred > 1704067200 && gmtime_r(&occurred, &utc) != NULL) {
        strftime(occurred_at, sizeof(occurred_at), "%Y-%m-%dT%H:%M:%SZ", &utc);
    }
    int body_len;
    if (kind == RESEARCH_EVENT_FEEDBACK) {
        body_len = snprintf(
            body,
            sizeof(body),
            "{\"event\":\"reading_feedback\",\"consent\":true,"
            "\"consentVersion\":\"%s\",\"face\":\"%s\",\"rating\":\"%s\","
            "\"readingDate\":\"%s\",\"source\":\"pwa\",\"occurredAt\":\"%s\"}",
            consent_version,
            feedback_face,
            feedback_rating,
            reading_date,
            occurred_at);
    } else {
        body_len = snprintf(
            body,
            sizeof(body),
            "{\"consent\":true,\"consentVersion\":\"%s\","
            "\"mood\":\"%s\",\"arousal\":%u,\"valence\":%u,"
            "\"source\":\"device-face\",\"occurredAt\":\"%s\"}",
            consent_version,
            mood,
            arousal,
            valence,
            occurred_at);
    }
    if (body_len <= 0 || (size_t)body_len >= sizeof(body)) {
        s_research.state = FACULTY175_RESEARCH_ERROR;
        goto done;
    }

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 15000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = research_http_event,
        .user_data = &response,
        .buffer_size = 1024,
        .buffer_size_tx = 512,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        s_research.state = FACULTY175_RESEARCH_ERROR;
        goto done;
    }
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Accept", "application/json");
    esp_http_client_set_header(client, "apikey", MYNAH_SUPABASE_ANON_KEY);
    esp_http_client_set_header(client, "Authorization",
                               "Bearer " MYNAH_SUPABASE_ANON_KEY);
    (void)faculty175_device_auth_headers(client);
    esp_http_client_set_post_field(client, body, body_len);
    err = esp_http_client_perform(client);
    status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err == ESP_OK && status >= 200 && status < 300) {
        cJSON *json = cJSON_Parse(response.body);
        const cJSON *accepted = json != NULL
            ? cJSON_GetObjectItemCaseSensitive(json, "accepted")
            : NULL;
        const cJSON *logged_item = json != NULL
            ? cJSON_GetObjectItemCaseSensitive(json, "logged")
            : NULL;
        const bool accepted_ok = cJSON_IsTrue(accepted);
        logged = cJSON_IsTrue(logged_item);
        cJSON_Delete(json);
        if (accepted_ok && logged && s_research.consent &&
            s_research.generation == generation) {
            s_research.pending = false;
            s_research.state = FACULTY175_RESEARCH_EXPORTED;
            (void)save_state();
        } else if (accepted_ok && logged && s_research.consent) {
            s_research.state = FACULTY175_RESEARCH_PENDING;
        } else if (accepted_ok && s_research.consent) {
            s_research.state = FACULTY175_RESEARCH_PAUSED;
        } else if (s_research.consent) {
            s_research.state = FACULTY175_RESEARCH_ERROR;
        }
    } else {
        s_research.state = FACULTY175_RESEARCH_ERROR;
    }

done:
    if (!s_research.consent) {
        s_research.state = FACULTY175_RESEARCH_OFF;
    } else if (s_research.pending && s_research.generation != generation) {
        s_research.state = FACULTY175_RESEARCH_PENDING;
    }
    FACULTY175_LOG_STAGE(TAG, "research",
                         "%s export status=%d err=%s logged=%s state=%s",
                         kind == RESEARCH_EVENT_FEEDBACK ? "feedback" : "mood",
                         status, esp_err_to_name(err), logged ? "yes" : "no",
                         faculty175_research_state_label(s_research.state));
    s_research.task_running = false;
    vTaskDelete(NULL);
}

void faculty175_research_init(void)
{
    load_state();
}

esp_err_t faculty175_research_set_consent(bool enabled,
                                          const char *consent_version)
{
    load_state();
    if (enabled) {
        if (consent_version == NULL ||
            strcmp(consent_version, RESEARCH_CONSENT_VERSION) != 0) {
            return ESP_ERR_INVALID_ARG;
        }
        s_research.consent = true;
        strlcpy(s_research.consent_version, consent_version,
                sizeof(s_research.consent_version));
        s_research.state = s_research.pending
            ? FACULTY175_RESEARCH_PENDING
            : FACULTY175_RESEARCH_READY;
    } else {
        s_research.consent = false;
        s_research.pending = false;
        s_research.state = FACULTY175_RESEARCH_OFF;
    }
    return save_state();
}

bool faculty175_research_consent_enabled(void)
{
    load_state();
    return s_research.consent;
}

esp_err_t faculty175_research_record_mood(const char *mood,
                                          uint8_t arousal,
                                          uint8_t valence)
{
    load_state();
    if (!s_research.consent) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_research.pending) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!valid_mood(mood) || arousal > 100 || valence > 100) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t len = strlen(mood);
    if (len >= sizeof(s_research.mood)) {
        return ESP_ERR_INVALID_SIZE;
    }
    for (size_t i = 0; i <= len; ++i) {
        s_research.mood[i] = (char)tolower((unsigned char)mood[i]);
    }
    s_research.arousal = arousal;
    s_research.valence = valence;
    s_research.epoch = (int64_t)time(NULL);
    s_research.kind = RESEARCH_EVENT_MOOD;
    ++s_research.generation;
    s_research.pending = true;
    s_research.state = FACULTY175_RESEARCH_PENDING;
    const esp_err_t err = save_state();
    if (err == ESP_OK) {
        s_research.last_attempt_ms = 0;
        faculty175_research_poll();
    }
    return err;
}

esp_err_t faculty175_research_record_feedback(const char *face,
                                              const char *rating,
                                              const char *reading_date)
{
    load_state();
    if (!s_research.consent || s_research.pending) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!valid_feedback(face, rating, reading_date)) {
        return ESP_ERR_INVALID_ARG;
    }
    strlcpy(s_research.feedback_face, face, sizeof(s_research.feedback_face));
    strlcpy(s_research.feedback_rating, rating,
            sizeof(s_research.feedback_rating));
    strlcpy(s_research.reading_date, reading_date,
            sizeof(s_research.reading_date));
    for (char *p = s_research.feedback_face; *p != '\0'; ++p) {
        *p = (char)tolower((unsigned char)*p);
    }
    for (char *p = s_research.feedback_rating; *p != '\0'; ++p) {
        *p = (char)tolower((unsigned char)*p);
    }
    s_research.epoch = (int64_t)time(NULL);
    s_research.kind = RESEARCH_EVENT_FEEDBACK;
    ++s_research.generation;
    s_research.pending = true;
    s_research.state = FACULTY175_RESEARCH_PENDING;
    const esp_err_t err = save_state();
    if (err == ESP_OK) {
        s_research.last_attempt_ms = 0;
        faculty175_research_poll();
    }
    return err;
}

void faculty175_research_poll(void)
{
    load_state();
    if (!s_research.consent || !s_research.pending ||
        s_research.task_running ||
        !faculty175_wifi_settings_sta_connected()) {
        return;
    }
    const uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    const uint32_t retry_ms = s_research.state == FACULTY175_RESEARCH_PAUSED
        ? RESEARCH_PAUSED_RETRY_MS
        : RESEARCH_RETRY_MS;
    if (s_research.last_attempt_ms != 0 &&
        now - s_research.last_attempt_ms < retry_ms) {
        return;
    }
    s_research.last_attempt_ms = now;
    s_research.task_running = true;
    BaseType_t created = xTaskCreateWithCaps(
        research_export_task,
        "research_export",
        RESEARCH_TASK_STACK,
        NULL,
        2,
        NULL,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (created != pdPASS) {
        s_research.task_running = false;
        s_research.state = FACULTY175_RESEARCH_ERROR;
    }
}

void faculty175_research_status(faculty175_research_status_t *out)
{
    if (out == NULL) {
        return;
    }
    load_state();
    memset(out, 0, sizeof(*out));
    out->consent_enabled = s_research.consent;
    out->pending = s_research.pending;
    out->state = s_research.state;
    strlcpy(out->consent_version, s_research.consent_version,
            sizeof(out->consent_version));
    strlcpy(out->last_mood, s_research.mood, sizeof(out->last_mood));
    strlcpy(out->last_feedback_face, s_research.feedback_face,
            sizeof(out->last_feedback_face));
    strlcpy(out->last_feedback_rating, s_research.feedback_rating,
            sizeof(out->last_feedback_rating));
}

const char *faculty175_research_state_label(faculty175_research_state_t state)
{
    switch (state) {
        case FACULTY175_RESEARCH_OFF: return "local only";
        case FACULTY175_RESEARCH_READY: return "ready";
        case FACULTY175_RESEARCH_PENDING: return "queued";
        case FACULTY175_RESEARCH_EXPORTED: return "exported";
        case FACULTY175_RESEARCH_PAUSED: return "server paused";
        case FACULTY175_RESEARCH_ERROR: return "retrying";
        default: return "unknown";
    }
}
