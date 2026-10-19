#pragma once

#include <stdint.h>

// Presentation only: this controller has no access to pet state or history.
class PageNavigation {
 public:
  bool devicePage() const { return device; }
  void enterSetup() { setup = true; }
  bool update(bool pressed, uint32_t now) {
    if (setup) return false;
    // Ignore BtnA held across boot, including its release. Page switching
    // requires a fresh short press; only BtnB can request maintenance.
    if (!armed) {
      if (!pressed) armed = true;
      return false;
    }
    if (pressed && !wasPressed) started = now;
    const bool toggle = wasPressed && !pressed && uint32_t(now - started) < 600;
    wasPressed = pressed;
    if (toggle) device = !device;
    return toggle;
  }

 private:
  bool device = false;
  bool setup = false;
  bool armed = false;
  bool wasPressed = false;
  uint32_t started = 0;
};
