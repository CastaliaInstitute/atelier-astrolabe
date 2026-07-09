#pragma once

#include "FreeRTOS.h"

void vTaskDelay(TickType_t ticks);
TickType_t xTaskGetTickCount(void);

