#include <algorithm>
#include <cassert>
#include <cstring>
#include <M5Unified.h>
#include "pet_ui.h"

TestM5 M5;
bool shown(const char* value) {
  const auto& text = M5.Display.text;
  return std::find(text.begin(), text.end(), value) != text.end();
}
void click(uint32_t now) {
  PetUi::updateButton(true, now);
  PetUi::updateButton(false, now + 100);
}
int main() {
  // Exercise the actual renderer with all state/history combinations.
  for (auto state : {PetState::SLEEP, PetState::RUNNING, PetState::SUCCESS, PetState::FAILED}) {
    for (auto result : {LastResult::NONE, LastResult::SUCCESS, LastResult::FAILED}) {
      PetUi::begin();
      PetUi::setState(state);
      PetUi::setLastResult(result);
      PetUi::updateButton(true, 0);
      PetUi::updateButton(false, 1600); // Boot release cannot open DEVICE.
      assert(!shown("DEVICE"));
      click(2000);
      assert(shown("DEVICE") && shown("v0.6a") && shown("--"));
      auto draws = M5.Display.draws;
      PetUi::setState(state);
      PetUi::setLastResult(result);
      PetUi::setDeviceInfo(DeviceInfo{});
      assert(M5.Display.draws == draws);
      DeviceInfo info;
      strcpy(info.ssid, "test network");
      strcpy(info.ipv4, "192.0.2.1");
      info.rssi = -67;
      info.hasRssi = true;
      PetUi::setDeviceInfo(info);
      assert(shown("test network") && shown("192.0.2.1") && shown("-67 dBm"));
      PetUi::setTransport(TransportState::OFFLINE);
      assert(shown("OFFLINE"));
      click(3000);
      const char* expected = state == PetState::SLEEP ? "SLEEP" :
          state == PetState::RUNNING ? "RUNNING" : state == PetState::SUCCESS ? "SUCCESS" : "FAILED";
      assert(shown(expected));
      assert(shown(result == LastResult::NONE ? "LAST --" :
                   result == LastResult::SUCCESS ? "LAST OK" : "LAST FAIL"));
      // New snapshots received while DEVICE is visible must remain invisible
      // there, then appear together when returning to the pet page.
      click(4000);
      draws = M5.Display.draws;
      Usage usage;
      usage.fiveHour = 42;
      PetUi::setUsage(usage);
      PetUi::setState(PetState::RUNNING);
      PetUi::setLastResult(LastResult::FAILED);
      assert(M5.Display.draws == draws);
      click(5000);
      assert(shown("42%") && shown("RUNNING") && shown("LAST FAIL"));
    }
  }
  // Runtime setup replaces either page and keeps navigation/updates hidden.
  for (bool device : {false, true}) {
    PetUi::begin();
    PetUi::updateButton(false, 0);
    PetUi::setState(PetState::SUCCESS);
    PetUi::setLastResult(LastResult::FAILED);
    if (device) click(100);
    assert(shown(device ? "DEVICE" : "SUCCESS"));
    PetUi::showSetup();
    assert(shown("USB SETUP"));
    const auto draws = M5.Display.draws;
    click(6000);
    PetUi::setTransport(TransportState::SETUP);
    PetUi::setDeviceInfo(DeviceInfo{});
    PetUi::setState(PetState::SLEEP);
    PetUi::setLastResult(LastResult::NONE);
    PetUi::setUsage(Usage{});
    assert(M5.Display.draws == draws && shown("USB SETUP"));
  }
}
