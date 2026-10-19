#include "bridge_config.h"
#include "pairing_config.h"
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <cstdlib>

namespace {
BridgeSelection::Stored currentBridge;
bool storageValid = false, mdnsStarted = false;
}
void BridgeConfig::begin() {
  currentBridge = BridgeSelection::Stored{};
  Preferences prefs;
  storageValid = false;
  if (!prefs.begin("codex-pet", false)) return;
  if (prefs.isKey("pairing")) {
    PairingConfig::Stored pairing;
    storageValid = prefs.getBytesLength("pairing") == sizeof(pairing) &&
        prefs.getBytes("pairing", &pairing, sizeof(pairing)) == sizeof(pairing) && pairing.bridge.valid();
    if (storageValid) currentBridge = pairing.bridge;
  } else if (prefs.isKey("bridge")) {
    // Migrate a specific v0.6a endpoint, never the old empty/automatic wildcard.
    storageValid = prefs.getBytesLength("bridge") == sizeof(currentBridge) &&
        prefs.getBytes("bridge", &currentBridge, sizeof(currentBridge)) == sizeof(currentBridge) && currentBridge.valid();
  }
  prefs.end();
}
void BridgeConfig::resetDiscovery() {
  if (mdnsStarted) MDNS.end();
  mdnsStarted = false;
}
String BridgeConfig::current() {
  return storageValid ? String(currentBridge.key) : String("Unpaired; run USB setup");
}
bool BridgeConfig::configured(Service& out) {
  if (!storageValid || !currentBridge.valid()) return false;
  const char* colon = std::strchr(currentBridge.key, ':');
  out.identity = currentBridge;
  out.host = String(currentBridge.key, colon - currentBridge.key);
  out.port = static_cast<uint16_t>(std::atoi(colon + 1));
  return true;
}
bool BridgeConfig::resolve(Service& out) {
  if (!configured(out) || WiFi.status() != WL_CONNECTED) return false;
  const size_t length = out.host.length();
  if (length > 6 && !std::strcmp(out.host.c_str() + length - 6, ".local")) {
    if (!mdnsStarted) {
      const String name = "codex-pet-" + String(uint32_t(ESP.getEfuseMac() & 0xffffff), HEX);
      mdnsStarted = MDNS.begin(name.c_str());
    }
    if (!mdnsStarted) return false;
    const String hostname(out.host.c_str(), length - 6);
    const IPAddress ip = MDNS.queryHost(hostname.c_str(), 750);
    if (ip == IPAddress()) return false;
    out.host = ip.toString();
  }
  return true;
}
