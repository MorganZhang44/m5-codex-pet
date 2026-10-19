#include "wifi_config.h"
#include <Preferences.h>
#include <WiFi.h>
#include "network_policy.h"
#include "pet_config.h"
#include "wifi_credentials.h"
#include "bridge_config.h"
#include "serial_setup.h"
#include "pairing_config.h"

namespace {
Preferences preferences;
String ssid, password;
bool inSetup = false, restarting = false, savedWifiValid = false;
WifiCredentials::Source source = WifiCredentials::Source::SETUP;
bool legacyValid = false;
NetworkPolicy::Timer restartTimer;
bool openPreferences() { return preferences.begin("codex-pet", false); }
struct SerialBackend {
  void reply(const char* text) {
    Serial.println(text);
    // HWCDC sends asynchronously. flush() can discard queued bytes when its
    // connection check briefly fails, including the acknowledgement just sent.
    // REBOOT already leaves a one-second grace period for the final reply.
  }
  void status() {
    // Deliberately omit SSID as well as all password/credential bytes.
    reply(savedWifiValid ? "INFO wifi-saved=yes" : "INFO wifi-saved=no");
    reply(WiFi.status() == WL_CONNECTED ? "INFO wifi-connected=yes" : "INFO wifi-connected=no");
    Serial.println(String("INFO bridge=") + BridgeConfig::current());
  }
  bool savePair(const WifiCredentials::Stored& wifi, const BridgeSelection::Stored& bridge, bool clear) {
    PairingConfig::Stored next;
    next.wifi = wifi;
    next.bridge = bridge;
    if (clear) {
      BridgeConfig::Service current;
      if (BridgeConfig::configured(current)) next.bridge = current.identity;
    }
    if (!openPreferences()) return false;
    const bool ok = preferences.putBytes("pairing", &next, sizeof(next)) == sizeof(next);
    // Delete obsolete v0.6a keys; only pairing is authoritative from now on.
    bool cleaned = ok;
    if (ok) {
      if (preferences.isKey("wifi") && !preferences.remove("wifi")) cleaned = false;
      if (preferences.isKey("bridge") && !preferences.remove("bridge")) cleaned = false;
    }
    preferences.end();
    if (ok) { savedWifiValid = wifi.valid(); BridgeConfig::begin(); }
    next = PairingConfig::Stored{};
    return ok && cleaned;
  }
  void reboot() { restarting = true; restartTimer.arm(millis(), 1000); }
} backend;
SerialSetup::Session<SerialBackend> session(backend);
void startSetup() {
  inSetup = true;
  // No AP, web server, reassociation, or credential changes at entry.
  backend.reply("OK READY codex-pet-usb-v1; use HELP");
}
}  // namespace

void WifiConfig::begin(bool maintenance) {
  Serial.begin(115200);
  BridgeConfig::begin();
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);  // The transport owns bounded retry timing.
  bool saved = false;
  WifiCredentials::Stored credentials;
  bool savedValid = false;
  if (openPreferences()) {
    if (preferences.isKey("pairing")) {
      saved = true;
      PairingConfig::Stored pairing;
      savedValid = preferences.getBytesLength("pairing") == sizeof(pairing) &&
          preferences.getBytes("pairing", &pairing, sizeof(pairing)) == sizeof(pairing) && pairing.wifi.valid();
      if (savedValid) credentials = pairing.wifi;
    } else {
      saved = preferences.isKey("wifi");
      savedValid = preferences.getBytesLength("wifi") == sizeof(credentials) &&
          preferences.getBytes("wifi", &credentials, sizeof(credentials)) == sizeof(credentials) && credentials.valid();
    }
    preferences.end();
  }
  savedWifiValid = savedValid;
  legacyValid = WifiCredentials::validInput(PET_WIFI_SSID, strlen(PET_WIFI_SSID),
                                           PET_WIFI_PASSWORD, strlen(PET_WIFI_PASSWORD));
  source = WifiCredentials::choose(maintenance, saved, savedValid, legacyValid);
  if (source == WifiCredentials::Source::SETUP) {
    startSetup();
    return;
  }
  ssid = source == WifiCredentials::Source::SAVED ? credentials.ssid : PET_WIFI_SSID;
  password = source == WifiCredentials::Source::SAVED ? credentials.password : PET_WIFI_PASSWORD;
  connect();
}
void WifiConfig::connect() {
  if (inSetup) return;
  // begin() enables STA but can leave an AP active. Always use station-only
  // mode for normal operation; disconnect without erasing SDK/NVS settings.
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, false);
  WiFi.begin(ssid.c_str(), password.c_str());
}
void WifiConfig::retry() {
  if (inSetup) return;
  const auto next = WifiCredentials::afterAssociationTimeout(source, legacyValid);
  if (next != source) {
    source = next;
    ssid = PET_WIFI_SSID;
    password = PET_WIFI_PASSWORD;
  }
  connect();
}
void WifiConfig::enterSetup() {
  if (!inSetup) startSetup();
}
bool WifiConfig::setupMode() { return inSetup; }
void WifiConfig::service() {
  if (!inSetup) return;
  if (!restarting) {
    // Bound work per loop even if a host streams garbage continuously.
    for (int i = 0; i < 512 && Serial.available(); ++i) {
      session.feed(static_cast<char>(Serial.read()));
      if (restarting) break;
    }
  }
  if (restarting && restartTimer.due(millis())) ESP.restart();
}
