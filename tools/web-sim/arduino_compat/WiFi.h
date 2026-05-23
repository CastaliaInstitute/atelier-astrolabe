#pragma once

class WiFiClass {
 public:
  int status() const { return 0; }
  int RSSI() const { return -70; }
  struct IPAddress {
    String toString() const { return String("127.0.0.1"); }
  };
  IPAddress localIP() const { return {}; }
};

static WiFiClass WiFi;
