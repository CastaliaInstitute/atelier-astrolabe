#pragma once

#include "FreeRTOS.h"

typedef void *TaskHandle_t;

void vTaskDelay(TickType_t ticks);
TickType_t xTaskGetTickCount(void);
