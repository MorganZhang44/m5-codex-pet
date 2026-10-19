#include <cassert>
#include <cstring>
#include "device_info.h"
#include "page_navigation.h"

int main() {
  PageNavigation page;
  assert(!page.devicePage());
  assert(!page.update(true, 0));
  assert(!page.update(true, 2000));
  assert(!page.update(false, 2010)); // Boot hold/release is swallowed.
  assert(!page.devicePage());
  assert(!page.update(true, 2100));
  assert(page.update(false, 2200));
  assert(page.devicePage());
  assert(!page.update(false, 2210)); // Release toggles exactly once.
  assert(!page.update(true, 2300));
  assert(!page.update(false, 2900)); // Long holds do not toggle.
  assert(page.devicePage());
  assert(!page.update(true, UINT32_MAX - 40));
  assert(page.update(false, 40)); // Short press across millis rollover.
  assert(!page.devicePage());
  page.enterSetup();
  assert(!page.update(true, 100));
  assert(!page.update(false, 200));
  assert(!page.devicePage());
  PageNavigation idleBoot;
  assert(!idleBoot.update(false, 10));
  assert(!idleBoot.update(true, 20));
  assert(idleBoot.update(false, 120));

  DeviceInfo a, b;
  assert(a == b);
  b.rssi = -50;
  assert(a == b); // Unavailable RSSI cannot cause redraws.
  b.hasRssi = true;
  assert(!(a == b));
  a = b;
  assert(a == b);
  strcpy(b.ssid, "test network");
  assert(!(a == b));
  a = b;
  strcpy(b.ipv4, "192.0.2.123");
  assert(!(a == b));
  a = b;
  strcpy(b.bridge, "bridge-host");
  assert(!(a == b));

  char fitted[20];
  DeviceText::fit("12345678901234567890123456789012", fitted, sizeof(fitted), 19);
  assert(strcmp(fitted, "1234567890123456...") == 0);
  DeviceText::fit("192.168.100.200", fitted, sizeof(fitted), 19);
  assert(strcmp(fitted, "192.168.100.200") == 0);
  DeviceText::fit("", fitted, sizeof(fitted), 19);
  assert(strcmp(fitted, "--") == 0);
  DeviceText::fit("name\n\t\xc3\xa9", fitted, sizeof(fitted), 19);
  assert(strcmp(fitted, "name????") == 0);
  char tiny[4];
  DeviceText::fit("long", tiny, sizeof(tiny), 19);
  assert(strcmp(tiny, "...") == 0);
  DeviceText::fit("long", tiny, 1, 19);
  assert(tiny[0] == 0);
  DeviceText::fit("long", nullptr, 0, 19);
  assert(strcmp(DeviceText::kFirmwareVersion, "v0.6a") == 0);
}
