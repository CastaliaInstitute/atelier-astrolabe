#pragma once

#include <stdint.h>

#define ESP_PARTITION_TYPE_DATA 0x01
#define ESP_PARTITION_SUBTYPE_DATA_SPIFFS 0x82

typedef struct {
    uint32_t address;
    uint32_t size;
} esp_partition_t;

const esp_partition_t *esp_partition_find_first(int type, int subtype, const char *label);
