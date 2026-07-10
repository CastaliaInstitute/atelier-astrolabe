#pragma once

#include <stdint.h>

typedef uint32_t TickType_t;
typedef void *TaskHandle_t;
typedef int portMUX_TYPE;

#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#define portTICK_PERIOD_MS 1
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(mux) ((void)(mux))
#define portEXIT_CRITICAL(mux) ((void)(mux))
