// Binary-derived layout for InitActorMeshScratchRegions (0x8005B6F8).
#pragma once

#include <cstdint>

namespace spyro {

struct ActorMeshScratchLayout {
  uint32_t emitList;
  uint32_t otBase;
  uint32_t primTop;
  uint32_t primBase1;
  uint32_t regionBase;
};

// SCUS_942.28 0x8005B6F8..0x8005B754: both modes carve the fixed emit/OT/prim
// headers first, then use either two 0x1C000-byte whole regions or two 0x13000-byte split regions.
constexpr ActorMeshScratchLayout actorMeshScratchLayout(uint32_t workTop, bool split) {
  const uint32_t primTop = workTop - 0x6008u;
  const uint32_t stride = split ? 0x13000u : 0x1C000u;
  const uint32_t primBase1 = primTop - stride;
  return {
      .emitList = workTop - 0x2000u,
      .otBase = workTop - 0x6000u,
      .primTop = primTop,
      .primBase1 = primBase1,
      .regionBase = primBase1 - stride,
  };
}

} // namespace spyro
