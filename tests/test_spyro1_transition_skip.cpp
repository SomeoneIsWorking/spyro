#include "core.h"
#include "spyro1_transition_skip.h"

#include <cstdlib>
#include <iostream>
#include <memory>

namespace {

void require(bool condition, const char *what) {
  if (!condition) {
    std::cerr << "spyro1_transition_skip: " << what << '\n';
    std::exit(1);
  }
}

void testSkipsOnlyActiveTransition() {
  auto core = std::make_unique<Core>();
  core->mem_w32(0x800757D8u, 1u);
  core->mem_w32(0x80075864u, 3u);
  core->mem_w32(0x800756ACu, 96u);
  core->mem_w32(0x800756B0u, 1u);

  require(spyro1::skipLevelTransition(*core, true), "fresh Start skips active transition");
  require(core->mem_r32(0x800756ACu) == 417u, "transition reaches exact hidden boundary");
  require(core->mem_r32(0x800756B0u) == 0u, "transition HUD is hidden");
  require(core->mem_r32(0x800757D8u) == 1u, "guest transition state remains active");
  require(core->mem_r32(0x80075864u) == 3u, "CD load stage remains active");
}

void testRefusesNonEdgeOrCompletedTransition() {
  auto core = std::make_unique<Core>();
  core->mem_w32(0x800757D8u, 1u);
  core->mem_w32(0x80075864u, 0xffffffffu);
  core->mem_w32(0x800756ACu, 20u);
  core->mem_w32(0x800756B0u, 1u);
  require(!spyro1::skipLevelTransition(*core, false), "held/non-edge input is ignored");
  require(core->mem_r32(0x800756ACu) == 20u, "non-edge leaves timing unchanged");
  require(!spyro1::skipLevelTransition(*core, true), "completed load refuses skip");
  require(core->mem_r32(0x800756B0u) == 1u, "refusal leaves HUD unchanged");
}

} // namespace

int main() {
  testSkipsOnlyActiveTransition();
  testRefusesNonEdgeOrCompletedTransition();
  std::cout << "spyro1_transition_skip: PASS (Start hides tally without bypassing load)\n";
  return 0;
}
