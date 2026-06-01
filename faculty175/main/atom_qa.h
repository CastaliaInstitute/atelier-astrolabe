#pragma once

#include "faculty175_board.h"
#include "atom_listen.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    atom_listen_t *listen;
    faculty175_ui_state_t *ui;
    const char *faculty_slug;
    const char *faculty_name;
    const char *ui_detail;
} atom_qa_bind_t;

/** Register live faculty state for `qa listen` / `qa ui` output. */
void atom_qa_bind(const atom_qa_bind_t *bind);

/** Handle `qa …` serial commands. Returns true when `line` was consumed. */
bool atom_qa_handle(const char *line);

#ifdef __cplusplus
}
#endif
