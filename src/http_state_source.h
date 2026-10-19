#pragma once

#include <stdint.h>
#include "pet_state.h"

namespace HttpStateSource {
void begin();
// True only when a valid service state was received; otherwise leave state alone.
bool poll(uint32_t now, PetState& state);
// Most recent poll succeeded (independent of the pet's agent-loop state).
bool connected();
}  // namespace HttpStateSource
