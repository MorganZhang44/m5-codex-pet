#pragma once
#include <stddef.h>
#include <cstring>

namespace WifiCredentials {
inline bool validInput(const char* ssid, size_t ssidBytes,
                       const char* password, size_t passwordBytes) {
  return ssid && password && ssidBytes > 0 && ssidBytes <= 32 &&
         (passwordBytes == 0 || (passwordBytes >= 8 && passwordBytes <= 63)) &&
         !std::memchr(ssid, 0, ssidBytes) &&
         !std::memchr(password, 0, passwordBytes);
}
// Fixed, zero-terminated layout; write the entire pair in one atomic NVS blob.
struct Stored {
  char ssid[33] = {};
  char password[64] = {};
  bool valid() const {
    return ssid[32] == 0 && password[63] == 0 &&
           validInput(ssid, std::strlen(ssid), password, std::strlen(password));
  }
};
static_assert(sizeof(Stored) == 97, "Keep the NVS credential layout stable");
enum class Source { SETUP, SAVED, LEGACY };
inline Source choose(bool maintenance, bool savedExists, bool savedValid,
                     bool legacyValid) {
  if (maintenance) return Source::SETUP;
  if (savedExists) return savedValid ? Source::SAVED : Source::SETUP;
  return legacyValid ? Source::LEGACY : Source::SETUP;
}
// A failed association may fall back for this boot only. Never changes NVS,
// overrides maintenance/clear, or oscillates between credential pairs.
inline Source afterAssociationTimeout(Source current, bool legacyValid) {
  return current == Source::SAVED && legacyValid ? Source::LEGACY : current;
}
}  // namespace WifiCredentials
