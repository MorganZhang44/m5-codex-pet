#include <cassert>
#include <initializer_list>
#include "page_navigation.h"
#include "setup_entry.h"

int main() {
  // A side button already held at the first boot sample uses the same timing.
  SetupEntry boot;
  assert(!boot.update(true, 100));
  assert(!boot.update(true, 2099));
  assert(boot.update(true, 2100));
  assert(!boot.update(true, 2101));
  assert(!boot.update(false, 2200));
  assert(!boot.update(true, 2300));
  assert(!boot.update(true, 4300)); // A setup request is latched, never repeated.

  SetupEntry cancelled;
  assert(!cancelled.update(true, 0));
  assert(!cancelled.update(false, 1999));
  assert(!cancelled.update(true, 2000));
  assert(!cancelled.update(true, 3999));
  assert(cancelled.update(true, 4000)); // Partial holds cannot accumulate.

  SetupEntry rollover;
  assert(!rollover.update(true, UINT32_MAX - 999));
  assert(!rollover.update(true, 999));
  assert(rollover.update(true, 1000));

  SetupEntry delayed;
  assert(!delayed.update(false, 0));
  assert(!delayed.update(true, 100));
  assert(delayed.update(true, 2750)); // Delayed loop sample still fires.

  // Exercise the runtime ordering on both pages, including simultaneous BtnA.
  for (bool device : {false, true}) {
    PageNavigation page;
    SetupEntry side;
    page.update(false, 0);
    page.update(true, 10);
    assert(!page.update(false, 2010)); // Long BtnA never enters setup/toggles.
    assert(!side.update(false, 2010));
    if (device) {
      page.update(true, 2100);
      assert(page.update(false, 2200));
    }
    assert(page.devicePage() == device);
    assert(!side.update(true, 3000));
    assert(!side.update(false, 3100)); // Short BtnB never switches pages.
    assert(page.devicePage() == device);
    assert(!side.update(true, 4000));
    page.update(true, 5900);
    assert(!side.update(true, 5999));
    if (side.update(true, 6000)) page.enterSetup();
    else assert(false);
    assert(!page.update(false, 6000)); // Setup wins over a short BtnA release.
    assert(page.devicePage() == device);
    assert(!page.update(true, 6100));
    assert(!page.update(false, 6200));
    assert(!side.update(false, 6200));
    assert(page.devicePage() == device);
  }
}
