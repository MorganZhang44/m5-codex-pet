#pragma once
#include <cstring>
#include "wifi_credentials.h"
#include "bridge_selection.h"

// Bounded, non-echoing line protocol. Backend owns persistence and replies.
// Hex is transport encoding, not encryption. No response contains input data.
namespace SerialSetup {
template<class Backend> class Session {
 public:
  explicit Session(Backend& backend) : io(backend) {}
  void feed(char c) {
    if (c == '\n') {
      if (bad) io.reply("ERR invalid-line");
      else { line[length] = 0; execute(); }
      std::memset(line, 0, sizeof(line));
      length = 0; bad = false;
    } else if (c != '\r') {
      if (c < 32 || c > 126 || length == sizeof(line) - 1) bad = true;
      if (!bad) line[length++] = c;
    }
  }
 private:
  Backend& io;
  char line[512] = {};
  size_t length = 0;
  bool bad = false, wifiDirty = false, bridgeDirty = false, clearing = false;
  WifiCredentials::Stored wifi;
  BridgeSelection::Stored bridge;
  static int nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  }
  static bool decode(const char* text, char* out, size_t capacity) {
    if (std::strcmp(text, "-") == 0) return true;
    const size_t size = std::strlen(text);
    if (!size || size % 2 || size / 2 >= capacity) return false;
    for (size_t i = 0; i < size; i += 2) {
      int a = nibble(text[i]), b = nibble(text[i + 1]);
      if (a < 0 || b < 0 || (a == 0 && b == 0)) return false;
      out[i / 2] = char(a * 16 + b);
    }
    return true;
  }
  void execute() {
    if (!std::strcmp(line, "HELP")) {
      io.reply("OK HELP STATUS WIFI <ssid-hex> <password-hex-or-> BRIDGE <host:port> CLEAR-WIFI CANCEL SAVE REBOOT");
    } else if (!std::strcmp(line, "STATUS")) {
      io.status();
      io.reply(wifiDirty || bridgeDirty ? "OK STATUS pending=yes" : "OK STATUS pending=no");
    } else if (!std::strncmp(line, "WIFI ", 5)) {
      WifiCredentials::Stored next;
      char* password = std::strchr(line + 5, ' ');
      bool valid = false;
      if (password) {
        *password++ = 0;
        valid = decode(line + 5, next.ssid, sizeof(next.ssid)) &&
            decode(password, next.password, sizeof(next.password)) && next.valid();
      }
      if (valid) { wifi = next; wifiDirty = true; clearing = false; }
      next = WifiCredentials::Stored{};
      io.reply(valid ? "OK WIFI staged" : "ERR wifi-input");
    } else if (!std::strncmp(line, "BRIDGE ", 7)) {
      BridgeSelection::Stored next;
      const char* value = line + 7;
      bool valid = false;
      if (std::strlen(value) < sizeof(next.key)) {
        std::strcpy(next.key, value);
        for (char* c = next.key; *c; ++c) if (*c >= 'A' && *c <= 'Z') *c += 'a' - 'A';
        valid = next.key[0] && next.valid();
      }
      if (valid) { bridge = next; bridgeDirty = true; }
      io.reply(valid ? "OK BRIDGE staged" : "ERR bridge-input");
    } else if (!std::strcmp(line, "CLEAR-WIFI")) {
      wifi = WifiCredentials::Stored{}; wifiDirty = true; clearing = true;
      io.reply("OK CLEAR-WIFI staged");
    } else if (!std::strcmp(line, "CANCEL")) {
      wifi = WifiCredentials::Stored{}; bridge = BridgeSelection::Stored{};
      wifiDirty = bridgeDirty = clearing = false; io.reply("OK CANCEL");
    } else if (!std::strcmp(line, "SAVE")) {
      if (wifiDirty || bridgeDirty) {
        if (!(clearing && !bridgeDirty) && !(wifiDirty && bridgeDirty && !clearing)) {
          io.reply("ERR SAVE stage-WIFI-and-BRIDGE-together"); return;
        }
        if (!io.savePair(wifi, bridge, clearing)) {
          io.reply("ERR SAVE pairing-write; retry-SAVE"); return;
        }
        wifiDirty = bridgeDirty = clearing = false;
        wifi = WifiCredentials::Stored{}; bridge = BridgeSelection::Stored{};
      }
      io.reply("OK SAVE");
    } else if (!std::strcmp(line, "REBOOT")) {
      if (wifiDirty || bridgeDirty) { io.reply("ERR unsaved-changes"); return; }
      io.reply("OK REBOOT"); io.reboot();
    } else io.reply("ERR unknown-command; use-HELP");
  }
};
}  // namespace SerialSetup
