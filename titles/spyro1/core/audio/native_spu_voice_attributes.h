// native_spu_voice_attributes.h — Spyro 1 (SCUS_942.28) SPU voice-attribute writer, owned natively.
//
// Each function here replaces the guest body of the same address in the PsyQ libspu the executable
// links (external/spyro-1/asm/psyq.s, which keeps no C body for it) and is held by the override
// differential (psxport docs/issues/0138): the override differential.
#pragma once

#include "core.h"

namespace spyro1::native {

void registerSpuVoiceAttributeOverrides(Core &core);

} // namespace spyro1::native