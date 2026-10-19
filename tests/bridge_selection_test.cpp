#include <cassert>
#include <cstring>
#include <initializer_list>
#include "bridge_selection.h"
using namespace BridgeSelection;
int main() {
  Stored saved, moved, other, address;
  assert(!saved.valid() && !matches(saved, true, other));
  assert(makeKey("My-Mac.local", "192.0.2.1", 8788, saved));
  assert(std::strcmp(saved.key, "my-mac.local:8788") == 0);
  assert(makeKey("my-mac.local", "192.0.2.99", 8788, moved));
  assert(makeKey("Other.local", "192.0.2.1", 8788, other));
  assert(matches(saved, true, moved));
  assert(!matches(saved, true, other) && !matches(saved, false, moved));
  assert(makeKey("", "192.0.2.1", 8788, address));
  assert(std::strcmp(address.key, "192.0.2.1:8788") == 0);
  assert(!matches(saved, true, address));
  assert(makeKey("my-mac.local", "192.0.2.99", 80, moved));
  assert(!matches(saved, true, moved));
  for (const char* s : {"", "host", ":80", "host:0", "host:65536", "host:-1", "host:08", "a:1:2", "<a>:80", "a:80/path", "a:port", ".:80", "a..b:80", "-host:80", "host-:80"})
    assert(!validKey(s, std::strlen(s)));
  const char nul[] = {'a', ':', '8', '0', 0, 'x'};
  assert(!validKey(nul, sizeof(nul)));
  Stored damaged;
  damaged.key[10] = 'x';
  assert(!damaged.valid());
  std::memset(saved.key, 'x', sizeof(saved.key));
  assert(!saved.valid() && !matches(saved, true, other));
}
