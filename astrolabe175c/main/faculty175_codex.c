#include "faculty175_codex.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "nvs.h"

static const char *TAG = "faculty175_codex";

#define CODEX_NVS_NAMESPACE "codex"
#define CODEX_NVS_PINS_KEY "pins"
#define CODEX_NVS_EMOJI_KEY "emoji"
#define CODEX_PIN_SCHEMA 1u
#define CODEX_EMOJI_SCHEMA 1u

typedef struct {
    uint32_t schema;
    uint8_t count;
    char ids[FACULTY175_CODEX_TASK_MAX][FACULTY175_CODEX_ID_MAX];
} codex_pin_blob_t;

typedef struct {
    char id[FACULTY175_CODEX_ID_MAX];
    uint8_t emoji_index;
} codex_emoji_override_t;

typedef struct {
    uint32_t schema;
    uint8_t count;
    codex_emoji_override_t entries[FACULTY175_CODEX_TASK_MAX];
} codex_emoji_blob_t;

static faculty175_codex_state_t s_state;
static codex_pin_blob_t s_pins = {.schema = CODEX_PIN_SCHEMA};
static codex_emoji_blob_t s_emojis = {.schema = CODEX_EMOJI_SCHEMA};
static bool s_task_settings;
static uint8_t s_model_choice;
static uint8_t s_context_choice;
static uint8_t s_speed_choice;
static const char *const s_models[] = {"SOL", "TERRA", "LUNA", "AUTO"};
static const char *const s_contexts[] = {"BALANCED", "SOURCES", "FULL THREAD", "MINIMAL"};
static const char *const s_speeds[] = {"ADAPTIVE", "FAST", "THOROUGH"};
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static void copy_text(char *dst, size_t cap, const char *src)
{
    if (dst == NULL || cap == 0) {
        return;
    }
    snprintf(dst, cap, "%s", src != NULL ? src : "");
}

static bool valid_id(const char *value)
{
    if (value == NULL || value[0] == '\0') {
        return false;
    }
    const size_t len = strnlen(value, FACULTY175_CODEX_ID_MAX);
    if (len == 0 || len >= FACULTY175_CODEX_ID_MAX) {
        return false;
    }
    for (size_t i = 0; i < len; ++i) {
        const unsigned char c = (unsigned char)value[i];
        if (c < 0x20 || c == 0x7f) {
            return false;
        }
    }
    return true;
}

