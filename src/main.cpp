#include <Arduino.h>
#include <M5Unified.h>

#include "http_state_source.h"
#include "pet_ui.h"
#include "wifi_config.h"

void setup() {
  auto config = M5.config();
  config.internal_imu = false;
  config.internal_rtc = false;
  config.internal_mic = false;
  config.internal_spk = false;
  M5.begin(config);  // Detects StickS3 and initializes its power/display.
  PetUi::begin();
  // BtnA is sampled ONLY during boot and never changes the Codex pet state.
  delay(100);
  M5.update();
  bool maintenance = M5.BtnA.isPressed();
  if (maintenance) {
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setCursor(4, 20);
    M5.Display.print("Hold BtnA for setup");
    const uint32_t started = millis();
    while (uint32_t(millis() - started) < 1500) {
      M5.update();
      if (!M5.BtnA.isPressed()) { maintenance = false; break; }
      delay(10);
    }
    PetUi::begin();
  }
  HttpStateSource::begin(maintenance);
  if (WifiConfig::setupMode()) PetUi::showSetup(WifiConfig::setupName().c_str());
}

void loop() {
  StatusData status;
  if (HttpStateSource::poll(millis(), status)) {
    PetUi::setState(status.state);
    PetUi::setUsage(status.usage);
    PetUi::setLastResult(status.lastResult);
  }
  if (!WifiConfig::setupMode()) PetUi::setTransport(HttpStateSource::transport());
  delay(10);
}
