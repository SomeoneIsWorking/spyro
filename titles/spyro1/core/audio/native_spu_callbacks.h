// native_spu_callbacks.h — Spyro 1 (SCUS_942.28) the PsyQ SPU library's IRQ-callback registration,
// owned natively.
//
// One PsyQ libspu entry, reached once per boot from the SPU init routine at 0x8005BB78 with the
// guest's own SPU IRQ routine. It is held by the override differential on the artisans-walk route
// .
#pragma once

#include "core.h"

namespace spyro1::native {

void registerSpuCallbackOverrides(Core &core);

} // namespace spyro1::native
