// native_pixel_fade.h — Spyro 1 (SCUS_942.28) subtractive pixel-run fade, owned natively.
//
// Each function here replaces the guest body of the same address in the retail executable and is
// held by the override differential (psxport docs/issues/0138): tools/native_override_gate.py.
#pragma once

#include "core.h"

namespace spyro1::native {

void registerPixelFadeOverrides(Core &core);

} // namespace spyro1::native
