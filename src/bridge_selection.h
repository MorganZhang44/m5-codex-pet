#pragma once
#include <stdint.h>
#include <stddef.h>
#include <cstring>
#include <cstdio>

namespace BridgeSelection {
// DNS host + service port survives DHCP changes. Address-only results cannot.
struct Stored {
  char key[262] = {};  // empty means unpaired (including legacy automatic records)
  bool valid() const;
};
inline bool validKey(const char* key, size_t size) {
  if (!key || size == 0 || size > 261 || std::memchr(key, 0, size)) return false;
  size_t colon = size;
  for (size_t i = 0; i < size; ++i) {
    const char c = key[i];
    if (c == ':') { if (colon != size) return false; colon = i; }
    else if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.')) return false;
  }
  if (colon == 0 || colon > 253 || colon + 1 >= size || size - colon - 1 > 5) return false;
  size_t label = 0;
  for (size_t i = 0; i < colon; ++i) {
    if (key[i] == '.') {
      if (!label || key[i - 1] == '-') return false;
      label = 0;
    } else {
      if ((!label && key[i] == '-') || ++label > 63) return false;
    }
  }
  if (!label || key[colon - 1] == '-') return false;
  unsigned port = 0;
  for (size_t i = colon + 1; i < size; ++i) {
    if (key[i] < '0' || key[i] > '9') return false;
    port = port * 10 + unsigned(key[i] - '0');
  }
  return port > 0 && port <= 65535 && key[colon + 1] != '0';
}
inline bool Stored::valid() const {
  if (key[sizeof(key) - 1] != 0) return false;
  const size_t size = std::strlen(key);
  // Empty legacy records and damaged padding fail closed.
  for (size_t i = size; i < sizeof(key); ++i) if (key[i] != 0) return false;
  return validKey(key, size);
}
static_assert(sizeof(Stored) == 262, "Keep the bridge NVS layout stable");
inline bool makeKey(const char* hostname, const char* ip, uint16_t port, Stored& out) {
  out = Stored{};
  const char* host = hostname && *hostname ? hostname : ip;
  if (!host || !*host || !port || std::strlen(host) > 253) return false;
  std::snprintf(out.key, sizeof(out.key), "%s:%u", host, unsigned(port));
  for (char* c = out.key; *c; ++c) if (*c >= 'A' && *c <= 'Z') *c += 'a' - 'A';
  return out.valid();
}
// Only the single configured endpoint can match; no discovery wildcard.
inline bool matches(const Stored& saved, bool storageValid, const Stored& candidate) {
  return storageValid && saved.valid() && candidate.valid() &&
      std::strcmp(saved.key, candidate.key) == 0;
}
}  // namespace BridgeSelection
