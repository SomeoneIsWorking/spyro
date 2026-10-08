// native_level_globals_table.h — Spyro 1 (SCUS_942.28) per-level globals fixup, owned natively.
//
// Each function here replaces the guest body of the address it is named for (0x8005A470, which
// external/open-spyro documents in include/funcs.h) and is held by the override differential
// .
#pragma once

#include "core.h"

namespace spyro1::native {

void registerLevelGlobalsTableOverrides(Core &core);

} // namespace spyro1::native
