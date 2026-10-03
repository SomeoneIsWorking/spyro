// native_player_physics.h — Spyro 1 (SCUS_942.28) player physics: the damped settle of Spyro's
// per-axis rotation channels, the ground-probe push into his acceleration, and the per-level
// initialisation that re-arms that state. Owned natively.
//
// Each function here replaces the guest body of the same address in external/spyro-1/src/pete.c
// and is held by the override differential (psxport docs/issues/0138):
// the override differential.
#pragma once

#include "core.h"

namespace spyro1::native {

void registerPlayerPhysicsOverrides(Core &core);

} // namespace spyro1::native
