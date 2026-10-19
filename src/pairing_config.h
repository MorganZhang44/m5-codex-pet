#pragma once
#include "wifi_credentials.h"
#include "bridge_selection.h"

// One atomic NVS value: never retain a previous pairing alongside the current one.
namespace PairingConfig {
struct Stored {
  WifiCredentials::Stored wifi;
  BridgeSelection::Stored bridge;
};
}
