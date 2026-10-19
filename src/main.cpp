#include <Arduino.h>
#include <M5Unified.h>

#include "http_state_source.h"
#include "pet_ui.h"

void setup() {
  auto config = M5.config();
  config.internal_imu = false;
  config.internal_rtc = false;
  config.internal_mic = false;
  config.internal_spk = false;
  M5.begin(config);  // Detects StickS3 and initializes its power/display.
  PetUi::begin();
  HttpStateSource::begin();
}

void loop() {
  PetState state;
  if (HttpStateSource::poll(millis(), state)) {
    PetUi::setState(state);
  }
  PetUi::setConnected(HttpStateSource::connected());
  delay(10);
}
