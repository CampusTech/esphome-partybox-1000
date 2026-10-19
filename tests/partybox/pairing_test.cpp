#include <cassert>
#include <iostream>
#include <vector>

#include "target.h"
using namespace esphome::partybox;
int main() {
  PeerAddress a{}, b{};
  assert(parse_address("68:52:10:54:00:98", a));
  assert(parse_address("68:52:10:54:01:1E", b));
  const uint8_t eir[] = {18,  9,   'J', 'B', 'L', ' ', 'P', 'a', 'r', 't', 'y', 'B', 'o', 'x', ' ',
                         '1', '0', '0', '0', 8,   255, 'H', 'M', 'P', 'B', 13,  0,   6,   0};
  assert(is_pairing_candidate(0x342428, eir, sizeof(eir)));
  assert(!is_pairing_candidate(0, eir, sizeof(eir)));
  assert(!is_pairing_candidate(0x342428, eir, 10));
  auto malformed = std::vector<uint8_t>(eir, eir + sizeof(eir));
  malformed[1] = 1;
  assert(!is_pairing_candidate(0x342428, malformed.data(), malformed.size()));
  PairingCandidates c;
  assert(c.size() == 0 && !c.overflowed());
  c.add(a);
  c.add(a);
  assert(c.size() == 1 && c.single() && format_address(*c.single()) == format_address(a));
  c.add(b);
  assert(c.size() == 2 && !c.single());
  for (int i = 1; i <= 8; i++) {
    auto x = a;
    x.bytes[5] = i;
    c.add(x);
  }
  assert(c.overflowed() && !c.single());
  c.clear();
  assert(c.size() == 0 && !c.overflowed());
  std::cout << "PASS: strict candidate matching, deduplication, ambiguity and bounded overflow\n";
}
