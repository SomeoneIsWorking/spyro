// native_level_initialization.h — Spyro 1 (SCUS_942.28) the per-level startup globals a level's
// own entry point writes, owned natively.
//
// Each function here replaces the guest body of the same address in the decompilation's
// initialization area and is held by the override differential .
#pragma once

#include "core.h"

namespace spyro1::native {

void registerLevelInitializationOverrides(Core &core);

} // namespace spyro1::native