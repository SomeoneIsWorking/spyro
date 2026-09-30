// native_draw_setup.h — Spyro 1 (SCUS_942.28) draw-area state helpers, owned natively.
//
// Each function here replaces the guest body of the same entry address in the decomp's draw area
// and is held by the override differential (tools/native_override_gate.py).
#pragma once

#include "core.h"

namespace spyro1::native {

void registerDrawSetupOverrides(Core &core);

} // namespace spyro1::native
