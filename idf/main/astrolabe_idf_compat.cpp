#include <Arduino.h>

bool __attribute__((weak)) pm_speaker_release_idle_task(void) {
  return false;
}
