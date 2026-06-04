#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void paper_ota_init(void);
bool paper_ota_handle(const char *line);
void paper_ota_maybe_start_recovery_request(void);
bool paper_ota_active(void);

#ifdef __cplusplus
}
#endif
