#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void atom_ota_init(void);
bool atom_ota_handle(const char *line);
void atom_ota_maybe_start_recovery_request(void);
bool atom_ota_active(void);

#ifdef __cplusplus
}
#endif
