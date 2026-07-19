#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cJSON.h"
#include "esp_err.h"
#include "faculty175_codex_emoji.h"

#define FACULTY175_CODEX_HOST_MAX 4
#define FACULTY175_CODEX_TASK_MAX 12
#define FACULTY175_CODEX_ID_MAX 48
#define FACULTY175_CODEX_TITLE_MAX 64
#define FACULTY175_CODEX_MODEL_MAX 16
#define FACULTY175_CODEX_SPEED_MAX 16
#define FACULTY175_CODEX_HOST_NAME_MAX 28
#define FACULTY175_CODEX_INPUT_MAX 192
#define FACULTY175_CODEX_DIAL_CX 233
#define FACULTY175_CODEX_DIAL_CY 236
#define FACULTY175_CODEX_DIAL_RADIUS 188
#define FACULTY175_CODEX_DIAL_HIT_RADIUS 34
#define FACULTY175_CODEX_SEGMENT_COUNT 12

typedef enum {
    FACULTY175_CODEX_TASK_IDLE = 0,
    FACULTY175_CODEX_TASK_RUNNING,
    FACULTY175_CODEX_TASK_WAITING_APPROVAL,
    FACULTY175_CODEX_TASK_WAITING_INPUT,
    FACULTY175_CODEX_TASK_DONE,
    FACULTY175_CODEX_TASK_ERROR,
} faculty175_codex_task_status_t;

typedef struct {
    char id[FACULTY175_CODEX_ID_MAX];
    char name[FACULTY175_CODEX_HOST_NAME_MAX];
    bool online;
} faculty175_codex_host_t;

typedef struct {
    char id[FACULTY175_CODEX_ID_MAX];
    char host_id[FACULTY175_CODEX_ID_MAX];
    char title[FACULTY175_CODEX_TITLE_MAX];
    char model[FACULTY175_CODEX_MODEL_MAX];
    char speed[FACULTY175_CODEX_SPEED_MAX];
    faculty175_codex_task_status_t status;
    uint64_t updated_at;
    bool pinned;
    bool emoji_overridden;
    uint8_t emoji_index;
    bool usage_available;
    uint64_t input_tokens;
    uint64_t cached_input_tokens;
    uint64_t output_tokens;
    uint64_t reasoning_output_tokens;
    uint64_t total_tokens;
    uint32_t token_rate_per_minute;
} faculty175_codex_task_t;

typedef struct {
    faculty175_codex_host_t hosts[FACULTY175_CODEX_HOST_MAX];
    faculty175_codex_task_t tasks[FACULTY175_CODEX_TASK_MAX];
    size_t host_count;
    size_t task_count;
    size_t selected_index;
    uint32_t revision;
    uint32_t action_sequence;
    char pending_action[20];
    char pending_task_id[FACULTY175_CODEX_ID_MAX];
    char pending_input[FACULTY175_CODEX_INPUT_MAX];
} faculty175_codex_state_t;

esp_err_t faculty175_codex_init(void);
void faculty175_codex_get(faculty175_codex_state_t *out);
esp_err_t faculty175_codex_apply_json(const cJSON *root);
esp_err_t faculty175_codex_to_json(cJSON *root);
bool faculty175_codex_select_delta(int delta);
bool faculty175_codex_select_id(const char *task_id);
bool faculty175_codex_select_dial_at(int16_t x, int16_t y);
bool faculty175_codex_toggle_pin(void);
bool faculty175_codex_toggle_pin_id(const char *task_id);
bool faculty175_codex_set_emoji(const char *task_id, int emoji_index);
bool faculty175_codex_queue_action(const char *action);
bool faculty175_codex_queue_prompt(const char *input);
bool faculty175_codex_task_settings_open(void);
bool faculty175_codex_task_settings_is_open(void);
void faculty175_codex_task_settings_close(void);
bool faculty175_codex_task_settings_adjust(int section, int delta);
int faculty175_codex_task_settings_value(int section);
const char *faculty175_codex_task_settings_label(int section);
const char *faculty175_codex_status_label(faculty175_codex_task_status_t status);
