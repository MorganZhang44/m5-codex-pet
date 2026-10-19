#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Portable, read-only network snapshot. No credentials or Arduino types.
struct DeviceInfo {
  char ssid[33] = {};
  char ipv4[16] = {};
  char bridge[254] = {};
  int32_t rssi = 0;
  bool hasRssi = false;

  bool operator==(const DeviceInfo& other) const {
    return strcmp(ssid, other.ssid) == 0 && strcmp(ipv4, other.ipv4) == 0 &&
           strcmp(bridge, other.bridge) == 0 && hasRssi == other.hasRssi &&
           (!hasRssi || rssi == other.rssi);
  }
};

namespace DeviceText {
constexpr const char* kFirmwareVersion = "v0.6a";

// Font 1 uses six-pixel cells. Bound output and replace unsupported/control
// bytes so remote names cannot wrap, inject newlines, or split a UTF-8 glyph.
inline void fit(const char* source, char* out, size_t capacity, size_t cells) {
  if (!capacity) return;
  const char* value = source && *source ? source : "--";
  const size_t limit = cells < capacity - 1 ? cells : capacity - 1;
  const size_t length = strlen(value);
  const size_t count = length < limit ? length : limit;
  for (size_t i = 0; i < count; ++i) {
    const unsigned char c = static_cast<unsigned char>(value[i]);
    out[i] = c >= 32 && c <= 126 ? c : '?';
  }
  if (length > limit && limit >= 3) {
    out[limit - 3] = out[limit - 2] = out[limit - 1] = '.';
  }
  out[count] = '\0';
}
}  // namespace DeviceText
