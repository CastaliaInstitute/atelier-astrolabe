#include <Arduino.h>

__attribute__((weak)) bool pm_speaker_release_idle_task(void) {
  return false;
}
