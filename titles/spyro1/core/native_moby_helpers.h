// native_moby_helpers.h — Spyro 1 (SCUS_942.28) moby (game object) lifecycle, timer, ground
// position, sound and animation-frame helpers, owned natively.
//
// Each function here replaces the guest body of the same address in
// external/spyro-1/src/moby_helpers.c or external/spyro-1/src/moby_interpolation_check.c and is
// held by the override differential (psxport docs/issues/0138): tools/native_override_gate.py.
#pragma once

#include "core.h"

namespace spyro1::native {

void registerMobyHelperOverrides(Core &core);

} // namespace spyro1::native
