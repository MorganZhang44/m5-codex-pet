#include "http_state_source.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include "bridge_config.h"
#include "status_parser.h"
#include "wifi_config.h"

namespace {
constexpr uint16_t kTimeoutMs = 750;
NetworkPolicy::Timer pollTimer, wifiTimer;
NetworkPolicy::Backoff httpBackoff;
NetworkPolicy::Backoff wifiBackoff(WifiConfig::kAssociationTimeoutMs);
TransportState transportState = TransportState::CONNECTING;
bool hadWifi = false;
String endpoint;
String bridgeAddress;
uint16_t endpointPort = 0;
IPAddress localAddress;

void invalidateEndpoint() {
  endpoint = "";
  bridgeAddress = "";
  endpointPort = 0;
  BridgeConfig::resetDiscovery();
}

bool resolveCurrent() {
  BridgeConfig::Service bridge;
  if (!BridgeConfig::resolve(bridge)) return false;
  endpoint = bridge.host;
  endpointPort = bridge.port;
  return true;
}

// Bound response memory, including chunked responses, without a JSON dependency.
class StatusBody : public Stream {
 public:
  static constexpr size_t kMaxBytes = 512;
  char data[kMaxBytes + 1] = {};
  size_t length = 0;
  bool invalid = false;
  size_t write(uint8_t value) override {
    if (length == sizeof(data) - 1 || value == 0) {
      invalid = true;
      return 0;
    }
    data[length++] = static_cast<char>(value);
    data[length] = '\0';
    return 1;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}
};
}  // namespace

void HttpStateSource::begin(bool maintenance) {
  WifiConfig::begin(maintenance);
  transportState = WifiConfig::setupMode() ? TransportState::SETUP : TransportState::CONNECTING;
  wifiTimer.arm(millis(), wifiBackoff.take());
  pollTimer.arm(millis(), 0);
}

void HttpStateSource::enterSetup() {
  if (WifiConfig::setupMode()) return;
  invalidateEndpoint();
  WifiConfig::enterSetup();
  transportState = TransportState::SETUP;
}

bool HttpStateSource::poll(uint32_t now, StatusData& status) {
  WifiConfig::service();
  if (WifiConfig::setupMode()) return false;
  if (WiFi.status() != WL_CONNECTED) {
    if (hadWifi) {
      hadWifi = false;
      transportState = TransportState::OFFLINE;
      invalidateEndpoint();
      wifiBackoff.reset();
      // Give the current credentials a fresh full attempt after a link loss.
      // Only its timeout may select the legacy fallback.
      WifiConfig::connect();
      wifiTimer.arm(millis(), wifiBackoff.take());
      return false;
    }
    // Initial association is CONNECTING; subsequent failed attempts are OFFLINE.
    if (transportState == TransportState::ONLINE) transportState = TransportState::OFFLINE;
    if (wifiTimer.due(now)) {
      transportState = TransportState::OFFLINE;
      WifiConfig::retry();
      wifiTimer.arm(millis(), wifiBackoff.take());
    }
    return false;
  }
  if (!hadWifi || localAddress != WiFi.localIP()) {
    hadWifi = true;
    localAddress = WiFi.localIP();
    invalidateEndpoint();
    wifiBackoff.reset();
    httpBackoff.reset();
    pollTimer.arm(now, 0);
    transportState = TransportState::CONNECTING;
  }
  if (!pollTimer.due(now)) return false;
  bool valid = false;
  if (resolveCurrent()) {
    WiFiClient client;
    HTTPClient http;
    http.setConnectTimeout(kTimeoutMs);
    http.setTimeout(kTimeoutMs);
    if (http.begin(client, endpoint, endpointPort, "/status")) {
      const int code = http.GET();
      StatusBody body;
      if (code == HTTP_CODE_OK && http.getSize() <= static_cast<int>(StatusBody::kMaxBytes)) {
        const int received = http.writeToStream(&body);
        valid = received > 0 && !body.invalid && StatusParser::parse(body.data, status);
      }
      http.end();
    }
  }
  // Schedule from completion: a slow request/resolution cannot cause tight retries.
  if (valid) {
    bridgeAddress = "";
    IPAddress resolved;
    if (resolved.fromString(endpoint) || WiFi.hostByName(endpoint.c_str(), resolved))
      bridgeAddress = resolved.toString();
    transportState = TransportState::ONLINE;
    httpBackoff.reset();
    pollTimer.arm(millis(), NetworkPolicy::pollInterval(status.state));
  } else {
    transportState = TransportState::OFFLINE;
    invalidateEndpoint();
    pollTimer.arm(millis(), httpBackoff.take());
  }
  return valid;
}

TransportState HttpStateSource::transport() { return transportState; }

DeviceInfo HttpStateSource::deviceInfo() {
  DeviceInfo info;
  // Read current station values even while HTTP is OFFLINE/CONNECTING. Do not
  // substitute saved credentials or a cached DHCP address after Wi-Fi loss.
  WiFi.SSID().toCharArray(info.ssid, sizeof(info.ssid));
  const IPAddress ip = WiFi.localIP();
  if (ip != IPAddress()) ip.toString().toCharArray(info.ipv4, sizeof(info.ipv4));
  info.hasRssi = WiFi.status() == WL_CONNECTED;
  if (info.hasRssi) info.rssi = WiFi.RSSI();
  bridgeAddress.toCharArray(info.bridge, sizeof(info.bridge));
  return info;
}
