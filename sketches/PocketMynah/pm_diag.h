#pragma once

#include <stddef.h>

#include <esp_err.h>

void pm_diag_init(void);

bool pm_diag_safe_mode(void);
void pm_diag_enter_safe_mode(const char *reason);
const char *pm_diag_safe_mode_reason(void);

void pm_diag_record_boot(void);
void pm_diag_mark_runtime_valid(void);
void pm_diag_mark_facepack_valid(void);

bool pm_diag_runtime_pending_validation(void);
bool pm_diag_facepack_pending(void);

void pm_diag_on_face_boot(void);
void pm_diag_record_face_error(esp_err_t err);

void pm_diag_status_line(char *buf, size_t cap);
