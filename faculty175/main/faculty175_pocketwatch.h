#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

bool faculty175_pocketwatch_background_enabled(void);
esp_err_t faculty175_pocketwatch_background_set_enabled(bool enabled);
bool faculty175_pocketwatch_draw_background(void);
bool faculty175_pocketwatch_handle(const char *line);

#ifdef __cplusplus
}
#endif
