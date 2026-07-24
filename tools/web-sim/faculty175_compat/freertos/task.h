#pragma once

#include "FreeRTOS.h"

typedef void *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);

void vTaskDelay(TickType_t ticks);
void vTaskDelete(TaskHandle_t task);
int xTaskCreate(void (*task)(void *),
                const char *name,
                uint32_t stack_depth,
                void *argument,
                unsigned priority,
                TaskHandle_t *handle);
TickType_t xTaskGetTickCount(void);
void vTaskDelete(TaskHandle_t task);
int xTaskCreate(TaskFunction_t task,
                const char *name,
                unsigned stack_depth,
                void *arg,
                unsigned priority,
                TaskHandle_t *out);

#ifndef pdPASS
#define pdPASS 1
#endif
