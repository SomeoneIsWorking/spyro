#include "archive_transfer_contract.h"

#include <cstdio>
#include <cstdlib>

namespace {

void require(bool condition, const char *message) {
  if (!condition) {
    std::fprintf(stderr, "archive_transfer_contract: %s\n", message);
    std::exit(1);
  }
}

} // namespace

int main() {
  using spyro::archive_transfer::decide;

  const auto complete = decide(0x14800u, 0x14800u);
  require(complete.transfer.complete(), "complete transfer was classified as short");
  require(complete.transfer.coveredEnd() == 0x14800u, "complete coverage end changed");
  require(complete.accepted(), "complete transfer was refused");
  require(complete.completionPending(), "complete transfer did not arm completion");
  require(complete.returnValue() == 1u, "complete transfer returned failure");

  // Artisans' collision-chain component begins near the end of its 0x14800-byte scene. A read
  // stopping one sector early has copied coherent actors near 0xACA4 but has not proved that tail.
  const auto missingTail = decide(0x14800u, 0x14000u);
  require(!missingTail.transfer.complete(), "truncated transfer was classified as complete");
  require(missingTail.transfer.coveredEnd() == 0x14000u, "short coverage end changed");
  require(!missingTail.accepted(), "truncated transfer was accepted");
  require(!missingTail.completionPending(), "truncated transfer armed completion");
  require(missingTail.returnValue() == 0u, "truncated transfer returned success");

  std::puts("archive_transfer_contract: PASS (complete accepted, truncated refused)");
  return 0;
}
