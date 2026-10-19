#pragma once
#include <Arduino.h>

namespace WifiConfig {
constexpr uint32_t kAssociationTimeoutMs = 20000;
// USB-only setup; normal runtime station/recovery behavior is unchanged.
void begin(bool maintenance);
void enterSetup();
bool setupMode();
void service();
void connect();
// Called only after an association attempt times out, never for HTTP failure.
void retry();
}  // namespace WifiConfig
