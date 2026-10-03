// native_spu_voice_pitch.h — Spyro 1 (SCUS_942.28) PsyQ libspu voice pitch, owned natively.
//
// The body replaced here is libspu's own SpuSetVoicePitch, which the game's sound layer reaches
// through a `jal` inside the resident executable. It is held by the override differential
// (psxport docs/issues/0138): the override differential.
#pragma once

#include "core.h"

namespace spyro1::native {

void registerSpuVoicePitchOverrides(Core &core);

} // namespace spyro1::native
