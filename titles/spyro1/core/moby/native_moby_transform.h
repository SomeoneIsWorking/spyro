// native_moby_transform.h — Spyro 1 (SCUS_942.28) moby (game object) world transform: the body that
// re-derives one moby's segment position row, files it into the rotation-indexed moby list and
// rebuilds its 3x3 orientation matrix through the GTE. Owned natively.
//
// The function here replaces the guest body of the same address in external/spyro-1 and is held by
// the override differential (psxport docs/issues/0138): the override differential.
#pragma once

#include "core.h"

namespace spyro1::native {

void registerMobyTransformOverrides(Core &core);

} // namespace spyro1::native
