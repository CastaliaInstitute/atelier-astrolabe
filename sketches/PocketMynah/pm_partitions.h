#pragma once

#include <stddef.h>

#include <FS.h>

/** Mount factory/data partitions from partitions_32mb.csv. */
bool pm_partitions_mount_all(void);

bool pm_partitions_faces_mounted(void);
bool pm_partitions_assets_mounted(void);

/** Active face pack LittleFS (label from NVS). */
fs::FS &pm_partitions_faces_fs(void);

/** Mount a face partition by label (faces_a / faces_b) for OTA staging. */
bool pm_partitions_mount_faces_label(const char *label);

const char *pm_partitions_faces_mount_path(void);

bool pm_partitions_erase_faces_label(const char *label);
