#include <cassert>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include "network_policy.h"
#include "wifi_credentials.h"

int main() {
  using namespace NetworkPolicy;
  assert(pollInterval(PetState::RUNNING) == 900);
  assert(pollInterval(PetState::SLEEP) == 2500);
  assert(pollInterval(PetState::SUCCESS) == 1500);
  assert(pollInterval(PetState::FAILED) == 1500);
  Backoff http;
  for (auto delay : {2000, 4000, 8000, 16000, 30000, 30000}) assert(http.take() == unsigned(delay));
  http.reset();
  assert(http.take() == 2000);
  Backoff wifi(10000);
  for (auto delay : {10000, 20000, 30000, 30000}) assert(wifi.take() == unsigned(delay));
  wifi.reset();
  assert(wifi.take() == 10000);
  Timer timer;
  timer.arm(UINT32_MAX - 100, 2000);
  assert(!timer.due(1898));
  assert(timer.due(1899));
  timer.arm(10000, 30000);
  assert(!timer.due(39999));
  assert(timer.due(40000));
  timer.arm(40000, 0);
  assert(timer.due(40000));

  using namespace WifiCredentials;
  assert(validInput("Wi-Fi", 5, "", 0));
  assert(validInput("Wi-Fi", 5, "password", 8));
  assert(!validInput("", 0, "password", 8));
  assert(!validInput("Wi-Fi", 5, "short", 5));
  assert(!validInput("Wi\0Fi", 5, "password", 8));
  assert(!validInput("Wi-Fi", 5, "pass\0word", 9));
  Stored credentials;
  assert(!credentials.valid());
  std::memset(credentials.ssid, 's', 32);
  std::memset(credentials.password, 'p', 63);
  assert(credentials.valid());
  credentials.ssid[32] = 's';
  assert(!credentials.valid());
  credentials.ssid[32] = 0;
  credentials.password[63] = 'p';
  assert(!credentials.valid());
  assert(!validInput("123456789012345678901234567890123", 33, "", 0));
  assert(!validInput("Wi-Fi", 5, "1234567890123456789012345678901234567890123456789012345678901234", 64));
  // Cleared/corrupt NVS must suppress embedded legacy credentials, and boot
  // maintenance must always be able to override a broken saved network.
  assert(choose(false, false, false, false) == Source::SETUP);
  assert(choose(false, false, false, true) == Source::LEGACY);
  assert(choose(false, true, true, true) == Source::SAVED);
  assert(choose(false, true, false, true) == Source::SETUP);
  for (bool saved : {false, true}) for (bool valid : {false, true})
    assert(choose(true, saved, valid, true) == Source::SETUP);
}
