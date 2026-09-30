// native_spu_key_status.h — Spyro 1 (SCUS_942.28) SPU key-slot status query, owned natively.
//
// The function here replaces the guest body of the same address in the PsyQ libspu library
// (external/spyro-1/asm/psyq.s, SpuGetKeyStatus) and is held by the override differential
// (psxport docs/issues/0138): tools/native_override_gate.py.
#pragma once

#include "core.h"

namespace spyro1::native {

void registerSpuKeyStatusOverrides(Core &core);

} // namespace spyro1::native
