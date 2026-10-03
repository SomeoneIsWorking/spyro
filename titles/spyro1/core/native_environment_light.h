// native_environment_light.h — Spyro 1 (SCUS_942.28) environment light propagation, owned natively.
//
// Replaces the guest body of the same address in external/spyro-1's r_environment area and is held
// by the override differential (psxport docs/issues/0138): the override differential.
#pragma once

#include "core.h"

namespace spyro1::native {

void registerEnvironmentLightOverrides(Core &core);

} // namespace spyro1::native
