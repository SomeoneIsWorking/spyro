#include "archive_transfer_contract.h"

#include <cassert>

int main() {
  using spyro::archive_transfer::evidence;

  const auto complete = evidence(0x14800u, 0x14800u);
  assert(complete.complete());
  assert(complete.coveredEnd() == 0x14800u);

  // Artisans' collision-chain component begins near the end of its 0x14800-byte scene. A read
  // stopping one sector early has copied coherent actors near 0xACA4 but has not proved that tail.
  const auto missingTail = evidence(0x14800u, 0x14000u);
  assert(!missingTail.complete());
  assert(missingTail.coveredEnd() == 0x14000u);

  return 0;
}
