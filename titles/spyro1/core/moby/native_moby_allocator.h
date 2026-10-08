// native_moby_allocator.h — Spyro 1 (SCUS_942.28) the dynamic Moby allocator, owned natively: it
// hands out one 0x58-byte Moby slot and the 0x18-byte props record that slot's m_Props word is
// given.
//
// Each function here replaces the guest body of the same address in external/spyro-1's asm/42CC4.s
// (include/42CC4.h: "Allocates a new Moby", returning the pointer it allocated) and is held by the
// override differential (psxport docs/issues/0138): the override differential.
#pragma once

#include "core.h"

namespace spyro1::native {

void registerMobyAllocatorOverrides(Core &core);

} // namespace spyro1::native
