#pragma once

#include <stdint.h>
#include "status_data.h"
#include "network_policy.h"
#include "device_info.h"

namespace HttpStateSource {
void begin(bool maintenance = false);
// Stop station discovery/polling and open the existing provisioning backend.
void enterSetup();
// True only when a valid full snapshot was received; otherwise leave it alone.
bool poll(uint32_t now, StatusData& status);
TransportState transport();
DeviceInfo deviceInfo();
}  // namespace HttpStateSource
