#pragma once

#include "paper_board.h"
#include "astrolabe_audio_pipeline.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    astrolabe_audio_pipeline_t *pipeline;
    paper_ui_state_t *ui;
    const char *faculty_slug;
    const char *faculty_name;
    const char *ui_detail;
} paper_qa_bind_t;

/** Register live FacultyPaper state for `qa listen` / `qa ui` output. */
void paper_qa_bind(const paper_qa_bind_t *bind);

/** Handle `qa ...` serial commands. Returns true when `line` was consumed. */
bool paper_qa_handle(const char *line);

#ifdef __cplusplus
}
#endif
