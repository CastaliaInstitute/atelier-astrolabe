#pragma once

#include "esp_err.h"

typedef struct {
    const char *base_path;
    const char *partition_label;
    int max_files;
    int format_if_mount_failed;
} esp_vfs_spiffs_conf_t;

esp_err_t esp_vfs_spiffs_register(const esp_vfs_spiffs_conf_t *conf);

