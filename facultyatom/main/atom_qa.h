#pragma once

#include "atom_board.h"
#include "astrolabe_audio_pipeline.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    astrolabe_audio_pipeline_t *pipeline;
    atom_ui_state_t *ui;
    const char *faculty_slug;
    const char *faculty_name;
    const char *ui_detail;
} atom_qa_bind_t;

/** Register live FacultyAtom state for `qa listen` / `qa ui` output. */
void atom_qa_bind(const atom_qa_bind_t *bind);

/** Handle `qa …` serial commands. Returns true when `line` was consumed. */
bool atom_qa_handle(const char *line);

#ifdef __cplusplus
}
#endif
