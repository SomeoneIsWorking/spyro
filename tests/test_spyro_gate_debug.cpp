#include "core.h"
#include "spyro_gate_debug.h"

#include <cstdlib>
#include <iostream>
#include <memory>

namespace {

void require(bool condition, const char *what) {
  if (!condition) {
    std::cerr << "spyro_gate_debug: " << what << '\n';
    std::exit(1);
  }
}

void seedGate(Core &core) {
  core.mem_w32(0x800757d8u, 0u);
  core.mem_w32(0x80075864u, 0xffffffffu);
  core.mem_w32(0x8007596cu, 10u);
  core.mem_w32(0x800758b4u, 14u);
  core.mem_w32(0x800758bcu, 1u);
  core.mem_w32(0x80078640u, 0x80010000u);
  core.mem_w32(0x80010018u, 40u);
  core.mem_w32(0x8001001cu, 14u);
  core.mem_w32(0x80010020u, 100u);
  core.mem_w32(0x80010024u, 200u);
  core.mem_w32(0x80010028u, 300u);
  core.mem_w32(0x80075828u, 0x80020000u);
  core.mem_w32(0x80020000u + 40u * 0x58u, 0x80030000u);
  core.mem_w32(0x80030000u, 0x80031000u);
  core.mem_w8(0x80031000u, 2u);
  core.mem_w32(0x80031008u, 1000u);
  core.mem_w32(0x8003100cu, 1100u);
  core.mem_w32(0x80031010u, 1200u);
  core.mem_w32(0x80031018u, static_cast<uint32_t>(-1000));
  core.mem_w32(0x8003101cu, static_cast<uint32_t>(-1100));
  core.mem_w32(0x80031020u, static_cast<uint32_t>(-1200));
}

void testInspectionAndTeleport() {
  auto core = std::make_unique<Core>();
  seedGate(*core);
  core->mem_w32(0x80078a58u, 1u);
  core->mem_w32(0x80078a5cu, 2u);
  core->mem_w32(0x80078a60u, 3u);
  spyro::gate_debug::GateInfo gate;
  require(spyro::gate_debug::inspectGate(*core, 0u, gate), "valid gate inspection");
  require(gate.levelId == 14 && gate.pathMoby == 40 && gate.nodeCount == 2u, "gate metadata");
  require(gate.firstPoint.x == 100 && gate.firstPoint.y == 200 && gate.firstPoint.z == 300,
          "portal first-point decode");
  require(gate.nodes[1].x == -1000 && gate.nodes[1].z == -1200, "path node decode");
  require(spyro::gate_debug::teleportToGate(*core, 0u, 1u), "teleport accepted in field");
  require(static_cast<int32_t>(core->mem_r32(0x80078a58u)) == -1000, "position x");
  require(static_cast<int32_t>(core->mem_r32(0x80078a5cu)) == -1100, "position y");
  require(static_cast<int32_t>(core->mem_r32(0x80078a60u)) == -1200, "position z");
  require(static_cast<int32_t>(core->mem_r32(0x80078ae4u)) == -1000, "previous position x");
}

void testTeleportRefusesOutsideGameplay() {
  auto core = std::make_unique<Core>();
  seedGate(*core);
  core->mem_w32(0x800757d8u, 1u);
  require(!spyro::gate_debug::teleportToGate(*core, 0u, 0u), "non-gameplay refusal");
}

} // namespace

int main() {
  testInspectionAndTeleport();
  testTeleportRefusesOutsideGameplay();
  std::cout << "spyro_gate_debug: PASS (portal/path decode + guarded teleport)\n";
  return 0;
}
