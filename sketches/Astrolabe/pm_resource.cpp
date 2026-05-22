#include "pm_resource.h"

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

#include "pm_heap.h"

static portMUX_TYPE s_resource_mux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_resource_owners = 0;

bool pm_resource_acquire(uint32_t owner, uint32_t conflicts, const char *name) {
  bool ok = false;
  uint32_t held = 0;
  portENTER_CRITICAL(&s_resource_mux);
  held = s_resource_owners;
  if ((held & conflicts) == 0) {
    s_resource_owners |= owner;
    ok = true;
  }
  portEXIT_CRITICAL(&s_resource_mux);
  if (ok) {
    Serial.printf("resource: acquire %s owner=0x%08x held=0x%08x\n", name ? name : "-", owner, held | owner);
    pm_heap_trace("resource-acquire", -1);
  } else {
    Serial.printf("resource: busy %s owner=0x%08x conflicts=0x%08x held=0x%08x\n", name ? name : "-", owner,
                  conflicts, held);
    pm_heap_trace("resource-busy", -1);
  }
  return ok;
}

void pm_resource_release(uint32_t owner, const char *name) {
  uint32_t held = 0;
  portENTER_CRITICAL(&s_resource_mux);
  s_resource_owners &= ~owner;
  held = s_resource_owners;
  portEXIT_CRITICAL(&s_resource_mux);
  Serial.printf("resource: release %s owner=0x%08x held=0x%08x\n", name ? name : "-", owner, held);
  pm_heap_trace("resource-release", -1);
}

uint32_t pm_resource_owners(void) {
  uint32_t held = 0;
  portENTER_CRITICAL(&s_resource_mux);
  held = s_resource_owners;
  portEXIT_CRITICAL(&s_resource_mux);
  return held;
}
