// native_spu_common_attr.h — Spyro 1 (SCUS_942.28) the PsyQ libspu common-attribute push, owned
// natively.
//
// The entry it replaces is 0x8005CC58, SpuSetCommonAttr, which the PsyQ library keeps as assembly
// (external/spyro-1 asm/psyq.s, between SpuInit and SpuGetVoiceVolume) rather than as a C body,
// and it is held by the override differential (psxport docs/issues/0138):
// the override differential.
#pragma once

#include "core.h"

namespace spyro1::native {

void registerSpuCommonAttrOverrides(Core &core);

} // namespace spyro1::native