#pragma once
#include "Arduino.h"
#include <functional>
#include <map>
enum { UPLOAD_FILE_START, UPLOAD_FILE_WRITE, UPLOAD_FILE_END, UPLOAD_FILE_ABORTED };
enum { HTTP_GET, HTTP_POST };
struct HTTPUpload { int status; String filename; uint8_t buf[2048]; size_t currentSize; };
class WebServer {
 public:
  explicit WebServer(int) {}
  typedef std::function<void()> H;
  void on(const char* p, int, H h) { routes[p] = h; }
  void on(const char* p, int, H h, H up) { routes[p] = h; uploads[p] = up; }
  void begin() {} void stop() {} void handleClient() {}
  void send(int code, const char* = "", const String& body = "") { lastCode = code; lastBody = body; }
  void sendHeader(const char*, const char*) {}
  String arg(const char* k) { return args[k]; }
  HTTPUpload& upload() { return up; }
  std::map<std::string, H> routes, uploads;
  std::map<std::string, String> args;
  HTTPUpload up;
  int lastCode = 0; String lastBody;
};
