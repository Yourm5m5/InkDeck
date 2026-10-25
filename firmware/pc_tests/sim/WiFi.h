#pragma once
#include "Arduino.h"
#define WIFI_AP 2
#define WIFI_OFF 0
struct FakeIP { String toString() { return "192.168.4.1"; } };
struct FakeWiFi { void mode(int) {} void softAP(const char*, const char*) {} void softAPdisconnect(bool) {} FakeIP softAPIP() { return {}; } };
extern FakeWiFi WiFi;
