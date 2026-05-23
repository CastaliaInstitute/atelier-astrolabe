#pragma once

class File {
 public:
  bool available() const { return false; }
  size_t size() const { return 0; }
  int read() { return -1; }
  size_t read(uint8_t *, size_t) { return 0; }
  size_t readBytes(char *out, size_t len) { if (len) out[0] = '\0'; return 0; }
  String readStringUntil(char) { return String(); }
  void println(const char *) {}
  void println(const String &) {}
  void print(const char *) {}
  void print(const String &) {}
  void close() {}
  operator bool() const { return false; }
};

#define FILE_READ "r"
#define FILE_WRITE "w"

class LittleFSClass {
 public:
  bool begin(bool = true) { return false; }
  bool exists(const char *) { return false; }
  bool mkdir(const char *) { return false; }
  File open(const char *, const char * = "r") { return {}; }
};

static LittleFSClass LittleFS;
