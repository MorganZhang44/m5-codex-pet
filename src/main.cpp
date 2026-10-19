#include <Arduino.h>
#include <M5Unified.h>

#include "http_state_source.h"
#include "pet_ui.h"
#include "setup_entry.h"
#include "wifi_config.h"

namespace {
SetupEntry setupEntry;
}

void setup() {
  auto config = M5.config();
  config.internal_imu = false;
  config.internal_rtc = false;
  config.internal_mic = false;
  config.internal_spk = false;
  M5.begin(config);  // Detects StickS3 and initializes its power/display.
  PetUi::begin();
  // Sample through the M5Unified debounce interval before checking a boot hold.
  // BtnB is the StickS3 side button (GPIO12), not BtnA or BtnPWR.
  for (int i = 0; i < 3; ++i) {
    M5.update();
    delay(10);
  }
  bool maintenance = false;
  if (M5.BtnB.isPressed()) {
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setCursor(4, 20);
    M5.Display.print("Hold side button\n2s for setup");
    while (true) {
      M5.update();
      maintenance = setupEntry.update(M5.BtnB.isPressed(), millis());
      if (maintenance || !M5.BtnB.isPressed()) break;
      delay(10);
    }
    PetUi::begin();
  }
  HttpStateSource::begin(maintenance);
  if (WifiConfig::setupMode()) PetUi::showSetup();
}

void loop() {
  static NetworkPolicy::Timer deviceTimer;
  static TransportState previousTransport = TransportState::SETUP;
  M5.update();
  if (!WifiConfig::setupMode()) {
    // Setup takes priority over any simultaneous BtnA release/page change.
    if (setupEntry.update(M5.BtnB.isPressed(), millis())) {
      HttpStateSource::enterSetup();
      PetUi::showSetup();
    } else {
      PetUi::updateButton(M5.BtnA.isPressed(), millis());
    }
  }
  StatusData status;
  if (HttpStateSource::poll(millis(), status)) {
    PetUi::setState(status.state);
    PetUi::setUsage(status.usage);
    PetUi::setLastResult(status.lastResult);
  }
  if (!WifiConfig::setupMode()) {
    const auto transport = HttpStateSource::transport();
    PetUi::setTransport(transport);
    if (transport != previousTransport || deviceTimer.due(millis())) {
      PetUi::setDeviceInfo(HttpStateSource::deviceInfo());
      deviceTimer.arm(millis(), 1000);
      previousTransport = transport;
    }
  }
  delay(10);
}
