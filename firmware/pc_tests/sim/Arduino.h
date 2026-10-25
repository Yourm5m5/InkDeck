// Tiny fake Arduino core so main.cpp can run on a PC (simulation only).
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <algorithm>
#include <stdexcept>
#include <initializer_list>
using std::min; using std::max;
#define HIGH 1
#define LOW 0
#define INPUT 0
#define INPUT_PULLUP 2
#define OUTPUT 1
#define FILE_WRITE "w"
#define FILE_READ "r"
template <class T, class A, class B> T constrain(T x, A a, B b) { return x < a ? a : (x > b ? b : x); }

class String : public std::string {
 public:
  String() {}
  String(const char* s) : std::string(s ? s : "") {}
  String(const std::string& s) : std::string(s) {}
  String(char c) : std::string(1, c) {}
  String(const char* s, unsigned n) : std::string(s, n) {}
  explicit String(int v) : std::string(std::to_string(v)) {}
  explicit String(unsigned v) : std::string(std::to_string(v)) {}
  explicit String(long v) : std::string(std::to_string(v)) {}
  explicit String(unsigned long v) : std::string(std::to_string(v)) {}
  unsigned length() const { return size(); }
  String substring(int a) const { return a >= (int)size() ? String() : String(substr(a)); }
  String substring(int a, int b) const { return String(substr(a, b - a)); }
  int lastIndexOf(char c) const { size_t p = rfind(c); return p == npos ? -1 : (int)p; }
  int indexOf(char c) const { size_t p = find(c); return p == npos ? -1 : (int)p; }
  bool startsWith(const String& s) const { return compare(0, s.size(), s) == 0; }
  bool endsWith(const String& s) const { return size() >= s.size() && compare(size() - s.size(), s.size(), s) == 0; }
  void remove(int i) { erase(i); }
  void replace(const String& a, const String& b) {
    size_t p = 0; while ((p = find(a, p)) != npos) { std::string::replace(p, a.size(), b); p += b.size(); }
  }
  void toLowerCase() { for (auto& c : *this) c = tolower(c); }
};
inline String operator+(const String& a, const String& b) { String r(a); r.append(b); return r; }
inline String operator+(const String& a, const char* b) { return String(static_cast<const std::string&>(a) + b); }
inline String operator+(const char* a, const String& b) { return String(a + static_cast<const std::string&>(b)); }
inline String operator+(const String& a, char b) { return String(static_cast<const std::string&>(a) + b); }

// ---- fake time + pins (driven by the test harness)
extern uint32_t g_millis;
extern int g_pins[64];
extern int g_motorOnMs;
inline uint32_t millis() { return g_millis; }
inline void delay(uint32_t ms) { g_millis += ms; }
inline void pinMode(int, int) {}
inline int digitalRead(int p) { return g_pins[p]; }
extern int g_motorPulses;
inline void digitalWrite(int p, int v) { if (p == 45 && v == HIGH) g_motorPulses++; g_pins[p] = v; }
int analogReadMilliVolts(int);
inline void* ps_calloc(size_t n, size_t s) { return calloc(n, s); }

struct FakeSerial {
  void begin(int) {}
  template <class... A> void printf(const char* f, A... a) { if (getenv("SIM_VERBOSE")) ::printf(f, a...); }
  void println(const char* s) { ::printf("%s\n", s); }
};
extern FakeSerial Serial;

// ---- sleep
struct DeepSleep : std::exception {};
enum { ESP_SLEEP_WAKEUP_UNDEFINED = 0, ESP_SLEEP_WAKEUP_EXT0 = 2 };
enum gpio_num_t { GPIO_NUM_10 = 10 };
extern int g_wakeCause;
inline int esp_sleep_get_wakeup_cause() { return g_wakeCause; }
inline void esp_sleep_enable_ext0_wakeup(gpio_num_t, int) {}
[[noreturn]] inline void esp_deep_sleep_start() { throw DeepSleep(); }
