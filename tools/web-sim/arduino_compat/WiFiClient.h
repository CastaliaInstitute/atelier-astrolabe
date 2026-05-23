#pragma once

class WiFiClient {
 public:
  int available() { return 0; }
  int read(uint8_t *, size_t) { return 0; }
  int readBytes(uint8_t *, size_t) { return 0; }
  bool connected() { return false; }
};
