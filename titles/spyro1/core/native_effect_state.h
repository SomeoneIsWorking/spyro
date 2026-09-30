// native_effect_state.h — Spyro 1 (SCUS_942.28) the per-frame effect state the native producers
// read: the ground-shadow ring.
//
// Each function here replaces the guest body of the same address in external/spyro-1/src/pete.c and
// is held by the override differential (psxport docs/issues/0138): tools/native_override_gate.py.
#pragma once

#include "core.h"

namespace spyro1::native {

void registerEffectStateOverrides(Core &core);

} // namespace spyro1::native
