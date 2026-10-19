#pragma once

#include <stdint.h>
#include "status_data.h"
#include "network_policy.h"

namespace HttpStateSource {
void begin(bool maintenance = false);
// True only when a valid full snapshot was received; otherwise leave it alone.
bool poll(uint32_t now, StatusData& status);
TransportState transport();
}  // namespace HttpStateSource
