#pragma once
#include <stdint.h>
#include <stddef.h>
#include "pet_state.h"

enum class TransportState { CONNECTING, ONLINE, OFFLINE, SETUP };

namespace NetworkPolicy {
inline uint32_t pollInterval(PetState state) {
  return state == PetState::RUNNING ? 900 :
         state == PetState::SLEEP ? 2500 : 1500;
}
// Unsigned elapsed time is safe across millis() rollover.
struct Timer {
  uint32_t since = 0;
  uint32_t interval = 0;
  bool due(uint32_t now) const { return uint32_t(now - since) >= interval; }
  void arm(uint32_t now, uint32_t delay) { since = now; interval = delay; }
};
struct Backoff {
  const uint32_t initial;
  uint32_t next;
  explicit Backoff(uint32_t base = 2000) : initial(base), next(base) {}
  void reset() { next = initial; }
  uint32_t take() {
    uint32_t result = next;
    next = next >= 15000 ? 30000 : next * 2;
    return result;
  }
};
}  // namespace NetworkPolicy
