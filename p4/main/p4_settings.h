#pragma once

#include <stdbool.h>

const char *astrolabe_p4_settings_profile(void);
int astrolabe_p4_settings_home_face(void);
bool astrolabe_p4_settings_set_home_face(int face);
void astrolabe_p4_settings_log_status(void);