static int pin_index(const char *id)
{
    for (size_t i = 0; i < s_pins.count && i < FACULTY175_CODEX_TASK_MAX; ++i) {
        if (strcmp(s_pins.ids[i], id) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int emoji_override_index(const char *id)
{
    for (size_t i = 0; i < s_emojis.count && i < FACULTY175_CODEX_TASK_MAX; ++i) {
        if (strcmp(s_emojis.entries[i].id, id) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static esp_err_t save_pins(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(CODEX_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_blob(nvs, CODEX_NVS_PINS_KEY, &s_pins, sizeof(s_pins));
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

static esp_err_t save_emojis(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(CODEX_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(nvs, CODEX_NVS_EMOJI_KEY, &s_emojis, sizeof(s_emojis));
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);
    return err;
}

static int task_compare(const void *lhs, const void *rhs)
{
    const faculty175_codex_task_t *a = lhs;
    const faculty175_codex_task_t *b = rhs;
    const int a_pin = pin_index(a->id);
    const int b_pin = pin_index(b->id);
    if ((a_pin >= 0) != (b_pin >= 0)) {
        return a_pin >= 0 ? -1 : 1;
    }
    if (a_pin >= 0 && b_pin >= 0 && a_pin != b_pin) {
        return a_pin - b_pin;
    }
    if (a->updated_at != b->updated_at) {
        return a->updated_at > b->updated_at ? -1 : 1;
    }
    return strcmp(a->id, b->id);
}

static faculty175_codex_task_status_t parse_status(const char *status)
{
    if (status == NULL) return FACULTY175_CODEX_TASK_IDLE;
    if (strcmp(status, "running") == 0) return FACULTY175_CODEX_TASK_RUNNING;
    if (strcmp(status, "waiting-approval") == 0) return FACULTY175_CODEX_TASK_WAITING_APPROVAL;
    if (strcmp(status, "waiting-input") == 0) return FACULTY175_CODEX_TASK_WAITING_INPUT;
    if (strcmp(status, "done") == 0) return FACULTY175_CODEX_TASK_DONE;
    if (strcmp(status, "error") == 0) return FACULTY175_CODEX_TASK_ERROR;
    return FACULTY175_CODEX_TASK_IDLE;
}

const char *faculty175_codex_status_label(faculty175_codex_task_status_t status)
{
    switch (status) {
        case FACULTY175_CODEX_TASK_RUNNING: return "RUNNING";
        case FACULTY175_CODEX_TASK_WAITING_APPROVAL: return "APPROVAL";
        case FACULTY175_CODEX_TASK_WAITING_INPUT: return "INPUT";
        case FACULTY175_CODEX_TASK_DONE: return "DONE";
        case FACULTY175_CODEX_TASK_ERROR: return "ERROR";
        case FACULTY175_CODEX_TASK_IDLE:
        default: return "IDLE";
    }
}

static const char *status_wire_value(faculty175_codex_task_status_t status)
{
    switch (status) {
        case FACULTY175_CODEX_TASK_RUNNING: return "running";
        case FACULTY175_CODEX_TASK_WAITING_APPROVAL: return "waiting-approval";
        case FACULTY175_CODEX_TASK_WAITING_INPUT: return "waiting-input";
        case FACULTY175_CODEX_TASK_DONE: return "done";
        case FACULTY175_CODEX_TASK_ERROR: return "error";
        case FACULTY175_CODEX_TASK_IDLE:
        default: return "idle";
    }
}

esp_err_t faculty175_codex_init(void)
{
    codex_pin_blob_t pins = {.schema = CODEX_PIN_SCHEMA};
    codex_emoji_blob_t emojis = {.schema = CODEX_EMOJI_SCHEMA};
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(CODEX_NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err == ESP_OK) {
        size_t len = sizeof(pins);
        err = nvs_get_blob(nvs, CODEX_NVS_PINS_KEY, &pins, &len);
        nvs_close(nvs);
        if (err == ESP_OK && len == sizeof(pins) && pins.schema == CODEX_PIN_SCHEMA &&
            pins.count <= FACULTY175_CODEX_TASK_MAX) {
            s_pins = pins;
        } else if (err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "pin load ignored: %s", esp_err_to_name(err));
        }
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "pin namespace unavailable: %s", esp_err_to_name(err));
    }
    err = nvs_open(CODEX_NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err == ESP_OK) {
        size_t len = sizeof(emojis);
        err = nvs_get_blob(nvs, CODEX_NVS_EMOJI_KEY, &emojis, &len);
        nvs_close(nvs);
        if (err == ESP_OK && len == sizeof(emojis) && emojis.schema == CODEX_EMOJI_SCHEMA &&
            emojis.count <= FACULTY175_CODEX_TASK_MAX) {
            bool valid = true;
            for (size_t i = 0; i < emojis.count; ++i) {
                valid = valid && valid_id(emojis.entries[i].id) &&
                    emojis.entries[i].emoji_index < FACULTY175_CODEX_EMOJI_COUNT;
            }
            if (valid) s_emojis = emojis;
            else ESP_LOGW(TAG, "emoji overrides ignored: invalid entry");
        } else if (err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "emoji override load ignored: %s", esp_err_to_name(err));
        }
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "emoji override namespace unavailable: %s", esp_err_to_name(err));
    }
    portENTER_CRITICAL(&s_lock);
    memset(&s_state, 0, sizeof(s_state));
    s_state.revision = 1;
    portEXIT_CRITICAL(&s_lock);
    return ESP_OK;
}

void faculty175_codex_get(faculty175_codex_state_t *out)
{
    if (out == NULL) return;
    portENTER_CRITICAL(&s_lock);
    *out = s_state;
    portEXIT_CRITICAL(&s_lock);
}

esp_err_t faculty175_codex_apply_json(const cJSON *root)
{
    if (!cJSON_IsObject(root)) return ESP_ERR_INVALID_ARG;
    const cJSON *hosts = cJSON_GetObjectItemCaseSensitive(root, "hosts");
    const cJSON *tasks = cJSON_GetObjectItemCaseSensitive(root, "tasks");
    if (!cJSON_IsArray(hosts) || !cJSON_IsArray(tasks) ||
        cJSON_GetArraySize(hosts) > FACULTY175_CODEX_HOST_MAX ||
        cJSON_GetArraySize(tasks) > FACULTY175_CODEX_TASK_MAX) {
        return ESP_ERR_INVALID_SIZE;
    }

    faculty175_codex_state_t previous;
    faculty175_codex_get(&previous);
    faculty175_codex_state_t next = {};
    const cJSON *replace_host = cJSON_GetObjectItemCaseSensitive(root, "replaceHostId");
    if (cJSON_IsString(replace_host)) {
        if (!valid_id(replace_host->valuestring)) return ESP_ERR_INVALID_ARG;
        for (size_t i = 0; i < previous.host_count; ++i) {
            if (strcmp(previous.hosts[i].id, replace_host->valuestring) != 0 &&
                next.host_count < FACULTY175_CODEX_HOST_MAX) {
                next.hosts[next.host_count++] = previous.hosts[i];
            }
        }
        for (size_t i = 0; i < previous.task_count; ++i) {
            if (strcmp(previous.tasks[i].host_id, replace_host->valuestring) != 0 &&
                next.task_count < FACULTY175_CODEX_TASK_MAX) {
                next.tasks[next.task_count++] = previous.tasks[i];
            }
        }
    }
    const cJSON *item;
    cJSON_ArrayForEach(item, hosts) {
        const cJSON *id = cJSON_GetObjectItemCaseSensitive(item, "id");
        const cJSON *name = cJSON_GetObjectItemCaseSensitive(item, "name");
        const cJSON *online = cJSON_GetObjectItemCaseSensitive(item, "online");
        if (!cJSON_IsString(id) || !valid_id(id->valuestring) || !cJSON_IsString(name)) {
            return ESP_ERR_INVALID_ARG;
        }
        if (cJSON_IsString(replace_host) && strcmp(id->valuestring, replace_host->valuestring) != 0) {
            return ESP_ERR_INVALID_ARG;
        }
        if (next.host_count >= FACULTY175_CODEX_HOST_MAX) return ESP_ERR_INVALID_SIZE;
        faculty175_codex_host_t *host = &next.hosts[next.host_count++];
        copy_text(host->id, sizeof(host->id), id->valuestring);
        copy_text(host->name, sizeof(host->name), name->valuestring);
        host->online = cJSON_IsTrue(online);
    }
    cJSON_ArrayForEach(item, tasks) {
        const cJSON *id = cJSON_GetObjectItemCaseSensitive(item, "id");
        const cJSON *host_id = cJSON_GetObjectItemCaseSensitive(item, "hostId");
        const cJSON *title = cJSON_GetObjectItemCaseSensitive(item, "title");
        const cJSON *status = cJSON_GetObjectItemCaseSensitive(item, "status");
        const cJSON *updated = cJSON_GetObjectItemCaseSensitive(item, "updatedAt");
        const cJSON *emoji = cJSON_GetObjectItemCaseSensitive(item, "emojiIndex");
        const cJSON *model = cJSON_GetObjectItemCaseSensitive(item, "model");
        const cJSON *speed = cJSON_GetObjectItemCaseSensitive(item, "speed");
        const cJSON *usage = cJSON_GetObjectItemCaseSensitive(item, "usage");
        if (!cJSON_IsString(id) || !valid_id(id->valuestring) ||
            !cJSON_IsString(host_id) || !valid_id(host_id->valuestring) || !cJSON_IsString(title)) {
            return ESP_ERR_INVALID_ARG;
        }
        if (cJSON_IsString(replace_host) && strcmp(host_id->valuestring, replace_host->valuestring) != 0) {
            return ESP_ERR_INVALID_ARG;
        }
        if (next.task_count >= FACULTY175_CODEX_TASK_MAX) return ESP_ERR_INVALID_SIZE;
        faculty175_codex_task_t *task = &next.tasks[next.task_count++];
        copy_text(task->id, sizeof(task->id), id->valuestring);
        copy_text(task->host_id, sizeof(task->host_id), host_id->valuestring);
        copy_text(task->title, sizeof(task->title), title->valuestring);
        copy_text(task->model, sizeof(task->model), cJSON_IsString(model) ? model->valuestring : "TERRA");
        copy_text(task->speed, sizeof(task->speed), cJSON_IsString(speed) ? speed->valuestring : "ADAPTIVE");
        task->status = parse_status(cJSON_IsString(status) ? status->valuestring : NULL);
        task->updated_at = cJSON_IsNumber(updated) && updated->valuedouble > 0
            ? (uint64_t)updated->valuedouble : 0;
        const uint8_t suggested_emoji = cJSON_IsNumber(emoji) && emoji->valueint >= 0 &&
            emoji->valueint < FACULTY175_CODEX_EMOJI_COUNT
            ? (uint8_t)emoji->valueint : FACULTY175_CODEX_EMOJI_ROBOT;
        const int override = emoji_override_index(task->id);
        task->emoji_overridden = override >= 0;
        task->emoji_index = override >= 0 ? s_emojis.entries[override].emoji_index : suggested_emoji;
        task->pinned = pin_index(task->id) >= 0;
        task->usage_available = false;
        task->input_tokens = task->cached_input_tokens = task->output_tokens = 0;
        task->reasoning_output_tokens = task->total_tokens = 0;
        task->token_rate_per_minute = 0;
        if (cJSON_IsObject(usage)) {
            const cJSON *available = cJSON_GetObjectItemCaseSensitive(usage, "available");
            task->usage_available = cJSON_IsTrue(available);
            const cJSON *field;
            field = cJSON_GetObjectItemCaseSensitive(usage, "inputTokens");
            if (cJSON_IsNumber(field) && field->valuedouble >= 0) task->input_tokens = (uint64_t)field->valuedouble;
            field = cJSON_GetObjectItemCaseSensitive(usage, "cachedInputTokens");
            if (cJSON_IsNumber(field) && field->valuedouble >= 0) task->cached_input_tokens = (uint64_t)field->valuedouble;
            field = cJSON_GetObjectItemCaseSensitive(usage, "outputTokens");
            if (cJSON_IsNumber(field) && field->valuedouble >= 0) task->output_tokens = (uint64_t)field->valuedouble;
            field = cJSON_GetObjectItemCaseSensitive(usage, "reasoningOutputTokens");
            if (cJSON_IsNumber(field) && field->valuedouble >= 0) task->reasoning_output_tokens = (uint64_t)field->valuedouble;
            field = cJSON_GetObjectItemCaseSensitive(usage, "totalTokens");
            if (cJSON_IsNumber(field) && field->valuedouble >= 0) task->total_tokens = (uint64_t)field->valuedouble;
            field = cJSON_GetObjectItemCaseSensitive(usage, "rateTokensPerMinute");
            if (cJSON_IsNumber(field) && field->valuedouble >= 0) task->token_rate_per_minute = (uint32_t)field->valuedouble;
        }
    }
    qsort(next.tasks, next.task_count, sizeof(next.tasks[0]), task_compare);

    next.revision = previous.revision + 1;
    next.action_sequence = previous.action_sequence;
    const cJSON *ack = cJSON_GetObjectItemCaseSensitive(root, "ackActionSequence");
    const bool acknowledged = cJSON_IsNumber(ack) && ack->valuedouble >= previous.action_sequence;
    if (!acknowledged) {
        copy_text(next.pending_action, sizeof(next.pending_action), previous.pending_action);
        copy_text(next.pending_task_id, sizeof(next.pending_task_id), previous.pending_task_id);
        copy_text(next.pending_input, sizeof(next.pending_input), previous.pending_input);
    }
    if (previous.task_count > 0 && previous.selected_index < previous.task_count) {
        const char *selected_id = previous.tasks[previous.selected_index].id;
        for (size_t i = 0; i < next.task_count; ++i) {
            if (strcmp(next.tasks[i].id, selected_id) == 0) {
                next.selected_index = i;
                break;
            }
        }
    }
    portENTER_CRITICAL(&s_lock);
    s_state = next;
    portEXIT_CRITICAL(&s_lock);
    return ESP_OK;
}

esp_err_t faculty175_codex_to_json(cJSON *root)
{
    if (!cJSON_IsObject(root)) return ESP_ERR_INVALID_ARG;
    faculty175_codex_state_t state;
    faculty175_codex_get(&state);
    cJSON_AddNumberToObject(root, "revision", state.revision);
    cJSON_AddNumberToObject(root, "selectedIndex", state.selected_index);
    cJSON *hosts = cJSON_AddArrayToObject(root, "hosts");
    cJSON *tasks = cJSON_AddArrayToObject(root, "tasks");
    if (hosts == NULL || tasks == NULL) return ESP_ERR_NO_MEM;
    for (size_t i = 0; i < state.host_count; ++i) {
        cJSON *host = cJSON_CreateObject();
        if (host == NULL || !cJSON_AddItemToArray(hosts, host)) return ESP_ERR_NO_MEM;
        cJSON_AddStringToObject(host, "id", state.hosts[i].id);
        cJSON_AddStringToObject(host, "name", state.hosts[i].name);
        cJSON_AddBoolToObject(host, "online", state.hosts[i].online);
    }
    for (size_t i = 0; i < state.task_count; ++i) {
        cJSON *task = cJSON_CreateObject();
        if (task == NULL || !cJSON_AddItemToArray(tasks, task)) return ESP_ERR_NO_MEM;
        cJSON_AddStringToObject(task, "id", state.tasks[i].id);
        cJSON_AddStringToObject(task, "hostId", state.tasks[i].host_id);
        cJSON_AddStringToObject(task, "title", state.tasks[i].title);
        cJSON_AddStringToObject(task, "model", state.tasks[i].model);
        cJSON_AddStringToObject(task, "speed", state.tasks[i].speed);
        cJSON_AddStringToObject(task, "status", status_wire_value(state.tasks[i].status));
        cJSON_AddNumberToObject(task, "updatedAt", (double)state.tasks[i].updated_at);
        cJSON_AddBoolToObject(task, "pinned", state.tasks[i].pinned);
        cJSON_AddNumberToObject(task, "emojiIndex", state.tasks[i].emoji_index);
        cJSON_AddBoolToObject(task, "emojiOverridden", state.tasks[i].emoji_overridden);
        cJSON *usage = cJSON_AddObjectToObject(task, "usage");
        if (usage == NULL) return ESP_ERR_NO_MEM;
        cJSON_AddBoolToObject(usage, "available", state.tasks[i].usage_available);
        cJSON_AddNumberToObject(usage, "inputTokens", (double)state.tasks[i].input_tokens);
        cJSON_AddNumberToObject(usage, "cachedInputTokens", (double)state.tasks[i].cached_input_tokens);
        cJSON_AddNumberToObject(usage, "outputTokens", (double)state.tasks[i].output_tokens);
        cJSON_AddNumberToObject(usage, "reasoningOutputTokens", (double)state.tasks[i].reasoning_output_tokens);
        cJSON_AddNumberToObject(usage, "totalTokens", (double)state.tasks[i].total_tokens);
        cJSON_AddNumberToObject(usage, "rateTokensPerMinute", state.tasks[i].token_rate_per_minute);
    }
    cJSON *pending = cJSON_AddObjectToObject(root, "pendingAction");
    if (pending == NULL) return ESP_ERR_NO_MEM;
    cJSON_AddNumberToObject(pending, "sequence", state.action_sequence);
    cJSON_AddStringToObject(pending, "action", state.pending_action);
    cJSON_AddStringToObject(pending, "taskId", state.pending_task_id);
    cJSON_AddStringToObject(pending, "input", state.pending_input);
    return ESP_OK;
}

bool faculty175_codex_select_delta(int delta)
{
    bool changed = false;
    portENTER_CRITICAL(&s_lock);
    if (s_state.task_count > 0 && delta != 0) {
        const int count = (int)s_state.task_count;
        int next = ((int)s_state.selected_index + (delta > 0 ? 1 : -1) + count) % count;
        changed = (size_t)next != s_state.selected_index;
        s_state.selected_index = (size_t)next;
        if (changed) ++s_state.revision;
    }
    portEXIT_CRITICAL(&s_lock);
    return changed;
}

bool faculty175_codex_select_id(const char *task_id)
{
    if (!valid_id(task_id)) return false;
    bool found = false;
    portENTER_CRITICAL(&s_lock);
    for (size_t i = 0; i < s_state.task_count; ++i) {
        if (strcmp(s_state.tasks[i].id, task_id) == 0) {
            const bool changed = i != s_state.selected_index;
            s_state.selected_index = i;
            if (changed) ++s_state.revision;
            found = true;
            break;
        }
    }
    portEXIT_CRITICAL(&s_lock);
    return found;
}

bool faculty175_codex_select_dial_at(int16_t x, int16_t y)
{
    faculty175_codex_state_t state;
    faculty175_codex_get(&state);
    if (state.task_count == 0) return false;

    const float pi = 3.14159265358979323846f;
    const float step = 2.0f * pi / (float)state.task_count;
    const int32_t hit_radius_sq = FACULTY175_CODEX_DIAL_HIT_RADIUS * FACULTY175_CODEX_DIAL_HIT_RADIUS;
    size_t nearest = 0;
    int32_t nearest_distance_sq = INT32_MAX;
    for (size_t i = 0; i < state.task_count; ++i) {
        const float angle = -0.5f * pi + step * (float)i;
        const int32_t icon_x = FACULTY175_CODEX_DIAL_CX +
            (int32_t)lrintf(cosf(angle) * FACULTY175_CODEX_DIAL_RADIUS);
        const int32_t icon_y = FACULTY175_CODEX_DIAL_CY +
            (int32_t)lrintf(sinf(angle) * FACULTY175_CODEX_DIAL_RADIUS);
        const int32_t dx = (int32_t)x - icon_x;
        const int32_t dy = (int32_t)y - icon_y;
        const int32_t distance_sq = dx * dx + dy * dy;
        if (distance_sq < nearest_distance_sq) {
            nearest = i;
            nearest_distance_sq = distance_sq;
        }
    }
    if (nearest_distance_sq > hit_radius_sq) return false;
    return faculty175_codex_select_id(state.tasks[nearest].id);
}

bool faculty175_codex_toggle_pin(void)
{
    faculty175_codex_state_t state;
    faculty175_codex_get(&state);
    if (state.task_count == 0 || state.selected_index >= state.task_count) return false;
    return faculty175_codex_toggle_pin_id(state.tasks[state.selected_index].id);
}

bool faculty175_codex_toggle_pin_id(const char *task_id)
{
    if (!valid_id(task_id)) return false;
    faculty175_codex_state_t state;
    faculty175_codex_get(&state);
    bool found = false;
    for (size_t i = 0; i < state.task_count; ++i) {
        if (strcmp(state.tasks[i].id, task_id) == 0) {
            found = true;
            break;
        }
    }
    if (!found) return false;
    char id[FACULTY175_CODEX_ID_MAX];
    char selected_id[FACULTY175_CODEX_ID_MAX];
    copy_text(id, sizeof(id), task_id);
    copy_text(selected_id, sizeof(selected_id),
              state.selected_index < state.task_count ? state.tasks[state.selected_index].id : task_id);
    const int existing = pin_index(id);
    if (existing >= 0) {
        for (size_t i = (size_t)existing; i + 1 < s_pins.count; ++i) {
            copy_text(s_pins.ids[i], sizeof(s_pins.ids[i]), s_pins.ids[i + 1]);
        }
        if (s_pins.count > 0) --s_pins.count;
    } else {
        if (s_pins.count >= FACULTY175_CODEX_TASK_MAX) return false;
        copy_text(s_pins.ids[s_pins.count++], sizeof(s_pins.ids[0]), id);
    }
    if (save_pins() != ESP_OK) return false;
    for (size_t i = 0; i < state.task_count; ++i) state.tasks[i].pinned = pin_index(state.tasks[i].id) >= 0;
    qsort(state.tasks, state.task_count, sizeof(state.tasks[0]), task_compare);
    for (size_t i = 0; i < state.task_count; ++i) {
        if (strcmp(state.tasks[i].id, selected_id) == 0) {
            state.selected_index = i;
            break;
        }
    }
    ++state.revision;
    portENTER_CRITICAL(&s_lock);
    s_state = state;
    portEXIT_CRITICAL(&s_lock);
    return true;
}

bool faculty175_codex_set_emoji(const char *task_id, int emoji_index)
{
    if (!valid_id(task_id) || emoji_index < -1 || emoji_index >= FACULTY175_CODEX_EMOJI_COUNT) {
        return false;
    }
    faculty175_codex_state_t state;
    faculty175_codex_get(&state);
    size_t task_index = state.task_count;
    for (size_t i = 0; i < state.task_count; ++i) {
        if (strcmp(state.tasks[i].id, task_id) == 0) {
            task_index = i;
            break;
        }
    }
    if (task_index >= state.task_count) return false;

    const int existing = emoji_override_index(task_id);
    if (emoji_index < 0) {
        if (existing >= 0) {
            for (size_t i = (size_t)existing; i + 1 < s_emojis.count; ++i) {
                s_emojis.entries[i] = s_emojis.entries[i + 1];
            }
            --s_emojis.count;
        }
        state.tasks[task_index].emoji_overridden = false;
    } else {
        int index = existing;
        if (index < 0) {
            if (s_emojis.count >= FACULTY175_CODEX_TASK_MAX) return false;
            index = (int)s_emojis.count++;
            copy_text(s_emojis.entries[index].id, sizeof(s_emojis.entries[index].id), task_id);
        }
        s_emojis.entries[index].emoji_index = (uint8_t)emoji_index;
        state.tasks[task_index].emoji_index = (uint8_t)emoji_index;
        state.tasks[task_index].emoji_overridden = true;
    }
    if (save_emojis() != ESP_OK) return false;
    ++state.revision;
    portENTER_CRITICAL(&s_lock);
    s_state = state;
    portEXIT_CRITICAL(&s_lock);
    return true;
}

bool faculty175_codex_queue_action(const char *action)
{
    if (action == NULL || (strcmp(action, "open") != 0 && strcmp(action, "interrupt") != 0 &&
                           strcmp(action, "approve") != 0 && strcmp(action, "voice") != 0)) {
        return false;
    }
    bool queued = false;
    portENTER_CRITICAL(&s_lock);
    if (s_state.task_count > 0 && s_state.selected_index < s_state.task_count) {
        copy_text(s_state.pending_action, sizeof(s_state.pending_action), action);
        copy_text(s_state.pending_task_id, sizeof(s_state.pending_task_id),
                  s_state.tasks[s_state.selected_index].id);
        s_state.pending_input[0] = '\0';
        ++s_state.action_sequence;
        ++s_state.revision;
        queued = true;
    }
    portEXIT_CRITICAL(&s_lock);
    return queued;
}

bool faculty175_codex_queue_prompt(const char *input)
{
    if (input == NULL || input[0] == '\0' || strnlen(input, FACULTY175_CODEX_INPUT_MAX) >= FACULTY175_CODEX_INPUT_MAX) {
        return false;
    }
    bool queued = false;
    portENTER_CRITICAL(&s_lock);
    if (s_state.task_count > 0 && s_state.selected_index < s_state.task_count) {
        copy_text(s_state.pending_action, sizeof(s_state.pending_action), "prompt");
        copy_text(s_state.pending_task_id, sizeof(s_state.pending_task_id),
                  s_state.tasks[s_state.selected_index].id);
        copy_text(s_state.pending_input, sizeof(s_state.pending_input), input);
        ++s_state.action_sequence;
        ++s_state.revision;
        queued = true;
    }
    portEXIT_CRITICAL(&s_lock);
    return queued;
}

bool faculty175_codex_task_settings_open(void)
{
    faculty175_codex_state_t state;
    faculty175_codex_get(&state);
    if (state.task_count == 0 || state.selected_index >= state.task_count) return false;
    s_task_settings = true;
    return true;
}

void faculty175_codex_task_settings_close(void) { s_task_settings = false; }
bool faculty175_codex_task_settings_adjust(int section, int delta)
{
    uint8_t *value = section == 0 ? &s_model_choice : section == 1 ? &s_context_choice : &s_speed_choice;
    const int count = section == 2 ? 3 : 4;
    if (section < 0 || section > 2 || delta == 0) return false;
    int next = ((int)*value + (delta > 0 ? 1 : -1) + count) % count;
    if (next == *value) return false;
    *value = (uint8_t)next;
    return true;
}
bool faculty175_codex_task_settings_is_open(void) { return s_task_settings; }
int faculty175_codex_task_settings_value(int section)
{
    return section == 0 ? s_model_choice : section == 1 ? s_context_choice : s_speed_choice;
}
const char *faculty175_codex_task_settings_label(int section)
{
    if (section == 0) return s_models[s_model_choice];
    if (section == 1) return s_contexts[s_context_choice];
    return s_speeds[s_speed_choice];
}
