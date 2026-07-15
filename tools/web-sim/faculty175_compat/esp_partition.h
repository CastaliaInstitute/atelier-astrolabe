#pragma once

#include <stdint.h>

typedef enum {
    ESP_PARTITION_TYPE_APP = 0x00,
    ESP_PARTITION_TYPE_DATA = 0x01,
} esp_partition_type_t;

typedef enum {
    ESP_PARTITION_SUBTYPE_APP_FACTORY = 0x00,
    ESP_PARTITION_SUBTYPE_APP_OTA_0 = 0x10,
    ESP_PARTITION_SUBTYPE_APP_OTA_1 = 0x11,
    ESP_PARTITION_SUBTYPE_DATA_FAT = 0x81,
    ESP_PARTITION_SUBTYPE_DATA_SPIFFS = 0x82,
} esp_partition_subtype_t;

typedef struct {
    uint32_t address;
    uint32_t size;
    char label[17];
} esp_partition_t;

const esp_partition_t *esp_partition_find_first(esp_partition_type_t type,
                                                esp_partition_subtype_t subtype,
                                                const char *label);
