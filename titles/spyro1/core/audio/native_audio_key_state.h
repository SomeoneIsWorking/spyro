// native_audio_key_state.h — Spyro 1 (SCUS_942.28) libspu CD/XA key bit position, owned natively.
//
// Each function here replaces the guest body of the same address in the PsyQ libspu library
// (external/spyro-1/asm/psyq.s) and is held by the override differential (psxport
// docs/issues/0138): the override differential.
#pragma once

#include "core.h"

namespace spyro1::native {

void registerAudioKeyStateOverrides(Core &core);

} // namespace spyro1::native
