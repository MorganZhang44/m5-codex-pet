#pragma once
#include <Arduino.h>

namespace WifiConfig {
// A boot maintenance request ignores legacy credentials and opens setup.
void begin(bool maintenance);
bool setupMode();
const String& setupName();
void service(uint32_t now);
void connect();
}  // namespace WifiConfig
