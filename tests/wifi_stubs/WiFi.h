#pragma once
#include <cassert>
#include <string>
#include "Arduino.h"
enum { WIFI_AP, WIFI_STA, WIFI_AP_STA, WL_CONNECTED = 3 };
struct IPAddress {
  std::string value;
  IPAddress(const char* s = "") : value(s) {}
  bool operator==(const IPAddress& b) const { return value == b.value; }
  String toString() const { return String(value.c_str()); }
};
struct FakeWiFi {
  int currentMode = -1;
  int attempts = 0;
  int modeChanges = 0, aps = 0;
  int disconnects = 0;
  int connection = 0;
  int status() { return connection; }
  std::string ssid, password;
  void persistent(bool enabled) { assert(!enabled); }
  void setAutoReconnect(bool enabled) { assert(!enabled); }
  void mode(int value) { assert(value != WIFI_AP_STA); currentMode = value; ++modeChanges; if (value == WIFI_AP) connection = 0; }
  void softAP(const char*) { assert(currentMode == WIFI_AP); ++aps; }
  IPAddress localIP() { return IPAddress("192.0.2.42"); }
  void disconnect(bool off, bool erase) {
    assert(!off && !erase && currentMode == WIFI_STA);
    connection = 0;
    ++disconnects;
  }
  void begin(const char* name, const char* pass) {
    assert(currentMode == WIFI_STA);
    ssid = name;
    password = pass;
    ++attempts;
  }
};
extern FakeWiFi WiFi;
