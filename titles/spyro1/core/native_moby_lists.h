// native_moby_lists.h — Spyro 1 (SCUS_942.28) moby list pass: the level moby queue and the
// animation/sound pass that walks one of those queues, owned natively.
//
// Each function here replaces the guest body of the same address in external/spyro-1's
// asm/moby_lists.s and is held by the override differential (psxport docs/issues/0138):
// tools/native_override_gate.py.
#pragma once

#include "core.h"

namespace spyro1::native {

void registerMobyListOverrides(Core &core);

} // namespace spyro1::native
