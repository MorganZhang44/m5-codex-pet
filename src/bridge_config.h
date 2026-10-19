#pragma once
#include <Arduino.h>
#include "bridge_selection.h"
namespace BridgeConfig {
struct Service {
  BridgeSelection::Stored identity;
  String host;
  uint16_t port = 0;
};
void begin();
void resetDiscovery();
// Returns only the current configured endpoint, never a discovered alternative.
bool configured(Service& out);
// Re-resolve .local on every request so DHCP changes do not pin a stale address.
bool resolve(Service& out);
String current();
}
