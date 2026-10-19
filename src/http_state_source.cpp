#include "http_state_source.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include "pet_config.h"
#include "status_parser.h"
#include "wifi_config.h"

namespace {
constexpr uint16_t kTimeoutMs = 750;
constexpr uint32_t kDiscoveryRefreshMs = 60000;
NetworkPolicy::Timer pollTimer, wifiTimer, discoveryTimer;
NetworkPolicy::Backoff httpBackoff;
NetworkPolicy::Backoff wifiBackoff(10000);
TransportState transportState = TransportState::CONNECTING;
bool hadWifi = false;
bool mdnsStarted = false;
String endpoint;
uint16_t endpointPort = 0;
IPAddress localAddress;

void invalidateDiscovery() {
  endpoint = "";
  endpointPort = 0;
  if (mdnsStarted) MDNS.end();
  mdnsStarted = false;
  discoveryTimer.arm(millis(), 0);
}

bool discover() {
  if (!mdnsStarted) {
    String name = "codex-pet-" + String(uint32_t(ESP.getEfuseMac() & 0xffffff), HEX);
    mdnsStarted = MDNS.begin(name.c_str());
  }
  int selected = -1;
  if (mdnsStarted) {
    const int count = MDNS.queryService("_codex-pet", "_tcp");
    for (int i = 0; i < count; ++i) {
      if (MDNS.IP(i) == IPAddress() || MDNS.port(i) == 0) continue;
      // Arduino 2.0.17 has no result instance-name accessor: first usable
      // IPv4 result in API index order wins; never infer it from hostname/TXT.
      selected = i;
      break;
    }
  }
  if (selected >= 0) {
    endpoint = MDNS.IP(selected).toString();
    endpointPort = MDNS.port(selected);
  } else {
    endpoint = PET_STATUS_HOST;
    endpointPort = PET_STATUS_PORT > 0 && PET_STATUS_PORT <= 65535 ? PET_STATUS_PORT : 0;
  }
  discoveryTimer.arm(millis(), kDiscoveryRefreshMs);
  return !endpoint.isEmpty() && endpointPort != 0;
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

bool HttpStateSource::poll(uint32_t now, StatusData& status) {
  WifiConfig::service(now);
  if (WifiConfig::setupMode()) return false;
  if (WiFi.status() != WL_CONNECTED) {
    if (hadWifi) {
      hadWifi = false;
      transportState = TransportState::OFFLINE;
      invalidateDiscovery();
      wifiBackoff.reset();
      wifiTimer.arm(now, wifiBackoff.take());
    }
    // Initial association is CONNECTING; subsequent failed attempts are OFFLINE.
    if (transportState == TransportState::ONLINE) transportState = TransportState::OFFLINE;
    if (wifiTimer.due(now)) {
      transportState = TransportState::OFFLINE;
      WiFi.disconnect();
      WifiConfig::connect();
      wifiTimer.arm(millis(), wifiBackoff.take());
    }
    return false;
  }
  if (!hadWifi || localAddress != WiFi.localIP()) {
    hadWifi = true;
    localAddress = WiFi.localIP();
    invalidateDiscovery();
    wifiBackoff.reset();
    httpBackoff.reset();
    pollTimer.arm(now, 0);
    transportState = TransportState::CONNECTING;
  }
  if (!pollTimer.due(now)) return false;
  bool valid = false;
  if ((!endpoint.isEmpty() && !discoveryTimer.due(now)) || discover()) {
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
  // Schedule from completion: a slow request/discovery cannot cause tight retries.
  if (valid) {
    transportState = TransportState::ONLINE;
    httpBackoff.reset();
    pollTimer.arm(millis(), NetworkPolicy::pollInterval(status.state));
  } else {
    transportState = TransportState::OFFLINE;
    invalidateDiscovery();
    pollTimer.arm(millis(), httpBackoff.take());
  }
  return valid;
}

TransportState HttpStateSource::transport() { return transportState; }
