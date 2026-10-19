#pragma once

#include "status_data.h"
#include "network_policy.h"
#include "device_info.h"

namespace PetUi {
void begin();
void updateButton(bool pressed, uint32_t now);
void setDeviceInfo(const DeviceInfo& info);
// Display a passive status; redraw only when the supplied state changes.
void setState(PetState state);
// Redraw usage only when values, reset display components, or freshness change.
void setUsage(const Usage& usage);
// Transport health only; does not change the pet state.
void setTransport(TransportState transport);
void setLastResult(LastResult result);
void showSetup();
}  // namespace PetUi
