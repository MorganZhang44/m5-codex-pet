#include "wifi_config.h"
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include "network_policy.h"
#include "pet_config.h"
#include "wifi_credentials.h"

namespace {
Preferences preferences;
WebServer server(80);
String ssid, password, apName;
bool inSetup = false;
bool restarting = false;
NetworkPolicy::Timer restartTimer;
const char kPage[] =
    "<!doctype html><meta name='viewport' content='width=device-width'>"
    "<title>Codex Pet Wi-Fi</title><h1>Codex Pet Wi-Fi</h1>"
    "<p>Enter your 2.4 GHz Wi-Fi. Saved credentials will be replaced.</p>"
    "<form method='post' action='/save'>"
    "<label>SSID <input name='ssid' maxlength='32' required></label><br>"
    "<label>Password <input name='password' type='password' maxlength='63'></label>"
    "<br><button>Save and restart</button></form>"
    "<form method='post' action='/clear'><button>Clear saved Wi-Fi</button></form>";

void reply(int code, const char* message) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "text/html; charset=utf-8", message);
}
bool openPreferences() { return preferences.begin("codex-pet", false); }
void startSetup() {
  inSetup = true;
  WiFi.mode(WIFI_AP);
  apName = "CodexPet-Setup-" + String(uint32_t(ESP.getEfuseMac() & 0xffffff), HEX);
  WiFi.softAP(apName.c_str());
  server.on("/", HTTP_GET, [] { reply(200, kPage); });
  server.on("/save", HTTP_POST, [] {
    const String newSsid = server.arg("ssid");
    const String newPassword = server.arg("password");
    if (!WifiCredentials::validInput(newSsid.c_str(), newSsid.length(),
                                     newPassword.c_str(), newPassword.length())) {
      reply(400, "Invalid SSID/password length (password: empty or 8-63 bytes).");
      return;
    }
    // One NVS blob makes the credential pair atomic across power loss.
    WifiCredentials::Stored credentials;
    newSsid.toCharArray(credentials.ssid, sizeof(credentials.ssid));
    newPassword.toCharArray(credentials.password, sizeof(credentials.password));
    if (!openPreferences()) { reply(500, "Storage unavailable; retry."); return; }
    const bool saved = preferences.putBytes("wifi", &credentials, sizeof(credentials)) == sizeof(credentials);
    preferences.end();
    if (!saved) { reply(500, "Could not save; retry."); return; }
    reply(200, "Wi-Fi saved. Restarting; reconnect to your normal Wi-Fi.");
    restarting = true;
    restartTimer.arm(millis(), 1000);
  });
  server.on("/clear", HTTP_POST, [] {
    if (!openPreferences()) { reply(500, "Storage unavailable."); return; }
    // An empty saved blob suppresses compile-time fallback on later boots.
    WifiCredentials::Stored empty;
    const bool cleared = preferences.putBytes("wifi", &empty, sizeof(empty)) == sizeof(empty);
    preferences.end();
    reply(cleared ? 200 : 500, cleared ? "Saved Wi-Fi cleared. Enter new Wi-Fi on the setup page." : "Could not clear; retry.");
  });
  server.begin();
}
}  // namespace

void WifiConfig::begin(bool maintenance) {
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);  // The transport owns bounded retry timing.
  bool saved = false;
  WifiCredentials::Stored credentials;
  bool savedValid = false;
  if (openPreferences()) {
    saved = preferences.isKey("wifi");
    savedValid = preferences.getBytesLength("wifi") == sizeof(credentials) &&
        preferences.getBytes("wifi", &credentials, sizeof(credentials)) == sizeof(credentials) &&
        credentials.valid();
    preferences.end();
  }
  const auto source = WifiCredentials::choose(maintenance, saved, savedValid,
      WifiCredentials::validInput(PET_WIFI_SSID, strlen(PET_WIFI_SSID),
                                  PET_WIFI_PASSWORD, strlen(PET_WIFI_PASSWORD)));
  if (source == WifiCredentials::Source::SETUP) {
    startSetup();
    return;
  }
  ssid = source == WifiCredentials::Source::SAVED ? credentials.ssid : PET_WIFI_SSID;
  password = source == WifiCredentials::Source::SAVED ? credentials.password : PET_WIFI_PASSWORD;
  WiFi.mode(WIFI_STA);
  connect();
}
void WifiConfig::connect() { WiFi.begin(ssid.c_str(), password.c_str()); }
bool WifiConfig::setupMode() { return inSetup; }
const String& WifiConfig::setupName() { return apName; }
void WifiConfig::service(uint32_t now) {
  if (!inSetup) return;
  if (!restarting) server.handleClient();
  if (restarting && restartTimer.due(now)) ESP.restart();
}
