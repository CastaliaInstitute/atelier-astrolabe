#pragma once

class Arduino_GFX;

#include <esp_err.h>

bool pm_faces_pack_init(void);
const char *pm_faces_pack_last_error(void);

bool pm_faces_pack_available(void);
bool pm_faces_pack_use_pack_face(void);

esp_err_t pm_faces_pack_validate_registry(void);
esp_err_t pm_faces_pack_dry_run(void);

void pm_faces_pack_render(Arduino_GFX *gfx, float thinking_progress);

esp_err_t pm_faces_pack_mount_inactive_and_verify(const char *label);
