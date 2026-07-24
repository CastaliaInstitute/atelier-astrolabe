#pragma once

#include "FreeRTOS.h"

typedef void *TaskHandle_t;

void vTaskDelay(TickType_t ticks);
void vTaskDelete(TaskHandle_t task);
int xTaskCreate(void (*task)(void *),
                const char *name,
                uint32_t stack_depth,
                void *argument,
                unsigned priority,
                TaskHandle_t *handle);
TickType_t xTaskGetTickCount(void);
