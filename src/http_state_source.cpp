#include "http_state_source.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>

#include "pet_config.h"
#include "status_parser.h"

namespace {
constexpr uint32_t kPollMs = 1000;
constexpr uint32_t kReconnectMs = 10000;
constexpr uint16_t kTimeoutMs = 750;
uint32_t lastPoll = 0;
uint32_t lastReconnect = 0;
bool configured = false;
bool online = false;

// Bound response memory, including chunked responses, without a JSON dependency.
class StatusBody : public Stream {
 public:
  char data[129] = {};
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

void HttpStateSource::begin() {
  configured = PET_WIFI_SSID[0] != '\0' && PET_STATUS_HOST[0] != '\0' &&
               PET_STATUS_PORT > 0 && PET_STATUS_PORT <= 65535;
  online = false;
  if (!configured) return;
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(PET_WIFI_SSID, PET_WIFI_PASSWORD);
  lastReconnect = millis();
  lastPoll = millis() - kPollMs;
}

bool HttpStateSource::poll(uint32_t now, PetState& state) {
  if (!configured) return false;
  if (WiFi.status() != WL_CONNECTED) {
    online = false;
    if (static_cast<uint32_t>(now - lastReconnect) >= kReconnectMs) {
      lastReconnect = now;
      WiFi.reconnect();
    }
    return false;
  }
  // Unsigned subtraction remains correct across millis() rollover.
  if (static_cast<uint32_t>(now - lastPoll) < kPollMs) return false;
  lastPoll = now;
  online = false;

  WiFiClient client;
  HTTPClient http;
  http.setConnectTimeout(kTimeoutMs);
  http.setTimeout(kTimeoutMs);
  if (!http.begin(client, PET_STATUS_HOST, PET_STATUS_PORT, "/status")) {
    return false;
  }
  const int code = http.GET();
  StatusBody body;
  if (code == HTTP_CODE_OK && http.getSize() <= 128) {
    const int received = http.writeToStream(&body);
    online = received > 0 && !body.invalid &&
             StatusParser::parse(body.data, state);
  }
  http.end();
  return online;
}

bool HttpStateSource::connected() { return online; }
