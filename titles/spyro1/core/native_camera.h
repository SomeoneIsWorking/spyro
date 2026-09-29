// native_camera.h — Spyro 1 (SCUS_942.28) camera state helpers, owned natively.
//
// Each function here replaces the guest body of the same address in external/spyro-1/src/camera.c
// and is held by the override differential (psxport docs/issues/0138):
// tools/native_override_gate.py.
#pragma once

#include "core.h"

namespace spyro1::native {

void registerCameraOverrides(Core &core);

} // namespace spyro1::native
