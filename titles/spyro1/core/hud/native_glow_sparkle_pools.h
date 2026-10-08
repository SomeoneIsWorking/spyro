// native_glow_sparkle_pools.h — Spyro 1 (SCUS_942.28) glow and sparkle record pools, owned
// natively.
//
// Each function here replaces the guest body of the same address in the decompilation's
// r_particles area and is held by the override differential (psxport docs/issues/0138):
// the override differential.
#pragma once

#include "core.h"

namespace spyro1::native {

void registerGlowSparklePoolOverrides(Core &core);

} // namespace spyro1::native
