#pragma once
#include "Arduino.h"
#include <map>
extern std::map<std::string, std::string> g_prefs;
struct Preferences {
  void begin(const char*, bool) {}
  String getString(const char* k, const char* d) { return g_prefs.count(k) ? String(g_prefs[k]) : String(d); }
  void putString(const char* k, const String& v) { g_prefs[k] = v; }
  bool getBool(const char* k, bool d) { return g_prefs.count(k) ? g_prefs[k] == "1" : d; }
  void putBool(const char* k, bool v) { g_prefs[k] = v ? "1" : "0"; }
  void remove(const char* k) { g_prefs.erase(k); }
};
