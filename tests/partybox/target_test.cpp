#include "target.h"

#include <cassert>
#include <iostream>
using namespace esphome::partybox;
int main() {
  PeerAddress address{};
  assert(!address.configured());
  assert(format_address(address).empty());
  assert(parse_address("68:52:10:54:00:98", address));
  assert(address.configured());
  assert(format_address(address) == "68:52:10:54:00:98");
  assert(parse_address("a4:b5:c6:d7:e8:f9", address));
  assert(format_address(address) == "A4:B5:C6:D7:E8:F9");
  for (const auto* invalid : {"00:00:00:00:00:00", "FF:FF:FF:FF:FF:FF", "68-52-10-54-00-98", "68:52:10:54:00",
                              "68:52:10:54:00:9Z", " 68:52:10:54:00:98", "68:52:10:54:00:980"}) {
    assert(!parse_address(invalid, address));
    assert(format_address(address) == "A4:B5:C6:D7:E8:F9");
  }
  assert(parse_address("", address));
  assert(!address.configured());
  assert(format_address(address).empty());
  std::cout << "PASS: target parsing, normalization, empty setting and invalid-write atomicity\n";
}
