#include <cassert>
#include <cstring>
#include "Arduino.h"
#include "Preferences.h"
#include "WiFi.h"
#include "wifi_config.h"
#include "wifi_credentials.h"
#include "bridge_config.h"
#include "pairing_config.h"
#include "ESPmDNS.h"

uint32_t testMillis = 100;
FakeESP ESP;
FakeWiFi WiFi;
FakeSerial Serial;
std::vector<unsigned char> Preferences::blob;
int Preferences::writes = 0;
std::vector<unsigned char> Preferences::bridgeBlob;
bool Preferences::failBridgeWrite = false;
std::vector<unsigned char> Preferences::pairingBlob;
bool Preferences::failPairWrite = false;
bool Preferences::failRemove = false;
bool Preferences::failWifiWrite = false;
FakeMDNS MDNS;
void service() {
  WifiConfig::service();
  Serial.hostRead(); // USB ISR/host progresses independently of firmware flush.
}
void command(const std::string& text, const char* expected) {
  Serial.output.clear(); Serial.send(text + "\n");
  for (int i = 0; i < 4; ++i) service();
  assert(Serial.output.find(expected) != std::string::npos);
  assert(Serial.output.find("test-saved") == std::string::npos);
  assert(Serial.output.find("test-new-pass") == std::string::npos);
  assert(Serial.output.find("74657374") == std::string::npos);
}
int main(int argc, char** argv) {
  assert(argc == 2);
  const std::string scenario = argv[1];
  WifiCredentials::Stored saved;
  std::strcpy(saved.ssid, "test-saved-network");
  std::strcpy(saved.password, "test-saved-pass");
  if (scenario == "cleared") saved = WifiCredentials::Stored{};
  if (scenario != "legacy" && scenario != "absent") {
    const auto* bytes = reinterpret_cast<const unsigned char*>(&saved);
    Preferences::blob.assign(bytes, bytes + sizeof(saved));
  }
  if (scenario == "corrupt") Preferences::blob = {1, 2};
  if (scenario == "paired" || scenario == "pair-corrupt" || scenario == "pair-cleared") {
    PairingConfig::Stored pairing;
    pairing.wifi = saved;
    std::strcpy(pairing.bridge.key, "paired-mac.local:8788");
    if (scenario == "pair-cleared") pairing.wifi = WifiCredentials::Stored{};
    const auto* bytes = reinterpret_cast<const unsigned char*>(&pairing);
    Preferences::pairingBlob.assign(bytes, bytes + sizeof(pairing));
    // Conflicting transitional keys must never override the current pair.
    BridgeSelection::Stored oldBridge;
    std::strcpy(oldBridge.key, "old-mac.local:8788");
    bytes = reinterpret_cast<const unsigned char*>(&oldBridge);
    Preferences::bridgeBlob.assign(bytes, bytes + sizeof(oldBridge));
    if (scenario == "pair-corrupt") Preferences::pairingBlob = {1, 2};
  }
  const auto original = Preferences::blob;
  WifiConfig::begin(scenario == "setup");
  if (scenario == "saved" || scenario == "legacy" || scenario == "paired") {
    assert(!WifiConfig::setupMode() && WiFi.currentMode == WIFI_STA && WiFi.attempts == 1);
    // Opening USB after reset must not remotely bypass physical setup entry.
    Serial.output.clear(); Serial.send("\nHELP\nCANCEL\n");
    service();
    assert(!WifiConfig::setupMode() && Serial.output.empty() && Preferences::writes == 0);
    WifiConfig::retry();
#ifdef TEST_NO_LEGACY
    assert(WiFi.ssid == "test-saved-network");
#else
    assert(WiFi.ssid == "test-legacy-network");
#endif
    WifiConfig::retry();
    assert(WiFi.attempts == 3 && Preferences::blob == original);
  }
  if (scenario == "paired") {
    BridgeConfig::Service endpoint;
    assert(BridgeConfig::configured(endpoint) && std::string(endpoint.host.c_str()) == "paired-mac.local");
  }
  if (scenario == "pair-corrupt" || scenario == "pair-cleared") assert(WifiConfig::setupMode());
  if (scenario == "pair-corrupt") {
    BridgeConfig::Service endpoint;
    assert(!BridgeConfig::configured(endpoint));
  }
  if (scenario == "online") WiFi.connection = WL_CONNECTED;
  int modes = WiFi.modeChanges, attempts = WiFi.attempts, disconnects = WiFi.disconnects;
  WifiConfig::enterSetup(); WifiConfig::enterSetup();
  assert(WifiConfig::setupMode() && WiFi.aps == 0 && WiFi.modeChanges == modes);
  WifiConfig::connect(); WifiConfig::retry();
  assert(WiFi.attempts == attempts && WiFi.disconnects == disconnects);
  // Commands can arrive one byte at a time; CRLF also works.
  Serial.output.clear();
  for (char c : std::string("HELP\r\n")) {
    Serial.send(std::string(1, c)); service();
  }
  assert(Serial.output.find("OK HELP") != std::string::npos);
  // Resynchronize a previous partial/invalid session, as the host retries do.
  Serial.send("stale partial"); service();
  command("\nHELP", "OK HELP");
  command("CANCEL", "OK CANCEL");
  command("\nHELP", "OK HELP");
  command("CANCEL", "OK CANCEL");
  assert(Preferences::writes == 0);
  command("HELP", "OK HELP");
  command("STATUS", "OK STATUS pending=no");
  command("WIFI 61 00", "ERR wifi-input");
  command("WIFI 61 123", "ERR wifi-input");
  command("WIFI 61 73686f7274", "ERR wifi-input");
  command("WIFI " + std::string(66, '6') + " -", "ERR wifi-input");
  command("WIFI 61 " + std::string(128, '6'), "ERR wifi-input");
  command("WIFI 61 zz", "ERR wifi-input");
  command("WIFI 61 - extra", "ERR wifi-input");
  command("BRIDGE bad:0", "ERR bridge-input");
  command("BRIDGE auto extra", "ERR bridge-input");
  command("BRIDGE auto", "ERR bridge-input");
  command(std::string(600, 'x'), "ERR invalid-line");
  command(std::string("SAVE\0extra", 10), "ERR invalid-line");
  assert(Preferences::writes == 0);
  command("WIFI 746573742d6e6574 746573742d6e65772d70617373", "OK WIFI staged");
  command("BRIDGE MAC.local:8788", "OK BRIDGE staged");
  command("STATUS", "OK STATUS pending=yes");
  assert(Preferences::writes == 0);
  command("REBOOT", "ERR unsaved-changes");
  command("CANCEL", "OK CANCEL");
  command("SAVE", "OK SAVE"); assert(Preferences::writes == 0);
  command("BRIDGE MAC.local:8788", "OK BRIDGE staged");
  command("SAVE", "ERR SAVE stage-WIFI-and-BRIDGE-together");
  command("WIFI 746573742d6e6574 746573742d6e65772d70617373", "OK WIFI staged");
  Preferences::failPairWrite = true;
  command("SAVE", "ERR SAVE pairing-write");
  assert(Preferences::writes == 0 && Preferences::blob == original);
  Preferences::failPairWrite = false;
  command("SAVE", "OK SAVE");
  assert(Preferences::writes == 1 && Preferences::blob.empty() && Preferences::bridgeBlob.empty());
  PairingConfig::Stored paired;
  std::memcpy(&paired, Preferences::pairingBlob.data(), sizeof(paired));
  assert(paired.wifi.valid() && std::strcmp(paired.wifi.ssid, "test-net") == 0);
  assert(std::strcmp(paired.bridge.key, "mac.local:8788") == 0);
  BridgeConfig::begin();
  BridgeConfig::Service endpoint;
  assert(BridgeConfig::configured(endpoint));
  assert(std::string(endpoint.host.c_str()) == "mac.local" && endpoint.port == 8788);
  WiFi.connection = WL_CONNECTED;
  MDNS.results = {{"mac.local", "192.0.2.99", 8788}, {"other.local", "192.0.2.11", 8788}};
  assert(BridgeConfig::resolve(endpoint) && std::string(endpoint.host.c_str()) == "192.0.2.99");
  MDNS.results[0].ip = "192.0.2.100";
  assert(BridgeConfig::resolve(endpoint) && std::string(endpoint.host.c_str()) == "192.0.2.100");
  MDNS.results.erase(MDNS.results.begin());
  assert(!BridgeConfig::resolve(endpoint)); // Never choose the other computer.
  const auto firstPair = Preferences::pairingBlob;
  // An open network and UTF-8 SSID replace both values in exactly one write.
  command("WIFI c3a9206e6574 -", "OK WIFI staged");
  command("SAVE", "ERR SAVE stage-WIFI-and-BRIDGE-together");
  assert(Preferences::pairingBlob == firstPair);
  command("BRIDGE new-mac.local:8788", "OK BRIDGE staged");
  command("SAVE", "OK SAVE");
  std::memcpy(&paired, Preferences::pairingBlob.data(), sizeof(paired));
  assert(paired.wifi.valid() && paired.wifi.password[0] == 0 && std::strcmp(paired.wifi.ssid, "\xc3\xa9 net") == 0);
  assert(std::strcmp(paired.bridge.key, "new-mac.local:8788") == 0 && Preferences::writes == 2);
  const unsigned char obsolete[] = {1, 2};
  Preferences::blob.assign(obsolete, obsolete + sizeof(obsolete));
  command("WIFI c3a9206e6574 -", "OK WIFI staged");
  command("BRIDGE new-mac.local:8788", "OK BRIDGE staged");
  Preferences::failRemove = true;
  command("SAVE", "ERR SAVE pairing-write");
  assert(!Preferences::blob.empty());
  command("REBOOT", "ERR unsaved-changes");
  Preferences::failRemove = false;
  command("SAVE", "OK SAVE");
  assert(Preferences::blob.empty() && Preferences::bridgeBlob.empty());
  command("CLEAR-WIFI", "OK CLEAR-WIFI staged");
  command("SAVE", "OK SAVE");
  std::memcpy(&paired, Preferences::pairingBlob.data(), sizeof(paired));
  assert(!paired.wifi.valid() && std::strcmp(paired.bridge.key, "new-mac.local:8788") == 0);
  // Old automatic records cannot select another computer or compiled fallback.
  Preferences::pairingBlob.clear();
  BridgeSelection::Stored empty;
  const auto* bytes = reinterpret_cast<const unsigned char*>(&empty);
  Preferences::bridgeBlob.assign(bytes, bytes + sizeof(empty));
  BridgeConfig::begin();
  assert(!BridgeConfig::configured(endpoint) && !BridgeConfig::resolve(endpoint));
  Preferences::bridgeBlob = {1, 2};
  BridgeConfig::begin();
  assert(!BridgeConfig::configured(endpoint));
  assert(WiFi.attempts == attempts && WiFi.disconnects == disconnects && WiFi.aps == 0);
  testMillis = UINT32_MAX - 500;
  command("REBOOT", "OK REBOOT"); assert(ESP.restarts == 0);
  testMillis += 999; service(); assert(ESP.restarts == 0);
  ++testMillis; service(); assert(ESP.restarts == 1);
}
