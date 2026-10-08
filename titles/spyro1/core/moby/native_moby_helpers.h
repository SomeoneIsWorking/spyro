// native_moby_helpers.h — Spyro 1 (SCUS_942.28) moby (game object) lifecycle, collision, timer,
// ground position, sound and animation-frame helpers, owned natively.
//
// Each function here replaces the guest body of the same address in
// external/spyro-1/src/moby_helpers.c, external/spyro-1/src/moby_interpolation_check.c or the
// moby allocation and collision unit external/spyro-1/asm/42CC4.s, and is held by the override
// differential (psxport docs/issues/0138): the override differential.
#pragma once

#include "core.h"

namespace spyro1::native {

void registerMobyHelperOverrides(Core &core);

} // namespace spyro1::native
