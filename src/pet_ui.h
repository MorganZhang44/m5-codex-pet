#pragma once

#include "pet_state.h"

namespace PetUi {
void begin();
// Display a passive status; redraw only when the supplied state changes.
void setState(PetState state);
// Transport health only; does not change the pet state.
void setConnected(bool connected);
}  // namespace PetUi
