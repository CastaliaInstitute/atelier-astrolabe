#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t faculty175_almanac_init(void);
void faculty175_almanac_start_auto_fetch_task(void);
bool faculty175_almanac_handle(const char *line);
bool faculty175_almanac_active(void);
const char *faculty175_almanac_state_name(void);
const char *faculty175_almanac_last(void);
const char *faculty175_almanac_manifest_url(void);
bool faculty175_almanac_cached_daily(char *date,
                                     size_t date_cap,
                                     char *season,
                                     size_t season_cap,
                                     char *moon,
                                     size_t moon_cap,
                                     char *prompt,
                                     size_t prompt_cap);
bool faculty175_almanac_cached_daily_ex(char *date,
                                        size_t date_cap,
                                        char *season,
                                        size_t season_cap,
                                        char *moon,
                                        size_t moon_cap,
                                        char *sun,
                                        size_t sun_cap,
                                        char *event,
                                        size_t event_cap,
                                        char *planting,
                                        size_t planting_cap,
                                        char *prompt,
                                        size_t prompt_cap);
bool faculty175_almanac_cached_phenology(char *date,
                                         size_t date_cap,
                                         char *subject,
                                         size_t subject_cap,
                                         char *action,
                                         size_t action_cap,
                                         char *habitat,
                                         size_t habitat_cap,
                                         char *prompt,
                                         size_t prompt_cap,
                                         char *image_path,
                                         size_t image_path_cap);

#ifdef __cplusplus
}
#endif
