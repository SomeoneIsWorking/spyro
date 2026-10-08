// native_moby_collision.h — Spyro 1 (SCUS_942.28) moby collision walk, owned natively.
//
// The body is 0x8004E3C8, external/spyro-1's func_8004E3C8 (asm/collision.s), the walker every
// moby-vs-world and moby-vs-moby collision query in the game goes through. It is held by the
// override differential (psxport docs/issues/0138): the override differential.
#pragma once

#include "core.h"

namespace spyro1::native {

void registerMobyCollisionOverrides(Core &core);

} // namespace spyro1::native
