#pragma once

#include "WiFiClient.h"

class HTTPClient {
 public:
  bool begin(const char *) { return false; }
  bool begin(WiFiClient &, const char *) { return false; }
  void setTimeout(int) {}
  void setFollowRedirects(int) {}
  int GET() { return -1; }
  int POST(const char *) { return -1; }
  int getSize() { return 0; }
  WiFiClient *getStreamPtr() { return nullptr; }
  bool connected() { return false; }
  void addHeader(const char *, const char *) {}
  void end() {}
  const char *errorToString(int) { return "web sim"; }
};

#define HTTPC_STRICT_FOLLOW_REDIRECTS 1
