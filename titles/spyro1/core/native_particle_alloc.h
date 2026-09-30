// native_particle_alloc.h — Spyro 1 (SCUS_942.28) particle pool allocation, owned natively.
//
// Each function here replaces the guest body of the same address in external/spyro-1's
// src/particle_alloc.c and is held by the override differential (psxport docs/issues/0138):
// tools/native_override_gate.py.
#pragma once

#include "core.h"

namespace spyro1::native {

void registerParticleAllocOverrides(Core &core);

} // namespace spyro1::native
