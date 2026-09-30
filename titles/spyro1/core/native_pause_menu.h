// native_pause_menu.h — Spyro 1 (SCUS_942.28) the pause menu's per-field input tick, owned
// natively.
//
// Replaces the guest body at 0x8002E12C in external/spyro-1/src/update.c and is held by the
// override differential (psxport docs/issues/0138): tools/native_override_gate.py.
#pragma once

#include "core.h"

namespace spyro1::native {

void registerPauseMenuOverrides(Core &core);

} // namespace spyro1::native
