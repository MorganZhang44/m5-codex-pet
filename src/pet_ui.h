#pragma once

#include "status_data.h"
#include "network_policy.h"

namespace PetUi {
void begin();
// Display a passive status; redraw only when the supplied state changes.
void setState(PetState state);
// Redraw usage only when values or freshness change.
void setUsage(const Usage& usage);
// Transport health only; does not change the pet state.
void setTransport(TransportState transport);
void setLastResult(LastResult result);
void showSetup(const char* apName);
}  // namespace PetUi
