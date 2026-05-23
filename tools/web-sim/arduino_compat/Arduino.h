#pragma once

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <ctime>

#define HIGH 1
#define LOW 0
#define OUTPUT 1
#define INPUT 0
#define INPUT_PULLUP 2
#define PROGMEM
#define IRAM_ATTR
#define ARDUINO 10819

using byte = uint8_t;

uint32_t millis(void);
uint32_t micros(void);
void delay(uint32_t ms);
inline void yield(void) {}
uint32_t esp_random(void);

template <typename T>
static inline T min(T a, T b) {
  return std::min(a, b);
}

template <typename T>
static inline T max(T a, T b) {
  return std::max(a, b);
}

class SerialClass {
 public:
  template <typename... Args>
  void printf(const char *fmt, Args... args) {
    std::printf(fmt, args...);
  }
  void print(const char *s) { std::printf("%s", s); }
  void println(const char *s = "") { std::printf("%s\n", s); }
};

extern SerialClass Serial;

class String : public std::string {
 public:
  using std::string::string;
  String() = default;
  String(const std::string &s) : std::string(s) {}
  void toCharArray(char *out, size_t cap) const {
    if (!out || cap == 0) return;
    std::snprintf(out, cap, "%s", c_str());
  }
};

using TaskHandle_t = void *;
using SemaphoreHandle_t = void *;
using BaseType_t = int;
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define portMAX_DELAY 0xffffffffu
#define eSetBits 0
#define pdMS_TO_TICKS(ms) (ms)
inline SemaphoreHandle_t xSemaphoreCreateMutex(void) { return reinterpret_cast<void *>(1); }
inline int xSemaphoreTake(SemaphoreHandle_t, uint32_t) { return pdTRUE; }
inline void xSemaphoreGive(SemaphoreHandle_t) {}
inline int xTaskCreatePinnedToCore(void (*)(void *), const char *, uint32_t, void *, uint32_t, TaskHandle_t *, int) {
  return pdFALSE;
}
inline uint32_t ulTaskNotifyTake(int, uint32_t) { return 0; }
inline void xTaskNotify(TaskHandle_t, uint32_t, int) {}
