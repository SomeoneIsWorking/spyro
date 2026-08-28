#include "native_gameplay.h"

#include <cassert>
#include <iostream>

int main() {
  assert(!spyro::gameplay::hasDigitalDirection(0));
  assert(spyro::gameplay::hasDigitalDirection(0x1000));
  assert(spyro::gameplay::hasDigitalDirection(0x8000));
  assert(spyro::gameplay::digitalDirectionTableIndex(0x1000) == 1);
  assert(spyro::gameplay::digitalDirectionTableIndex(0x2000) == 2);
  assert(spyro::gameplay::digitalDirectionTableIndex(0x4000) == 4);
  assert(spyro::gameplay::digitalDirectionTableIndex(0x8000) == 8);
  std::cout << "native_gameplay: PASS\n";
  return 0;
}
