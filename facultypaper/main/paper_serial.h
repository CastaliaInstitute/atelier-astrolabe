#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"

typedef esp_err_t (*paper_serial_turn_fn)(const char *user, const char *reply);
typedef esp_err_t (*paper_serial_scroll_fn)(int delta);

void paper_serial_set_turn_callback(paper_serial_turn_fn fn);
void paper_serial_set_scroll_callback(paper_serial_scroll_fn fn);
/** Start USB serial command reader (`screen`, `qa status`, ...). */
void paper_serial_init(void);

#ifdef __cplusplus
}
#endif
