// native_spu_state.h — Spyro 1 (SCUS_942.28) PsyQ libspu state words, owned natively.
//
// The SCE sound library keeps its own bookkeeping in a structure it reaches through a pointer in
// initialised data (claim C074), and the entries here rewrite fields of that structure instead of
// hardware: no SPU register, table dispatch or back-edge is involved.
//
// Each function here replaces the guest body of the same address in the retail executable and is
// held by the override differential (psxport docs/issues/0138): the override differential.
#pragma once

#include "core.h"

namespace spyro1::native {

void registerSpuStateOverrides(Core &core);

} // namespace spyro1::native
