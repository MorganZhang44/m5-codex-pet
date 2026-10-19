#pragma once

#include <stdint.h>

// Feed debounced BtnB state at boot and during use, independent of the page.
// A completed hold requests setup once; release before the threshold cancels it.
class SetupEntry {
 public:
  static constexpr uint32_t kHoldMs = 2000;

  bool update(bool pressed, uint32_t now) {
    if (requested) return false;
    if (!pressed) {
      holding = false;
      return false;
    }
    if (!holding) {
      holding = true;
      started = now;
    }
    if (uint32_t(now - started) < kHoldMs) return false;
    requested = true;
    return true;
  }

 private:
  bool holding = false;
  bool requested = false;
  uint32_t started = 0;
};
