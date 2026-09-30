// native_shared_models.h — Spyro 1 (SCUS_942.28) the PETE.WAD shared-model load, owned natively.
//
// Each function here replaces the guest body of the same address in
// external/spyro-1/src/4BEF8.c and is held by the override differential (psxport docs/issues/0138):
// tools/native_override_gate.py.
#pragma once

#include "core.h"

namespace spyro1::native {

void registerSharedModelOverrides(Core &core);

} // namespace spyro1::native
