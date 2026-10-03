// native_shaded_moby_queue.h — Spyro 1 (SCUS_942.28) shaded-Moby queue assembly, owned natively.
//
// Each function here replaces the guest body of the same address in
// external/spyro-1/src/gamestates/draw.c and is held by the override differential (psxport
// docs/issues/0138): the override differential.
#pragma once

#include "core.h"

namespace spyro1::native {

void registerShadedMobyQueueOverrides(Core &core);

} // namespace spyro1::native
