#include "indicator.h"

#include <cassert>
#include <iostream>

using namespace esphome::partybox;
int main() {
  Indicator i;
  assert(i.color(0, true, true, false) == IndicatorColor::GREEN);
  assert(i.color(0, false, true, false) == IndicatorColor::AMBER);
  assert(i.color(600, false, true, false) == IndicatorColor::OFF);
  assert(i.color(0, true, false, false) == IndicatorColor::BLUE);
  assert(i.color(600, true, false, false) == IndicatorColor::OFF);
  // Standby is deliberately not an input to health.
  i.transmitted(1000);
  assert(i.color(1000, true, true, false) == IndicatorColor::CYAN);
  assert(i.color(1119, true, true, false) == IndicatorColor::CYAN);
  assert(i.color(1120, true, true, false) == IndicatorColor::GREEN);
  i.failed(2000);
  for (unsigned n = 0; n < 6; ++n)
    assert(i.color(2000 + n * 100, true, true, false) == (n % 2 ? IndicatorColor::OFF : IndicatorColor::RED));
  assert(i.color(2600, true, true, false) == IndicatorColor::GREEN);
  i.failed(3000);
  i.transmitted(3100);
  assert(i.color(3100, true, true, false) == IndicatorColor::OFF);  // A late local write cannot hide a failure.
  assert(i.color(3100, true, true, true) == IndicatorColor::RED);
  i.transmitted(0xfffffff0U);
  assert(i.color(0x20, true, true, false) == IndicatorColor::CYAN);
  assert(i.color(0x80, true, true, false) == IndicatorColor::GREEN);
  assert(i.color(0xfffffff0U, true, true, false) == IndicatorColor::GREEN);
  std::cout << "PASS: RGB status, activity/error overlays, standby semantics and timer wrap\n";
}
