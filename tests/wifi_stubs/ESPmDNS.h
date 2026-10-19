#pragma once
#include <vector>
#include <cassert>
#include "WiFi.h"
struct FakeMDNS {
  struct Result { const char* host; const char* ip; uint16_t port; };
  std::vector<Result> results;
  int queries = 0;
  bool begin(const char*) { return true; }
  void end() {}
  int queryService(const char* service, const char* proto) {
    assert(std::string(service) == "_codex-pet" && std::string(proto) == "_tcp");
    ++queries;
    return int(results.size());
  }
  IPAddress queryHost(const char* host, uint32_t timeout) {
    assert(timeout == 750);
    ++queries;
    for (const auto& r : results) {
      std::string name(r.host);
      if (name == host || name == std::string(host) + ".local") return IPAddress(r.ip);
    }
    return IPAddress();
  }
  String hostname(int i) { return String(results[i].host); }
  IPAddress IP(int i) { return IPAddress(results[i].ip); }
  uint16_t port(int i) { return results[i].port; }
};
extern FakeMDNS MDNS;
