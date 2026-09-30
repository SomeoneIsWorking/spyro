// native_hud_collectables.h — Spyro 1 (SCUS_942.28) HUD collectible update, owned natively.
//
// The body replaces the guest function of the same address in external/spyro-1's HUD tick and is
// held by the override differential (tools/native_override_gate.py).
#pragma once

#include "core.h"

namespace spyro1::native {

void registerHudCollectableOverrides(Core &core);

} // namespace spyro1::native
