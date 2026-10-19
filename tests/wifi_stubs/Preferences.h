#pragma once
#include <cstring>
#include <vector>
#include <string>
struct Preferences {
  static std::vector<unsigned char> blob;
  static int writes;
  static std::vector<unsigned char> bridgeBlob;
  static std::vector<unsigned char> pairingBlob;
  static bool failPairWrite;
  static bool failRemove;
  static bool failBridgeWrite;
  static bool failWifiWrite;
  static std::vector<unsigned char>& data(const char* key) {
    return std::string(key) == "pairing" ? pairingBlob : std::string(key) == "bridge" ? bridgeBlob : blob;
  }
  bool begin(const char*, bool) { return true; }
  void end() {}
  bool isKey(const char* key) { return !data(key).empty(); }
  size_t getBytesLength(const char* key) { return data(key).size(); }
  size_t getBytes(const char* key, void* out, size_t size) {
    auto& blob = data(key);
    if (size != blob.size()) return 0;
    std::memcpy(out, blob.data(), size);
    return size;
  }
  bool remove(const char* key) { if (failRemove) return false; data(key).clear(); return true; }
  size_t putBytes(const char* key, const void* input, size_t size) {
    if ((std::string(key) == "pairing" && failPairWrite) || (std::string(key) == "bridge" && failBridgeWrite) ||
        (std::string(key) == "wifi" && failWifiWrite)) return 0;
    auto& blob = data(key);
    const auto* bytes = static_cast<const unsigned char*>(input);
    blob.assign(bytes, bytes + size);
    ++writes;
    return size;
  }
};
