#pragma once

#include "esp_err.h"

#include "faculty175_board.h"
#include "faculty175_listen.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    faculty175_listen_t *listen;
    faculty175_ui_state_t *ui;
    const char *faculty_slug;
    const char *faculty_name;
    const char *ui_detail;
    bool (*set_faculty)(const char *slug, const char *name);
    bool (*fetch_faculty)(const char *slug, const char *name);
    esp_err_t (*trigger_stt)(uint32_t capture_ms);
    void (*emit_tasks)(void);
} faculty175_qa_bind_t;

/** Register live faculty state for `qa listen` / `qa ui` output. */
void faculty175_qa_bind(const faculty175_qa_bind_t *bind);

/** True while a QA command owns the audio path. */
bool faculty175_qa_audio_busy(void);

/** Handle `qa …` serial commands. Returns true when `line` was consumed. */
bool faculty175_qa_handle(const char *line);

#ifdef __cplusplus
}
#endif
