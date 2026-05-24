#pragma once

#include <stdbool.h>

#include "astrolabe_ui.h"

const char *astrolabe_p4_settings_profile(void);
astrolabe_ui_face_t astrolabe_p4_settings_home_face(void);
bool astrolabe_p4_settings_set_home_face(astrolabe_ui_face_t face);
void astrolabe_p4_settings_log_status(void);

