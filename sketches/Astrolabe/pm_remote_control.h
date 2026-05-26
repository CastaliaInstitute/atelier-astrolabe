#pragma once

#include <Arduino.h>
#include <stdint.h>

class WebServer;
class BLEServer;

enum class PmRemoteCommandType : uint8_t {
  None = 0,
  Face,
  Button,
  Tap,
  TouchDown,
  TouchUp,
  Tts,
  Tour,
  Stop,
};

struct PmRemoteCommand {
  PmRemoteCommandType type = PmRemoteCommandType::None;
  char face[32] = "";
  char button[16] = "";
  char mode[16] = "";
  char text[768] = "";
  int16_t x = 0;
  int16_t y = 0;
  uint32_t duration_ms = 0;
  uint32_t dwell_ms = 0;
  uint32_t seq = 0;
};

bool pm_remote_control_enabled(void);
uint32_t pm_remote_control_sequence(void);
bool pm_remote_control_enqueue(const PmRemoteCommand &cmd);
bool pm_remote_control_take(PmRemoteCommand *out);

/** Serial: `remote`, `remote pair [seconds]`, `remote clear`. Returns true if handled. */
bool pm_remote_control_serial_command(const char *line);

/** Adds GET/POST /control to the existing WiFi screen/log HTTP server. */
void pm_remote_control_register_http(WebServer &server);

/** Adds a BLE GATT control service to an already initialized BLE server. */
void pm_remote_control_attach_ble(BLEServer *server);
