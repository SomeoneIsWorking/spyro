// native_spu_registers.h — Spyro 1 (SCUS_942.28) writes into the PsyQ SPU register file, owned
// natively.
//
// Every address in this module is in external/spyro-1/asm/psyq.s, the PsyQ libspu library, which
// the decompilation carries as a listing with no C body for these entries: the retail disassembly
// is the only authority for what they do. Each function here replaces the guest body of the same
// address and is held by the override differential (psxport docs/issues/0138):
// tools/native_override_gate.py.
#pragma once

#include "core.h"

namespace spyro1::native {

void registerSpuRegisterOverrides(Core &core);

} // namespace spyro1::native
